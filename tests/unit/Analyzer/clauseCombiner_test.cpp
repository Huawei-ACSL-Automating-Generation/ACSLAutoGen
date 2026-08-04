#include "Analyzer/NumericalInvariant/clauseCombiner.h"
#include "testHelper.h"

#include <gtest/gtest.h>

#include <limits>

namespace {
    namespace invariant = acslg::analyzer::invariant;
    namespace symbolic  = acslg::analyzer::symbolic;

    class ClauseCombinerPrototypeTest : public acslg::test::utils::FixtureWithCode {
      protected:
        symbolic::Expr variable(unsigned id) {
            const auto declaration = getVarDecl(id);
            const auto address     = symbolic::Addr::variable(declaration);
            return symbolic::Expr::symbolValue(symbolic::deriveType(declaration->getType()),
                                               address, defaultPoint);
        }

        symbolic::Expr variable(unsigned id, symbolic::ExprType type) {
            const auto address = symbolic::Addr::variable(getVarDecl(id));
            return symbolic::Expr::symbolValue(type, address, defaultPoint);
        }

        symbolic::Expr integer(symbolic::ExprFactory &factory, int value) {
            return symbolic::LiteralExpr{factory, value};
        }

        symbolic::Expr boolean(symbolic::ExprFactory &factory, bool value) {
            return symbolic::LiteralExpr{factory, value};
        }
    };
} // namespace

#if ACSLG_NUMERICAL_INVARIANT_HAS_Z3
TEST_F(ClauseCombinerPrototypeTest, FindsDisjunctionNeededForInitiationAndExitPost) {
    const auto x    = variable(0);
    const auto n    = variable(1);
    auto &factory   = x.factory();
    const auto zero = integer(factory, 0);

    invariant::TransitionModel model{{{x, zero}},
                                     {n},
                                     x.equalTo(zero),
                                     x.lessThan(n),
                                     {{boolean(factory, true), {x + integer(factory, 1)}}},
                                     n.lessThan(zero).logicalOr(x.equalTo(n))};

    const auto weak = invariant::verifyInvariant(model, x.lessEqual(n));
    ASSERT_EQ(weak.status, invariant::VerificationStatus::Refuted);
    EXPECT_EQ(weak.failedObligation, invariant::ObligationKind::Initiation);

    const auto result =
        invariant::combineInvariantClauses(model, {x.lessEqual(n), x.equalTo(zero)});
    ASSERT_EQ(result.status, invariant::CombinationStatus::Found) << result.reason;
    ASSERT_TRUE(result.invariant.has_value());
    EXPECT_TRUE(result.invariant->structurallyEqual(x.lessEqual(n).logicalOr(x.equalTo(zero))));
    EXPECT_GT(result.counterexampleFilteredCandidates, 0U);
    EXPECT_LE(result.solverQueries, 64U);
}

TEST_F(ClauseCombinerPrototypeTest, FindsConjunctionNeededForExitPost) {
    const auto x     = variable(0);
    const auto y     = variable(1);
    const auto index = variable(2);
    const auto limit = variable(3);
    auto &factory    = x.factory();
    const auto zero  = integer(factory, 0);
    const auto one   = integer(factory, 1);

    const auto precondition =
        x.equalTo(zero).logicalAnd(y.equalTo(zero)).logicalAnd(index.equalTo(zero));
    invariant::TransitionModel model{{{x, zero}, {y, zero}, {index, zero}},
                                     {limit},
                                     precondition,
                                     index.lessThan(limit),
                                     {{boolean(factory, true), {x + one, y + one, index + one}}},
                                     x.greaterEqual(zero).logicalAnd(y.greaterEqual(zero))};

    const auto result =
        invariant::combineInvariantClauses(model, {x.greaterEqual(zero), y.greaterEqual(zero)});
    ASSERT_EQ(result.status, invariant::CombinationStatus::Found) << result.reason;
    ASSERT_TRUE(result.invariant.has_value());
    EXPECT_TRUE(
        result.invariant->structurallyEqual(x.greaterEqual(zero).logicalAnd(y.greaterEqual(zero))));
}

TEST_F(ClauseCombinerPrototypeTest, CanRequireCandidateToDependOnChangedState) {
    const auto x    = variable(0);
    const auto y    = variable(1);
    auto &factory   = x.factory();
    const auto zero = integer(factory, 0);
    const auto one  = integer(factory, 1);
    invariant::TransitionModel model{{{x, zero}, {y, zero}},
                                     {},
                                     x.equalTo(zero).logicalAnd(y.equalTo(zero)),
                                     x.lessThan(integer(factory, 10)),
                                     {{boolean(factory, true), {x + one, y}}},
                                     std::nullopt};
    const auto unchangedOnly = y.equalTo(model.variables[1].entry);
    const auto changed       = x.greaterEqual(model.variables[0].entry);

    EXPECT_FALSE(invariant::dependsOnChangedStateVariable(model, unchangedOnly));
    EXPECT_TRUE(invariant::dependsOnChangedStateVariable(model, changed));
    EXPECT_TRUE(invariant::dependsOnChangedStateVariable(
        model, unchangedOnly.logicalAnd(changed)));

    const auto defaultResult = invariant::combineInvariantClauses(model, {unchangedOnly});
    EXPECT_EQ(defaultResult.status, invariant::CombinationStatus::Found);

    invariant::ClauseLimits strict;
    strict.requireChangedStateVariable = true;
    const auto rejected = invariant::combineInvariantClauses(model, {unchangedOnly}, strict);
    EXPECT_EQ(rejected.status, invariant::CombinationStatus::NoInvariant);
    EXPECT_EQ(rejected.generatedCandidates, 0U);
    EXPECT_EQ(rejected.attemptedCandidates, 0U);

    const auto accepted =
        invariant::combineInvariantClauses(model, {unchangedOnly, changed}, strict);
    EXPECT_EQ(accepted.status, invariant::CombinationStatus::Found) << accepted.reason;
    ASSERT_TRUE(accepted.invariant.has_value());
    EXPECT_TRUE(invariant::dependsOnChangedStateVariable(model, *accepted.invariant));
}

TEST_F(ClauseCombinerPrototypeTest, ReportsCounterexampleAndFailingBranch) {
    const auto x    = variable(0);
    auto &factory   = x.factory();
    const auto zero = integer(factory, 0);
    const auto one  = integer(factory, 1);

    invariant::TransitionModel model{{{x, zero}},
                                     {},
                                     x.equalTo(zero),
                                     x.lessThan(integer(factory, 10)),
                                     {{x.equalTo(zero), {x + one}}, {x.equalTo(zero), {x - one}}},
                                     x.greaterEqual(zero)};

    const auto result = invariant::verifyInvariant(model, x.greaterEqual(zero));
    ASSERT_EQ(result.status, invariant::VerificationStatus::Refuted);
    EXPECT_EQ(result.failedObligation, invariant::ObligationKind::Consecution);
    ASSERT_TRUE(result.failedBranch.has_value());
    EXPECT_EQ(*result.failedBranch, 1U);
    EXPECT_TRUE(result.counterexample.has_value());
    EXPECT_FALSE(result.counterexample->bindings.empty());

    const auto combined = invariant::combineInvariantClauses(model, {x.greaterEqual(zero)});
    ASSERT_EQ(combined.status, invariant::CombinationStatus::NoInvariant);
    ASSERT_TRUE(combined.lastFailure.has_value());
    EXPECT_EQ(combined.lastFailure->failedObligation, invariant::ObligationKind::Consecution);
    EXPECT_EQ(combined.lastFailure->failedBranch, 1U);
}

TEST_F(ClauseCombinerPrototypeTest, EnforcesQueryBudgetAndRejectsUnsupportedExpressions) {
    const auto x    = variable(0);
    auto &factory   = x.factory();
    const auto zero = integer(factory, 0);
    invariant::TransitionModel model{{{x, zero}},
                                     {},
                                     x.equalTo(zero),
                                     x.lessThan(integer(factory, 2)),
                                     {{boolean(factory, true), {x + integer(factory, 1)}}},
                                     x.greaterEqual(zero)};

    invariant::ClauseLimits noQueries;
    noQueries.maxSolverQueries = 0;
    EXPECT_EQ(invariant::verifyInvariant(model, x.greaterEqual(zero), noQueries).status,
              invariant::VerificationStatus::BudgetExceeded);

    EXPECT_EQ(
        invariant::verifyInvariant(model, (x / integer(factory, 2)).greaterEqual(zero)).status,
        invariant::VerificationStatus::Unsupported);
}

TEST_F(ClauseCombinerPrototypeTest, TreatsSolverUnknownAsUnproved) {
    const auto x    = variable(0);
    auto &factory   = x.factory();
    const auto zero = integer(factory, 0);
    invariant::TransitionModel model{{{x, zero}},
                                     {},
                                     x.equalTo(zero),
                                     x.lessThan(integer(factory, 100)),
                                     {{boolean(factory, true), {x + integer(factory, 1)}}},
                                     x.greaterEqual(zero)};

    invariant::ClauseLimits limits;
    limits.solverResourceLimit = 1;
    const auto result          = invariant::verifyInvariant(model, x.greaterEqual(zero), limits);
    EXPECT_EQ(result.status, invariant::VerificationStatus::Unknown);
}

TEST_F(ClauseCombinerPrototypeTest, ProducesDeterministicCombination) {
    const auto x    = variable(0);
    const auto n    = variable(1);
    auto &factory   = x.factory();
    const auto zero = integer(factory, 0);
    invariant::TransitionModel model{{{x, zero}},
                                     {n},
                                     x.equalTo(zero),
                                     x.lessThan(n),
                                     {{boolean(factory, true), {x + integer(factory, 1)}}},
                                     n.lessThan(zero).logicalOr(x.equalTo(n))};
    const std::vector atoms{x.lessEqual(n), x.equalTo(zero)};

    const auto first  = invariant::combineInvariantClauses(model, atoms);
    const auto second = invariant::combineInvariantClauses(model, atoms);
    ASSERT_EQ(first.status, invariant::CombinationStatus::Found);
    ASSERT_EQ(second.status, invariant::CombinationStatus::Found);
    ASSERT_TRUE(first.invariant && second.invariant);
    EXPECT_TRUE(first.invariant->structurallyEqual(*second.invariant));
    EXPECT_EQ(first.generatedCandidates, second.generatedCandidates);
    EXPECT_EQ(first.attemptedCandidates, second.attemptedCandidates);
    EXPECT_EQ(first.solverQueries, second.solverQueries);
}

TEST_F(ClauseCombinerPrototypeTest, ProvesBoundedSignedIncrementIsDefined) {
    const auto x    = variable(0, {symbolic::ExprScalarKind::Int, 32});
    const auto n    = variable(1, {symbolic::ExprScalarKind::Int, 32});
    auto &factory   = x.factory();
    const auto zero = integer(factory, 0);
    const auto one  = integer(factory, 1);
    invariant::TransitionModel model{{{x, zero}},
                                     {n},
                                     x.equalTo(zero),
                                     x.lessThan(n),
                                     {{boolean(factory, true), {x + one}}},
                                     std::nullopt,
                                     invariant::TransitionIntegerSemantics::CMachine};

    const auto result = invariant::verifyInvariant(model, boolean(factory, true));

    EXPECT_EQ(result.status, invariant::VerificationStatus::Proved) << result.reason;
    EXPECT_EQ(result.solverQueries, 4U);
}

TEST_F(ClauseCombinerPrototypeTest, ReportsSignedOverflowAsDefinednessFailure) {
    const auto x    = variable(0, {symbolic::ExprScalarKind::Int, 32});
    auto &factory   = x.factory();
    const auto zero = integer(factory, 0);
    const auto one  = integer(factory, 1);
    invariant::TransitionModel model{{{x, zero}},
                                     {},
                                     x.equalTo(zero),
                                     boolean(factory, true),
                                     {{boolean(factory, true), {x + one}}},
                                     std::nullopt,
                                     invariant::TransitionIntegerSemantics::CMachine};

    const auto result = invariant::verifyInvariant(model, boolean(factory, true));

    EXPECT_EQ(result.status, invariant::VerificationStatus::Refuted);
    EXPECT_EQ(result.failedObligation, invariant::ObligationKind::Definedness);
    EXPECT_EQ(result.failedBranch, 0U);
    ASSERT_TRUE(result.counterexample.has_value());
}

TEST_F(ClauseCombinerPrototypeTest, ProvesAndRefutesMemoryAccessBounds) {
    const auto x = variable(0, {symbolic::ExprScalarKind::Int, 32});
    auto &factory = x.factory();
    const auto zero = integer(factory, 0);
    const auto five = integer(factory, 5);
    const auto boolType =
        symbolic::ExprType{symbolic::ExprScalarKind::Bool, 1};
    const auto inBounds = x.lessThan(five).withType(boolType);
    invariant::TransitionModel model{
        {{x, zero}},
        {},
        x.equalTo(zero),
        boolean(factory, true),
        {{boolean(factory, true), {x}, {inBounds}}},
        std::nullopt,
        invariant::TransitionIntegerSemantics::CMachine};

    const auto unsafe = invariant::verifyInvariant(model, boolean(factory, true));
    EXPECT_EQ(unsafe.status, invariant::VerificationStatus::Refuted);
    EXPECT_EQ(unsafe.failedObligation, invariant::ObligationKind::Definedness);
    EXPECT_EQ(unsafe.failedBranch, 0U);

    const auto safe = invariant::verifyInvariant(model, inBounds);
    EXPECT_EQ(safe.status, invariant::VerificationStatus::Proved) << safe.reason;
}

TEST_F(ClauseCombinerPrototypeTest, ModelsUnsignedIncrementAsModuloArithmetic) {
    const auto type = symbolic::ExprType{symbolic::ExprScalarKind::UInt, 32};
    const auto x    = variable(0, type);
    auto &factory   = x.factory();
    const auto zero = symbolic::LiteralExpr{factory, std::uint32_t{0}};
    const auto one  = symbolic::LiteralExpr{factory, std::uint32_t{1}};
    const auto max  = symbolic::LiteralExpr{factory, std::uint32_t{0xffffffffU}};
    invariant::TransitionModel model{{{x, zero}},
                                     {},
                                     x.equalTo(zero),
                                     boolean(factory, true),
                                     {{boolean(factory, true), {x + one}}},
                                     std::nullopt,
                                     invariant::TransitionIntegerSemantics::CMachine};
    const auto range = x.greaterEqual(zero).logicalAnd(x.lessEqual(max));

    const auto result = invariant::verifyInvariant(model, range);

    EXPECT_EQ(result.status, invariant::VerificationStatus::Proved) << result.reason;
}

TEST_F(ClauseCombinerPrototypeTest, ModelsUInt64IncrementAsModuloArithmetic) {
    const auto type = symbolic::ExprType{symbolic::ExprScalarKind::UInt, 64};
    const auto x    = variable(0, type);
    auto &factory   = x.factory();
    const auto zero = symbolic::LiteralExpr{factory, std::uint64_t{0}};
    const auto one  = symbolic::LiteralExpr{factory, std::uint64_t{1}};
    const auto maximum =
        symbolic::LiteralExpr{factory, std::numeric_limits<std::uint64_t>::max()};
    invariant::TransitionModel model{
        {{x, maximum}},
        {},
        x.equalTo(maximum),
        x.equalTo(maximum),
        {{boolean(factory, true), {x + one}}},
        std::nullopt,
        invariant::TransitionIntegerSemantics::CMachine};
    const auto candidate = x.equalTo(maximum).logicalOr(x.equalTo(zero));

    const auto result = invariant::verifyInvariant(model, candidate);

    EXPECT_EQ(result.status, invariant::VerificationStatus::Proved) << result.reason;
}

TEST_F(ClauseCombinerPrototypeTest, ReportsInt64OverflowAsDefinednessFailure) {
    const auto type = symbolic::ExprType{symbolic::ExprScalarKind::Int, 64};
    const auto x    = variable(0, type);
    auto &factory   = x.factory();
    const auto one  = symbolic::LiteralExpr{factory, std::int64_t{1}};
    const auto maximum =
        symbolic::LiteralExpr{factory, std::numeric_limits<std::int64_t>::max()};
    invariant::TransitionModel model{
        {{x, maximum}},
        {},
        x.equalTo(maximum),
        boolean(factory, true),
        {{boolean(factory, true), {x + one}}},
        std::nullopt,
        invariant::TransitionIntegerSemantics::CMachine};

    const auto result = invariant::verifyInvariant(model, boolean(factory, true));

    EXPECT_EQ(result.status, invariant::VerificationStatus::Refuted);
    EXPECT_EQ(result.failedObligation, invariant::ObligationKind::Definedness);
    EXPECT_EQ(result.failedBranch, 0U);
}

TEST_F(ClauseCombinerPrototypeTest, ModelsSignedDivisionAndRemainderTowardZero) {
    const auto x          = variable(0, {symbolic::ExprScalarKind::Int, 32});
    auto &factory         = x.factory();
    const auto minusThree = integer(factory, -3);
    const auto minusOne   = integer(factory, -1);
    const auto two        = integer(factory, 2);
    invariant::TransitionModel model{{{x, minusThree}},
                                     {},
                                     x.equalTo(minusThree),
                                     x.equalTo(minusThree),
                                     {{boolean(factory, true), {x / two}}},
                                     std::nullopt,
                                     invariant::TransitionIntegerSemantics::CMachine};
    const auto candidate = x.equalTo(minusThree).logicalOr(x.equalTo(minusOne));

    const auto divisionResult = invariant::verifyInvariant(model, candidate);
    EXPECT_EQ(divisionResult.status, invariant::VerificationStatus::Proved)
        << divisionResult.reason;

    model.branches[0].nextValues[0] = x.binary(symbolic::BinaryOp::Remainder, two);
    const auto remainderResult      = invariant::verifyInvariant(model, candidate);
    EXPECT_EQ(remainderResult.status, invariant::VerificationStatus::Proved)
        << remainderResult.reason;
}

TEST_F(ClauseCombinerPrototypeTest, ReportsDivisionUndefinedBehavior) {
    const auto x        = variable(0, {symbolic::ExprScalarKind::Int, 32});
    const auto divisor  = variable(1, {symbolic::ExprScalarKind::Int, 32});
    auto &factory       = x.factory();
    const auto zero     = integer(factory, 0);
    const auto minusOne = integer(factory, -1);
    const auto minimum  = symbolic::LiteralExpr{factory, std::numeric_limits<std::int32_t>::min()};

    invariant::TransitionModel divideByZero{{{x, zero}, {divisor, zero}},
                                            {},
                                            x.equalTo(zero).logicalAnd(divisor.equalTo(zero)),
                                            boolean(factory, true),
                                            {{boolean(factory, true), {x / divisor, divisor}}},
                                            std::nullopt,
                                            invariant::TransitionIntegerSemantics::CMachine};
    const auto zeroResult = invariant::verifyInvariant(divideByZero, boolean(factory, true));
    EXPECT_EQ(zeroResult.status, invariant::VerificationStatus::Refuted);
    EXPECT_EQ(zeroResult.failedObligation, invariant::ObligationKind::Definedness);

    invariant::TransitionModel quotientOverflow{{{x, minimum}},
                                                {},
                                                x.equalTo(minimum),
                                                boolean(factory, true),
                                                {{boolean(factory, true), {x / minusOne}}},
                                                std::nullopt,
                                                invariant::TransitionIntegerSemantics::CMachine};
    const auto overflowResult =
        invariant::verifyInvariant(quotientOverflow, boolean(factory, true));
    EXPECT_EQ(overflowResult.status, invariant::VerificationStatus::Refuted);
    EXPECT_EQ(overflowResult.failedObligation, invariant::ObligationKind::Definedness);
}

TEST_F(ClauseCombinerPrototypeTest, ModelsUnsignedRemainder) {
    const auto type  = symbolic::ExprType{symbolic::ExprScalarKind::UInt, 32};
    const auto x     = variable(0, type);
    auto &factory    = x.factory();
    const auto seven = symbolic::LiteralExpr{factory, std::uint32_t{7}};
    const auto four  = symbolic::LiteralExpr{factory, std::uint32_t{4}};
    invariant::TransitionModel model{
        {{x, seven}},
        {},
        x.equalTo(seven),
        x.equalTo(seven),
        {{boolean(factory, true), {x.binary(symbolic::BinaryOp::Remainder, four)}}},
        std::nullopt,
        invariant::TransitionIntegerSemantics::CMachine};
    const auto candidate = x.equalTo(seven).logicalOr(x.lessThan(four));

    const auto result = invariant::verifyInvariant(model, candidate);

    EXPECT_EQ(result.status, invariant::VerificationStatus::Proved) << result.reason;
}

TEST_F(ClauseCombinerPrototypeTest, ModelsUnsignedShiftsWithSignedCount) {
    const auto unsignedType = symbolic::ExprType{symbolic::ExprScalarKind::UInt, 32};
    const auto signedType   = symbolic::ExprType{symbolic::ExprScalarKind::Int, 32};
    const auto x            = variable(0, unsignedType);
    const auto count        = variable(1, signedType);
    auto &factory           = x.factory();
    const auto one          = symbolic::LiteralExpr{factory, std::uint32_t{1}};
    const auto highBit      = symbolic::LiteralExpr{factory, std::uint32_t{0x80000000U}};
    const auto thirtyOne    = integer(factory, 31);

    invariant::TransitionModel leftShift{
        {{x, one}, {count, thirtyOne}},
        {},
        x.equalTo(one).logicalAnd(count.equalTo(thirtyOne)),
        x.equalTo(one),
        {{boolean(factory, true), {x.binary(symbolic::BinaryOp::ShiftLeft, count), count}}},
        std::nullopt,
        invariant::TransitionIntegerSemantics::CMachine};
    const auto leftCandidate =
        x.equalTo(one).logicalOr(x.equalTo(highBit)).logicalAnd(count.equalTo(thirtyOne));
    const auto leftResult = invariant::verifyInvariant(leftShift, leftCandidate);
    EXPECT_EQ(leftResult.status, invariant::VerificationStatus::Proved)
        << leftResult.reason << " obligation="
        << (leftResult.failedObligation
                ? std::to_string(static_cast<int>(*leftResult.failedObligation))
                : "none");

    invariant::TransitionModel rightShift{
        {{x, highBit}, {count, thirtyOne}},
        {},
        x.equalTo(highBit).logicalAnd(count.equalTo(thirtyOne)),
        x.equalTo(highBit),
        {{boolean(factory, true), {x.binary(symbolic::BinaryOp::ShiftRight, count), count}}},
        std::nullopt,
        invariant::TransitionIntegerSemantics::CMachine};
    const auto rightCandidate =
        x.equalTo(highBit).logicalOr(x.equalTo(one)).logicalAnd(count.equalTo(thirtyOne));
    const auto rightResult = invariant::verifyInvariant(rightShift, rightCandidate);
    EXPECT_EQ(rightResult.status, invariant::VerificationStatus::Proved)
        << rightResult.reason << " obligation="
        << (rightResult.failedObligation
                ? std::to_string(static_cast<int>(*rightResult.failedObligation))
                : "none");
}

TEST_F(ClauseCombinerPrototypeTest, ModelsUInt64ShiftWithInt32Count) {
    const auto uint64Type = symbolic::ExprType{symbolic::ExprScalarKind::UInt, 64};
    const auto int32Type  = symbolic::ExprType{symbolic::ExprScalarKind::Int, 32};
    const auto x          = variable(0, uint64Type);
    const auto count      = variable(1, int32Type);
    auto &factory         = x.factory();
    const auto one        = symbolic::LiteralExpr{factory, std::uint64_t{1}};
    const auto highBit =
        symbolic::LiteralExpr{factory, std::uint64_t{1} << 63U};
    const auto sixtyThree = integer(factory, 63);
    invariant::TransitionModel model{
        {{x, one}, {count, sixtyThree}},
        {},
        x.equalTo(one).logicalAnd(count.equalTo(sixtyThree)),
        x.equalTo(one),
        {{boolean(factory, true), {x.binary(symbolic::BinaryOp::ShiftLeft, count), count}}},
        std::nullopt,
        invariant::TransitionIntegerSemantics::CMachine};
    const auto candidate =
        x.equalTo(one).logicalOr(x.equalTo(highBit)).logicalAnd(count.equalTo(sixtyThree));

    const auto result = invariant::verifyInvariant(model, candidate);

    EXPECT_EQ(result.status, invariant::VerificationStatus::Proved) << result.reason;

    model.variables[1].entry = integer(factory, 64);
    model.precondition = x.equalTo(one).logicalAnd(count.equalTo(model.variables[1].entry));
    const auto invalid = invariant::verifyInvariant(model, boolean(factory, true));
    EXPECT_EQ(invalid.status, invariant::VerificationStatus::Refuted);
    EXPECT_EQ(invalid.failedObligation, invariant::ObligationKind::Definedness);
}

TEST_F(ClauseCombinerPrototypeTest, ReportsUndefinedShiftOperations) {
    const auto unsignedType = symbolic::ExprType{symbolic::ExprScalarKind::UInt, 32};
    const auto signedType   = symbolic::ExprType{symbolic::ExprScalarKind::Int, 32};
    const auto unsignedX    = variable(0, unsignedType);
    const auto signedX      = variable(1, signedType);
    const auto count        = variable(2, signedType);
    auto &factory           = unsignedX.factory();
    const auto unsignedOne  = symbolic::LiteralExpr{factory, std::uint32_t{1}};
    const auto signedOne    = integer(factory, 1);
    const auto thirtyTwo    = integer(factory, 32);

    auto verifyUpdate = [&](const symbolic::Expr &current, const symbolic::Expr &entry,
                            const symbolic::Expr &next, const symbolic::Expr &countEntry) {
        invariant::TransitionModel model{
            {{current, entry}, {count, countEntry}},
            {},
            current.equalTo(entry).logicalAnd(count.equalTo(countEntry)),
            boolean(factory, true),
            {{boolean(factory, true), {next, count}}},
            std::nullopt,
            invariant::TransitionIntegerSemantics::CMachine};
        return invariant::verifyInvariant(model, boolean(factory, true));
    };

    const auto invalidCount = verifyUpdate(
        unsignedX, unsignedOne, unsignedX.binary(symbolic::BinaryOp::ShiftLeft, count), thirtyTwo);
    EXPECT_EQ(invalidCount.status, invariant::VerificationStatus::Refuted);
    EXPECT_EQ(invalidCount.failedObligation, invariant::ObligationKind::Definedness);

    const auto negative     = integer(factory, -1);
    const auto negativeLeft = verifyUpdate(
        signedX, negative, signedX.binary(symbolic::BinaryOp::ShiftLeft, count), signedOne);
    EXPECT_EQ(negativeLeft.status, invariant::VerificationStatus::Refuted);
    EXPECT_EQ(negativeLeft.failedObligation, invariant::ObligationKind::Definedness);

    const auto large       = integer(factory, 1 << 30);
    const auto overflowing = verifyUpdate(
        signedX, large, signedX.binary(symbolic::BinaryOp::ShiftLeft, count), signedOne);
    EXPECT_EQ(overflowing.status, invariant::VerificationStatus::Refuted);
    EXPECT_EQ(overflowing.failedObligation, invariant::ObligationKind::Definedness);
}

TEST_F(ClauseCombinerPrototypeTest, ModelsDefinedSignedLeftShift) {
    const auto type   = symbolic::ExprType{symbolic::ExprScalarKind::Int, 32};
    const auto x      = variable(0, type);
    const auto count  = variable(1, type);
    auto &factory     = x.factory();
    const auto three  = integer(factory, 3);
    const auto two    = integer(factory, 2);
    const auto twelve = integer(factory, 12);
    invariant::TransitionModel model{
        {{x, three}, {count, two}},
        {},
        x.equalTo(three).logicalAnd(count.equalTo(two)),
        x.equalTo(three),
        {{boolean(factory, true), {x.binary(symbolic::BinaryOp::ShiftLeft, count), count}}},
        std::nullopt,
        invariant::TransitionIntegerSemantics::CMachine};
    const auto candidate =
        x.equalTo(three).logicalOr(x.equalTo(twelve)).logicalAnd(count.equalTo(two));

    const auto result = invariant::verifyInvariant(model, candidate);

    EXPECT_EQ(result.status, invariant::VerificationStatus::Proved) << result.reason;
}

TEST_F(ClauseCombinerPrototypeTest, RejectsImplementationDefinedSignedRightShift) {
    const auto type = symbolic::ExprType{symbolic::ExprScalarKind::Int, 32};
    const auto x    = variable(0, type);
    auto &factory   = x.factory();
    const auto four = integer(factory, 4);
    const auto one  = integer(factory, 1);
    invariant::TransitionModel model{
        {{x, four}},
        {},
        x.equalTo(four),
        boolean(factory, true),
        {{boolean(factory, true), {x.binary(symbolic::BinaryOp::ShiftRight, one)}}},
        std::nullopt,
        invariant::TransitionIntegerSemantics::CMachine};

    const auto result = invariant::verifyInvariant(model, boolean(factory, true));

    EXPECT_EQ(result.status, invariant::VerificationStatus::Unsupported);
    EXPECT_NE(result.reason.find("implementation-defined"), std::string::npos);
}

TEST_F(ClauseCombinerPrototypeTest, ModelsExactBitwiseOperations) {
    const auto unsignedType = symbolic::ExprType{symbolic::ExprScalarKind::UInt, 32};
    const auto x            = variable(0, unsignedType);
    auto &factory           = x.factory();
    const auto initial      = symbolic::LiteralExpr{factory, std::uint32_t{0xf0U}};

    auto verifyUnsignedUpdate = [&](const symbolic::Expr &next, const symbolic::Expr &expected) {
        invariant::TransitionModel model{{{x, initial}},
                                         {},
                                         x.equalTo(initial),
                                         x.equalTo(initial),
                                         {{boolean(factory, true), {next}}},
                                         std::nullopt,
                                         invariant::TransitionIntegerSemantics::CMachine};
        const auto candidate = x.equalTo(initial).logicalOr(x.equalTo(expected));
        return invariant::verifyInvariant(model, candidate);
    };

    const auto lowNibble = symbolic::LiteralExpr{factory, std::uint32_t{0x0fU}};
    const auto allBits   = symbolic::LiteralExpr{factory, std::uint32_t{0xffU}};
    const auto zero      = symbolic::LiteralExpr{factory, std::uint32_t{0}};
    const auto andResult =
        verifyUnsignedUpdate(x.binary(symbolic::BinaryOp::BitAnd, lowNibble), zero);
    EXPECT_EQ(andResult.status, invariant::VerificationStatus::Proved) << andResult.reason;
    const auto orResult =
        verifyUnsignedUpdate(x.binary(symbolic::BinaryOp::BitOr, lowNibble), allBits);
    EXPECT_EQ(orResult.status, invariant::VerificationStatus::Proved) << orResult.reason;
    const auto xorResult =
        verifyUnsignedUpdate(x.binary(symbolic::BinaryOp::BitXor, allBits), lowNibble);
    EXPECT_EQ(xorResult.status, invariant::VerificationStatus::Proved) << xorResult.reason;

    const auto signedType = symbolic::ExprType{symbolic::ExprScalarKind::Int, 32};
    const auto y          = variable(1, signedType);
    const auto signedZero = integer(factory, 0);
    const auto minusOne   = integer(factory, -1);
    invariant::TransitionModel complement{
        {{y, signedZero}},
        {},
        y.equalTo(signedZero),
        y.equalTo(signedZero),
        {{boolean(factory, true), {y.unary(symbolic::UnaryOp::BitwiseNot)}}},
        std::nullopt,
        invariant::TransitionIntegerSemantics::CMachine};
    const auto complementCandidate = y.equalTo(signedZero).logicalOr(y.equalTo(minusOne));
    const auto complementResult    = invariant::verifyInvariant(complement, complementCandidate);
    EXPECT_EQ(complementResult.status, invariant::VerificationStatus::Proved)
        << complementResult.reason;
}

TEST_F(ClauseCombinerPrototypeTest, ModelsExplicitIntegerAndBooleanCasts) {
    const auto intType  = symbolic::ExprType{symbolic::ExprScalarKind::Int, 32};
    const auto uintType = symbolic::ExprType{symbolic::ExprScalarKind::UInt, 32};
    const auto boolType = symbolic::ExprType{symbolic::ExprScalarKind::Bool, 1};
    const auto x        = variable(0, intType);
    const auto y        = variable(1, uintType);
    auto &factory       = x.factory();
    const auto minusOne = integer(factory, -1);
    const auto zero     = symbolic::LiteralExpr{factory, std::uint32_t{0}};
    const auto maximum =
        symbolic::LiteralExpr{factory, std::numeric_limits<std::uint32_t>::max()};

    invariant::TransitionModel model{
        {{x, minusOne}, {y, zero}},
        {},
        x.equalTo(minusOne).logicalAnd(y.equalTo(zero)),
        x.castTo(boolType),
        {{boolean(factory, true), {x, x.castTo(uintType)}}},
        std::nullopt,
        invariant::TransitionIntegerSemantics::CMachine};
    const auto candidate =
        x.equalTo(minusOne).logicalAnd(y.equalTo(zero).logicalOr(y.equalTo(maximum)));

    const auto result = invariant::verifyInvariant(model, candidate);

    EXPECT_EQ(result.status, invariant::VerificationStatus::Proved) << result.reason;
}

TEST_F(ClauseCombinerPrototypeTest, RejectsImplementationDefinedUnsignedToSignedCast) {
    const auto uintType = symbolic::ExprType{symbolic::ExprScalarKind::UInt, 32};
    const auto intType  = symbolic::ExprType{symbolic::ExprScalarKind::Int, 32};
    const auto x        = variable(0, uintType);
    auto &factory       = x.factory();
    const auto zero     = symbolic::LiteralExpr{factory, std::uint32_t{0}};
    invariant::TransitionModel model{
        {{x, zero}},
        {},
        x.equalTo(zero),
        x.castTo(intType).greaterEqual(integer(factory, 0)),
        {{boolean(factory, true), {x}}},
        std::nullopt,
        invariant::TransitionIntegerSemantics::CMachine};

    const auto result = invariant::verifyInvariant(model, boolean(factory, true));

    EXPECT_EQ(result.status, invariant::VerificationStatus::Unsupported);
    EXPECT_NE(result.reason.find("implementation-defined"), std::string::npos);
}

TEST_F(ClauseCombinerPrototypeTest, ModelsPromotedSignedNarrowUpdateWhenRepresentable) {
    const auto narrowType = symbolic::ExprType{symbolic::ExprScalarKind::Int, 16};
    const auto intType    = symbolic::ExprType{symbolic::ExprScalarKind::Int, 32};
    const auto x          = variable(0, narrowType);
    auto &factory         = x.factory();
    const auto zero       = symbolic::LiteralExpr{factory, short{0}};
    const auto ten        = symbolic::LiteralExpr{factory, short{10}};
    const auto promoted   = x.castTo(intType);
    const auto next       = (promoted + integer(factory, 1)).castTo(narrowType);
    invariant::TransitionModel model{
        {{x, zero}},
        {},
        x.equalTo(zero),
        promoted.lessThan(integer(factory, 10)),
        {{boolean(factory, true), {next}}},
        std::nullopt,
        invariant::TransitionIntegerSemantics::CMachine};
    const auto candidate = x.greaterEqual(zero).logicalAnd(x.lessEqual(ten));

    const auto result = invariant::verifyInvariant(model, candidate);

    EXPECT_EQ(result.status, invariant::VerificationStatus::Proved) << result.reason;
}

TEST_F(ClauseCombinerPrototypeTest, RefutesNonPortableSignedNarrowing) {
    const auto narrowType = symbolic::ExprType{symbolic::ExprScalarKind::Int, 16};
    const auto intType    = symbolic::ExprType{symbolic::ExprScalarKind::Int, 32};
    const auto x          = variable(0, narrowType);
    auto &factory         = x.factory();
    const auto maximum    = symbolic::LiteralExpr{factory, std::numeric_limits<short>::max()};
    const auto next       = (x.castTo(intType) + integer(factory, 1)).castTo(narrowType);
    invariant::TransitionModel model{
        {{x, maximum}},
        {},
        x.equalTo(maximum),
        boolean(factory, true),
        {{boolean(factory, true), {next}}},
        std::nullopt,
        invariant::TransitionIntegerSemantics::CMachine};

    const auto result = invariant::verifyInvariant(model, boolean(factory, true));

    EXPECT_EQ(result.status, invariant::VerificationStatus::Refuted);
    EXPECT_EQ(result.failedObligation, invariant::ObligationKind::Definedness);
}

TEST_F(ClauseCombinerPrototypeTest, ModelsUnsignedNarrowingAsModuloArithmetic) {
    const auto narrowType = symbolic::ExprType{symbolic::ExprScalarKind::UInt, 16};
    const auto intType    = symbolic::ExprType{symbolic::ExprScalarKind::Int, 32};
    const auto x          = variable(0, narrowType);
    auto &factory         = x.factory();
    const auto maximum =
        symbolic::LiteralExpr{factory, std::numeric_limits<unsigned short>::max()};
    const auto zero     = symbolic::LiteralExpr{factory, static_cast<unsigned short>(0)};
    const auto promoted = x.castTo(intType);
    const auto next     = (promoted + integer(factory, 1)).castTo(narrowType);
    invariant::TransitionModel model{
        {{x, maximum}},
        {},
        x.equalTo(maximum),
        promoted.equalTo(integer(factory, std::numeric_limits<unsigned short>::max())),
        {{boolean(factory, true), {next}}},
        std::nullopt,
        invariant::TransitionIntegerSemantics::CMachine};
    const auto candidate = x.equalTo(maximum).logicalOr(x.equalTo(zero));

    const auto result = invariant::verifyInvariant(model, candidate);

    EXPECT_EQ(result.status, invariant::VerificationStatus::Proved) << result.reason;
}

TEST_F(ClauseCombinerPrototypeTest, RejectsNarrowArithmeticWithoutIntegerPromotion) {
    const auto x    = variable(0, {symbolic::ExprScalarKind::Int, 16});
    auto &factory   = x.factory();
    const auto zero = symbolic::LiteralExpr{factory, short{0}};
    const auto one  = symbolic::LiteralExpr{factory, short{1}};
    invariant::TransitionModel model{{{x, zero}},
                                     {},
                                     x.equalTo(zero),
                                     boolean(factory, true),
                                     {{boolean(factory, true), {x + one}}},
                                     std::nullopt,
                                     invariant::TransitionIntegerSemantics::CMachine};

    const auto result = invariant::verifyInvariant(model, boolean(factory, true));

    EXPECT_EQ(result.status, invariant::VerificationStatus::Unsupported);
    EXPECT_NE(result.reason.find("promotion"), std::string::npos);
}
#endif

TEST_F(ClauseCombinerPrototypeTest, SolverAvailabilityMatchesBuildConfiguration) {
#if ACSLG_NUMERICAL_INVARIANT_HAS_Z3
    GTEST_SKIP() << "Z3-backed tests already exercised the available solver";
#else
    const auto x  = variable(0);
    auto &factory = x.factory();
    invariant::TransitionModel model{{{x, integer(factory, 0)}},      {},
                                     boolean(factory, true),          boolean(factory, true),
                                     {{boolean(factory, true), {x}}}, std::nullopt};

    EXPECT_EQ(invariant::verifyInvariant(model, boolean(factory, true)).status,
              invariant::VerificationStatus::SolverUnavailable);
#endif
}
