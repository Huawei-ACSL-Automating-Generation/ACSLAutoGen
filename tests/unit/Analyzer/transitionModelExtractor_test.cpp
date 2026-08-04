#include "Analyzer/NumericalInvariant/clauseCombiner.h"
#include "Analyzer/NumericalInvariant/clause2InvPrototype.h"
#include "Analyzer/NumericalInvariant/transitionModelExtractor.h"
#include "SpecGenerator/specGenerator.h"
#include "testHelper.h"

#include <gtest/gtest.h>
#include <llvm/Support/Casting.h>

#include <functional>
#include <limits>
#include <memory>
#include <string_view>

namespace {
    namespace analyzer  = acslg::analyzer;
    namespace invariant = acslg::analyzer::invariant;
    namespace symbolic  = acslg::analyzer::symbolic;
    namespace spec      = acslg::spec_generator;

    class ModelAwareProvider final : public invariant::ClauseProvider {
      public:
        invariant::ClauseProviderResult generate(
            const invariant::ClauseProviderRequest &request) override {
            ++calls;
            symbolic::Expr zeroState = request.model.variables.front().current;
            symbolic::Expr other     = request.model.variables.front().current;
            bool foundZeroState      = false;
            for (const auto &variable : request.model.variables) {
                const auto literal = symbolic::LiteralExpr::tryFrom(variable.entry);
                if (literal && literal->value() == 0) {
                    zeroState      = variable.current;
                    foundZeroState = true;
                } else {
                    other = variable.current;
                }
            }
            if (!foundZeroState)
                return {invariant::ClauseProviderStatus::UnsupportedModel,
                        {},
                        "test model has no zero-initialized state"};
            auto &factory = zeroState.factory();
            return {
                invariant::ClauseProviderStatus::Success,
                {zeroState.lessEqual(other), zeroState.equalTo(symbolic::LiteralExpr{factory, 0})},
                {}};
        }

        std::size_t calls = 0;
    };

    class TransitionModelExtractorTest : public ::testing::Test {
      protected:
        using Callback = std::function<
            void(const analyzer::ProgramState &, const spec::LoopInfo &, const symbolic::Expr &)>;

        void withFirstLoop(std::string_view code, const Callback &callback) {
            acslg::test::utils::ASTExtractor extractor{code};
            acslg::context::ACSLGContext context{extractor.getASTContext()};
            symbolic::ExprFactoryScope scope{context.getExprFactory()};
            const auto *function = extractor.findFirstDecl<clang::FunctionDecl>();
            auto state           = std::make_unique<analyzer::ProgramState>(
                std::make_unique<analyzer::ACSLFunction>(function), context);
            state->init();

            const clang::Stmt *loop = nullptr;
            for (const auto *statement : function->getBody()->children()) {
                if (llvm::isa<clang::WhileStmt, clang::ForStmt, clang::DoStmt>(statement)) {
                    loop = statement;
                    break;
                }
                state->step(statement);
            }
            ASSERT_NE(loop, nullptr);

            auto loopEntry = state->clone();
            if (const auto *forLoop = llvm::dyn_cast<clang::ForStmt>(loop);
                forLoop && forLoop->getInit())
                loopEntry->step(forLoop->getInit());

            spec::LoopInfo loopInfo{loop};
            const auto *registered = spec::ACSLPluginRegistry::instance().get("SetEntryAndCurrent");
            ASSERT_NE(registered, nullptr);
            const auto *entryPlugin = dynamic_cast<const spec::LoopInfoPlugin *>(registered);
            ASSERT_NE(entryPlugin, nullptr);
            ASSERT_TRUE(entryPlugin->parse(*state, *loopEntry, loopInfo));
            ASSERT_TRUE(loopInfo.entryAndCurrentInfo.has_value());
            const auto &entryAndCurrent = *loopInfo.entryAndCurrentInfo;
            ASSERT_EQ(loopEntry->getPaths().size(), 1U);
            ASSERT_EQ(entryAndCurrent.symbolicLoopEntry->getPaths().size(), 1U);

            auto conditionPath = entryAndCurrent.symbolicLoopEntry->getPaths().front()->clone();
            symbolic::Expr condition = symbolic::LiteralExpr{context.getExprFactory(), true};
            if (loopInfo.condExpr) {
                auto evaluated = conditionPath->evalExpr(loopInfo.condExpr);
                ASSERT_TRUE(evaluated.first.empty());
                ASSERT_EQ(evaluated.second.size(), 1U);
                condition = evaluated.second.front();
            }
            callback(*loopEntry, loopInfo, condition);
        }

        static invariant::TransitionExtractionLimits prototypeLimits() {
            invariant::TransitionExtractionLimits limits;
            limits.integerModel = invariant::CIntegerModel::MathematicalIntegersForPrototype;
            return limits;
        }

        static invariant::TransitionExtractionLimits exactLimits() {
            invariant::TransitionExtractionLimits limits;
            limits.integerModel = invariant::CIntegerModel::ExactMachineIntegers;
            return limits;
        }

        static std::size_t variableIndex(const invariant::ExtractedTransitionModel &extracted,
                                         std::string_view name) {
            for (std::size_t index = 0; index < extracted.variableDeclarations.size(); ++index) {
                if (extracted.variableDeclarations[index]->getNameAsString() == name)
                    return index;
            }
            return extracted.variableDeclarations.size();
        }
    };
} // namespace

TEST_F(TransitionModelExtractorTest, ExtractsScalarLoopAndBuildsProvableModel) {
    withFirstLoop(
        R"(
        int func(int n) {
            int x = 0;
            for (int i = 0; i < n; ++i)
                x += 1;
            return x;
        }
    )",
        [&](const auto &loopEntry, const auto &loopInfo, const auto &condition) {
            const auto &info  = *loopInfo.entryAndCurrentInfo;
            const auto result = invariant::extractTransitionModel(
                *loopEntry.getPaths().front(), *info.symbolicLoopEntry->getPaths().front(),
                *info.symbolicLoopCurrent, condition, info.loopEntryPoint, exactLimits());
            ASSERT_EQ(result.status, invariant::TransitionExtractionStatus::Success)
                << result.reason;
            ASSERT_TRUE(result.extracted.has_value());
            const auto &extracted = *result.extracted;
            ASSERT_EQ(extracted.model.branches.size(), 1U);
            EXPECT_EQ(extracted.model.integerSemantics,
                      invariant::TransitionIntegerSemantics::CMachine);

            const auto xIndex = variableIndex(extracted, "x");
            const auto iIndex = variableIndex(extracted, "i");
            ASSERT_LT(xIndex, extracted.model.variables.size());
            ASSERT_LT(iIndex, extracted.model.variables.size());
            auto &factory  = extracted.model.variables.front().current.factory();
            const auto one = symbolic::LiteralExpr{factory, 1};
            EXPECT_TRUE(extracted.model.branches[0].nextValues[xIndex].structurallyEqual(
                extracted.model.variables[xIndex].current + one));
            EXPECT_TRUE(extracted.model.branches[0].nextValues[iIndex].structurallyEqual(
                extracted.model.variables[iIndex].current + one));

#if ACSLG_NUMERICAL_INVARIANT_HAS_Z3
            const auto zero = symbolic::LiteralExpr{factory, 0};
            const auto candidate =
                extracted.model.variables[iIndex].current.greaterEqual(zero).logicalAnd(
                    extracted.model.variables[xIndex].current.equalTo(
                        extracted.model.variables[iIndex].current));
            const auto verified = invariant::verifyInvariant(extracted.model, candidate);
            EXPECT_EQ(verified.status, invariant::VerificationStatus::Proved) << verified.reason;
#endif
        });
}

TEST_F(TransitionModelExtractorTest, ExtractsAndProvesFixedArrayAccessBounds) {
    withFirstLoop(
        R"(
        int func(void) {
            int items[5];
            int i = 0;
            while (i < 5) {
                items[i];
                ++i;
            }
            return i;
        }
    )",
        [&](const auto &loopEntry, const auto &loopInfo, const auto &condition) {
            const auto &info = *loopInfo.entryAndCurrentInfo;
            const auto result = invariant::extractTransitionModel(
                *loopEntry.getPaths().front(), *info.symbolicLoopEntry->getPaths().front(),
                *info.symbolicLoopCurrent, condition, info.loopEntryPoint, exactLimits());
            ASSERT_EQ(result.status, invariant::TransitionExtractionStatus::Success)
                << result.reason;
            ASSERT_TRUE(result.extracted.has_value());
            const auto &model = result.extracted->model;
            ASSERT_EQ(model.branches.size(), 1U);
            ASSERT_EQ(model.branches[0].definednessConditions.size(), 1U);

#if ACSLG_NUMERICAL_INVARIANT_HAS_Z3
            const auto iIndex = variableIndex(*result.extracted, "i");
            ASSERT_LT(iIndex, model.variables.size());
            auto &factory = model.variables[iIndex].current.factory();
            const auto zero = symbolic::LiteralExpr{factory, 0};
            const auto five = symbolic::LiteralExpr{factory, 5};
            const auto candidate =
                model.variables[iIndex].current.greaterEqual(zero).logicalAnd(
                    model.variables[iIndex].current.lessEqual(five));
            const auto verified = invariant::verifyInvariant(model, candidate);
            EXPECT_EQ(verified.status, invariant::VerificationStatus::Proved)
                << verified.reason;
#endif
        });
}

TEST_F(TransitionModelExtractorTest, RejectsPointerAccessWithUnknownExtent) {
    withFirstLoop(
        R"(
        int func(int *items, int n) {
            int i = 0;
            while (i < n) {
                items[i];
                ++i;
            }
            return i;
        }
    )",
        [&](const auto &loopEntry, const auto &loopInfo, const auto &condition) {
            const auto &info = *loopInfo.entryAndCurrentInfo;
            const auto result = invariant::extractTransitionModel(
                *loopEntry.getPaths().front(), *info.symbolicLoopEntry->getPaths().front(),
                *info.symbolicLoopCurrent, condition, info.loopEntryPoint, exactLimits());
            EXPECT_EQ(result.status, invariant::TransitionExtractionStatus::Unsupported);
            EXPECT_NE(result.reason.find("unknown extent"), std::string::npos);
        });
}

TEST_F(TransitionModelExtractorTest, AllowsOnePastPointerFormation) {
    withFirstLoop(
        R"(
        int func(void) {
            int items[5];
            int i = 0;
            while (i < 6) {
                int *pointer = items + i;
                ++i;
            }
            return i;
        }
    )",
        [&](const auto &loopEntry, const auto &loopInfo, const auto &condition) {
            const auto &info = *loopInfo.entryAndCurrentInfo;
            const auto result = invariant::extractTransitionModel(
                *loopEntry.getPaths().front(), *info.symbolicLoopEntry->getPaths().front(),
                *info.symbolicLoopCurrent, condition, info.loopEntryPoint, exactLimits());
            ASSERT_EQ(result.status, invariant::TransitionExtractionStatus::Success)
                << result.reason;
            ASSERT_TRUE(result.extracted.has_value());
            const auto &model = result.extracted->model;
            ASSERT_EQ(model.branches.size(), 1U);
            ASSERT_EQ(model.branches[0].definednessConditions.size(), 1U);

#if ACSLG_NUMERICAL_INVARIANT_HAS_Z3
            const auto iIndex = variableIndex(*result.extracted, "i");
            ASSERT_LT(iIndex, model.variables.size());
            auto &factory = model.variables[iIndex].current.factory();
            const auto zero = symbolic::LiteralExpr{factory, 0};
            const auto six = symbolic::LiteralExpr{factory, 6};
            const auto candidate =
                model.variables[iIndex].current.greaterEqual(zero).logicalAnd(
                    model.variables[iIndex].current.lessEqual(six));
            const auto verified = invariant::verifyInvariant(model, candidate);
            EXPECT_EQ(verified.status, invariant::VerificationStatus::Proved)
                << verified.reason;
#endif
        });
}

TEST_F(TransitionModelExtractorTest, RefutesPointerFormationBeyondOnePast) {
    withFirstLoop(
        R"(
        int func(void) {
            int items[5];
            int i = 0;
            while (i < 7) {
                int *pointer = items + i;
                ++i;
            }
            return i;
        }
    )",
        [&](const auto &loopEntry, const auto &loopInfo, const auto &condition) {
            const auto &info = *loopInfo.entryAndCurrentInfo;
            const auto result = invariant::extractTransitionModel(
                *loopEntry.getPaths().front(), *info.symbolicLoopEntry->getPaths().front(),
                *info.symbolicLoopCurrent, condition, info.loopEntryPoint, exactLimits());
            ASSERT_EQ(result.status, invariant::TransitionExtractionStatus::Success)
                << result.reason;
            ASSERT_TRUE(result.extracted.has_value());
#if ACSLG_NUMERICAL_INVARIANT_HAS_Z3
            const auto &model = result.extracted->model;
            const auto iIndex = variableIndex(*result.extracted, "i");
            ASSERT_LT(iIndex, model.variables.size());
            auto &factory = model.variables[iIndex].current.factory();
            const auto zero = symbolic::LiteralExpr{factory, 0};
            const auto seven = symbolic::LiteralExpr{factory, 7};
            const auto candidate =
                model.variables[iIndex].current.greaterEqual(zero).logicalAnd(
                    model.variables[iIndex].current.lessEqual(seven));
            const auto verified = invariant::verifyInvariant(model, candidate);
            EXPECT_EQ(verified.status, invariant::VerificationStatus::Refuted);
            EXPECT_EQ(verified.failedObligation, invariant::ObligationKind::Definedness);
#endif
        });
}

TEST_F(TransitionModelExtractorTest, PreservesUnsignedTypesThroughIncrementAndProof) {
    withFirstLoop(
        R"(
        unsigned func(unsigned limit) {
            unsigned x = 0;
            while (x < limit)
                ++x;
            return x;
        }
    )",
        [&](const auto &loopEntry, const auto &loopInfo, const auto &condition) {
            EXPECT_EQ(condition.getValType(),
                      (symbolic::ExprType{symbolic::ExprScalarKind::Int, 32}));

            const auto &info  = *loopInfo.entryAndCurrentInfo;
            const auto result = invariant::extractTransitionModel(
                *loopEntry.getPaths().front(), *info.symbolicLoopEntry->getPaths().front(),
                *info.symbolicLoopCurrent, condition, info.loopEntryPoint, exactLimits());
            ASSERT_EQ(result.status, invariant::TransitionExtractionStatus::Success)
                << result.reason;
            ASSERT_TRUE(result.extracted.has_value());

            const auto &extracted = *result.extracted;
            const auto xIndex     = variableIndex(extracted, "x");
            const auto limitIndex = variableIndex(extracted, "limit");
            ASSERT_LT(xIndex, extracted.model.variables.size());
            ASSERT_LT(limitIndex, extracted.model.variables.size());
            const auto unsignedType =
                symbolic::ExprType{symbolic::ExprScalarKind::UInt, 32};
            EXPECT_EQ(extracted.model.variables[xIndex].current.getValType(), unsignedType);
            EXPECT_EQ(extracted.model.branches[0].nextValues[xIndex].getValType(), unsignedType);

            const auto update =
                symbolic::BinaryExpr::tryFrom(extracted.model.branches[0].nextValues[xIndex]);
            ASSERT_TRUE(update.has_value());
            EXPECT_EQ(update->operation(), symbolic::BinaryOp::Add);
            EXPECT_EQ(update->right().getValType(), unsignedType);

#if ACSLG_NUMERICAL_INVARIANT_HAS_Z3
            const auto candidate = extracted.model.variables[xIndex].current.lessEqual(
                extracted.model.variables[limitIndex].current);
            const auto verified = invariant::verifyInvariant(extracted.model, candidate);
            EXPECT_EQ(verified.status, invariant::VerificationStatus::Proved) << verified.reason;
#endif
        });
}

TEST_F(TransitionModelExtractorTest, PreservesUnsignedCompoundAssignmentComputationType) {
    withFirstLoop(
        R"(
        unsigned func(unsigned limit) {
            unsigned x = 0;
            while (x < limit)
                x += 1;
            return x;
        }
    )",
        [&](const auto &loopEntry, const auto &loopInfo, const auto &condition) {
            const auto &info  = *loopInfo.entryAndCurrentInfo;
            const auto result = invariant::extractTransitionModel(
                *loopEntry.getPaths().front(), *info.symbolicLoopEntry->getPaths().front(),
                *info.symbolicLoopCurrent, condition, info.loopEntryPoint, exactLimits());
            ASSERT_EQ(result.status, invariant::TransitionExtractionStatus::Success)
                << result.reason;
            ASSERT_TRUE(result.extracted.has_value());

            const auto &extracted = *result.extracted;
            const auto xIndex     = variableIndex(extracted, "x");
            ASSERT_LT(xIndex, extracted.model.variables.size());
            const auto update =
                symbolic::BinaryExpr::tryFrom(extracted.model.branches[0].nextValues[xIndex]);
            ASSERT_TRUE(update.has_value());
            EXPECT_EQ(update->operation(), symbolic::BinaryOp::Add);
            EXPECT_EQ(update->right().getValType(),
                      (symbolic::ExprType{symbolic::ExprScalarKind::UInt, 32}));
        });
}

TEST_F(TransitionModelExtractorTest, PreservesIfElseAsGuardedBranches) {
    withFirstLoop(R"(
        int func(int n, int choose) {
            int i = 0;
            int x = 0;
            while (i < n) {
                if (choose)
                    x += 1;
                else
                    x += 2;
                i += 1;
            }
            return x;
        }
    )",
                  [&](const auto &loopEntry, const auto &loopInfo, const auto &condition) {
                      const auto &info  = *loopInfo.entryAndCurrentInfo;
                      const auto result = invariant::extractTransitionModel(
                          *loopEntry.getPaths().front(),
                          *info.symbolicLoopEntry->getPaths().front(), *info.symbolicLoopCurrent,
                          condition, info.loopEntryPoint, prototypeLimits());
                      ASSERT_EQ(result.status, invariant::TransitionExtractionStatus::Success)
                          << result.reason;
                      ASSERT_TRUE(result.extracted.has_value());
                      EXPECT_EQ(result.extracted->model.branches.size(), 2U);
                      for (const auto &branch : result.extracted->model.branches)
                          EXPECT_FALSE(branch.guard.tryEvalAsConstant().has_value());
                  });
}

TEST_F(TransitionModelExtractorTest, RejectsMachineIntegerApproximationByDefault) {
    withFirstLoop(R"(
        int func(int n) {
            int x = 0;
            while (x < n)
                ++x;
            return x;
        }
    )",
                  [&](const auto &loopEntry, const auto &loopInfo, const auto &condition) {
                      const auto &info  = *loopInfo.entryAndCurrentInfo;
                      const auto result = invariant::extractTransitionModel(
                          *loopEntry.getPaths().front(),
                          *info.symbolicLoopEntry->getPaths().front(), *info.symbolicLoopCurrent,
                          condition, info.loopEntryPoint);
                      EXPECT_EQ(result.status, invariant::TransitionExtractionStatus::Unsupported);
                  });
}

TEST_F(TransitionModelExtractorTest, GatesDivisionToExactMachineSemantics) {
    withFirstLoop(
        R"(
        int func(int n) {
            int x = n;
            while (x > 1)
                x = x / 2;
            return x;
        }
    )",
        [&](const auto &loopEntry, const auto &loopInfo, const auto &condition) {
            const auto &info     = *loopInfo.entryAndCurrentInfo;
            const auto prototype = invariant::extractTransitionModel(
                *loopEntry.getPaths().front(), *info.symbolicLoopEntry->getPaths().front(),
                *info.symbolicLoopCurrent, condition, info.loopEntryPoint, prototypeLimits());
            EXPECT_EQ(prototype.status, invariant::TransitionExtractionStatus::Unsupported);

            const auto exact = invariant::extractTransitionModel(
                *loopEntry.getPaths().front(), *info.symbolicLoopEntry->getPaths().front(),
                *info.symbolicLoopCurrent, condition, info.loopEntryPoint, exactLimits());
            ASSERT_EQ(exact.status, invariant::TransitionExtractionStatus::Success) << exact.reason;
#if ACSLG_NUMERICAL_INVARIANT_HAS_Z3
            const auto verified = invariant::verifyInvariant(
                exact.extracted->model, symbolic::LiteralExpr{condition.factory(), true});
            EXPECT_EQ(verified.status, invariant::VerificationStatus::Proved) << verified.reason;
#endif
        });
}

TEST_F(TransitionModelExtractorTest, PreservesExplicitCastsForExactMachineVerification) {
    withFirstLoop(
        R"(
        unsigned func(int x) {
            unsigned y = 0;
            while ((_Bool)x) {
                y = (unsigned)x;
                x = 0;
            }
            return y;
        }
    )",
        [&](const auto &loopEntry, const auto &loopInfo, const auto &condition) {
            const auto &info     = *loopInfo.entryAndCurrentInfo;
            const auto prototype = invariant::extractTransitionModel(
                *loopEntry.getPaths().front(), *info.symbolicLoopEntry->getPaths().front(),
                *info.symbolicLoopCurrent, condition, info.loopEntryPoint, prototypeLimits());
            EXPECT_EQ(prototype.status, invariant::TransitionExtractionStatus::Unsupported);

            const auto result = invariant::extractTransitionModel(
                *loopEntry.getPaths().front(), *info.symbolicLoopEntry->getPaths().front(),
                *info.symbolicLoopCurrent, condition, info.loopEntryPoint, exactLimits());
            ASSERT_EQ(result.status, invariant::TransitionExtractionStatus::Success)
                << result.reason;
            ASSERT_TRUE(result.extracted.has_value());

            const auto &extracted = *result.extracted;
            const auto xIndex     = variableIndex(extracted, "x");
            const auto yIndex     = variableIndex(extracted, "y");
            ASSERT_LT(xIndex, extracted.model.variables.size());
            ASSERT_LT(yIndex, extracted.model.variables.size());

            const auto conditionCast = symbolic::CastExpr::tryFrom(extracted.model.loopCondition);
            ASSERT_TRUE(conditionCast.has_value());
            EXPECT_EQ(conditionCast->targetType(),
                      (symbolic::ExprType{symbolic::ExprScalarKind::Bool, 1}));
            const auto updateCast =
                symbolic::CastExpr::tryFrom(extracted.model.branches[0].nextValues[yIndex]);
            ASSERT_TRUE(updateCast.has_value());
            EXPECT_EQ(updateCast->targetType(),
                      (symbolic::ExprType{symbolic::ExprScalarKind::UInt, 32}));

#if ACSLG_NUMERICAL_INVARIANT_HAS_Z3
            auto &factory   = extracted.model.variables.front().current.factory();
            const auto zero = symbolic::LiteralExpr{factory, 0};
            const auto unsignedZero = symbolic::LiteralExpr{factory, std::uint32_t{0}};
            const auto candidate =
                extracted.model.variables[yIndex].current.equalTo(unsignedZero).logicalOr(
                    extracted.model.variables[xIndex].current.equalTo(zero));
            const auto verified = invariant::verifyInvariant(extracted.model, candidate);
            EXPECT_EQ(verified.status, invariant::VerificationStatus::Proved) << verified.reason;
#endif
        });
}

TEST_F(TransitionModelExtractorTest, PreservesUsualArithmeticConversionInLoopUpdate) {
    withFirstLoop(
        R"(
        unsigned func(int delta) {
            unsigned x = 0;
            while (x == 0)
                x = x + delta;
            return x;
        }
    )",
        [&](const auto &loopEntry, const auto &loopInfo, const auto &condition) {
            const auto &info     = *loopInfo.entryAndCurrentInfo;
            const auto prototype = invariant::extractTransitionModel(
                *loopEntry.getPaths().front(), *info.symbolicLoopEntry->getPaths().front(),
                *info.symbolicLoopCurrent, condition, info.loopEntryPoint, prototypeLimits());
            EXPECT_EQ(prototype.status, invariant::TransitionExtractionStatus::Unsupported);

            const auto result = invariant::extractTransitionModel(
                *loopEntry.getPaths().front(), *info.symbolicLoopEntry->getPaths().front(),
                *info.symbolicLoopCurrent, condition, info.loopEntryPoint, exactLimits());
            ASSERT_EQ(result.status, invariant::TransitionExtractionStatus::Success)
                << result.reason;
            ASSERT_TRUE(result.extracted.has_value());

            const auto &extracted = *result.extracted;
            const auto xIndex     = variableIndex(extracted, "x");
            ASSERT_LT(xIndex, extracted.model.variables.size());
            const auto update =
                symbolic::BinaryExpr::tryFrom(extracted.model.branches[0].nextValues[xIndex]);
            ASSERT_TRUE(update.has_value());
            const auto conversion = symbolic::CastExpr::tryFrom(update->right());
            ASSERT_TRUE(conversion.has_value());
            EXPECT_EQ(conversion->targetType(),
                      (symbolic::ExprType{symbolic::ExprScalarKind::UInt, 32}));
            EXPECT_EQ(conversion->operand().getValType(),
                      (symbolic::ExprType{symbolic::ExprScalarKind::Int, 32}));

#if ACSLG_NUMERICAL_INVARIANT_HAS_Z3
            const auto verified = invariant::verifyInvariant(
                extracted.model, symbolic::LiteralExpr{condition.factory(), true});
            EXPECT_EQ(verified.status, invariant::VerificationStatus::Proved) << verified.reason;
#endif
        });
}

TEST_F(TransitionModelExtractorTest, PreservesInt32ToUInt64ConversionInLoopUpdate) {
    withFirstLoop(
        R"(
        unsigned long long func(void) {
            unsigned long long x = 0;
            int delta = -1;
            while (x == 0)
                x = x + delta;
            return x;
        }
    )",
        [&](const auto &loopEntry, const auto &loopInfo, const auto &condition) {
            const auto &info  = *loopInfo.entryAndCurrentInfo;
            const auto result = invariant::extractTransitionModel(
                *loopEntry.getPaths().front(), *info.symbolicLoopEntry->getPaths().front(),
                *info.symbolicLoopCurrent, condition, info.loopEntryPoint, exactLimits());
            ASSERT_EQ(result.status, invariant::TransitionExtractionStatus::Success)
                << result.reason;
            ASSERT_TRUE(result.extracted.has_value());

            const auto &extracted = *result.extracted;
            const auto xIndex     = variableIndex(extracted, "x");
            const auto deltaIndex = variableIndex(extracted, "delta");
            ASSERT_LT(xIndex, extracted.model.variables.size());
            ASSERT_LT(deltaIndex, extracted.model.variables.size());
            const auto update =
                symbolic::BinaryExpr::tryFrom(extracted.model.branches[0].nextValues[xIndex]);
            ASSERT_TRUE(update.has_value());
            const auto conversion = symbolic::CastExpr::tryFrom(update->right());
            ASSERT_TRUE(conversion.has_value());
            EXPECT_EQ(conversion->operand(),
                      extracted.model.variables[deltaIndex].current);
            EXPECT_EQ(conversion->targetType(),
                      (symbolic::ExprType{symbolic::ExprScalarKind::UInt, 64}));

#if ACSLG_NUMERICAL_INVARIANT_HAS_Z3
            auto &factory = extracted.model.variables[xIndex].current.factory();
            const auto zero = symbolic::LiteralExpr{factory, std::uint64_t{0}};
            const auto maximum =
                symbolic::LiteralExpr{factory, std::numeric_limits<std::uint64_t>::max()};
            const auto minusOne = symbolic::LiteralExpr{factory, -1};
            const auto candidate =
                extracted.model.variables[xIndex]
                    .current.equalTo(zero)
                    .logicalOr(extracted.model.variables[xIndex].current.equalTo(maximum))
                    .logicalAnd(
                        extracted.model.variables[deltaIndex].current.equalTo(minusOne));
            const auto verified = invariant::verifyInvariant(extracted.model, candidate);
            EXPECT_EQ(verified.status, invariant::VerificationStatus::Proved) << verified.reason;
#endif
        });
}

TEST_F(TransitionModelExtractorTest, ExtractsPromotedNarrowIntegerAssignment) {
    withFirstLoop(
        R"(
        short func(void) {
            short x = 0;
            while (x < 10)
                x = x + 1;
            return x;
        }
    )",
        [&](const auto &loopEntry, const auto &loopInfo, const auto &condition) {
            const auto &info     = *loopInfo.entryAndCurrentInfo;
            const auto prototype = invariant::extractTransitionModel(
                *loopEntry.getPaths().front(), *info.symbolicLoopEntry->getPaths().front(),
                *info.symbolicLoopCurrent, condition, info.loopEntryPoint, prototypeLimits());
            EXPECT_EQ(prototype.status, invariant::TransitionExtractionStatus::Unsupported);

            const auto result = invariant::extractTransitionModel(
                *loopEntry.getPaths().front(), *info.symbolicLoopEntry->getPaths().front(),
                *info.symbolicLoopCurrent, condition, info.loopEntryPoint, exactLimits());
            ASSERT_EQ(result.status, invariant::TransitionExtractionStatus::Success)
                << result.reason;
            ASSERT_TRUE(result.extracted.has_value());

            const auto &extracted = *result.extracted;
            const auto xIndex     = variableIndex(extracted, "x");
            ASSERT_LT(xIndex, extracted.model.variables.size());
            EXPECT_EQ(extracted.model.variables[xIndex].current.getValType(),
                      (symbolic::ExprType{symbolic::ExprScalarKind::Int, 16}));

            const auto assignmentCast =
                symbolic::CastExpr::tryFrom(extracted.model.branches[0].nextValues[xIndex]);
            ASSERT_TRUE(assignmentCast.has_value());
            EXPECT_EQ(assignmentCast->targetType(),
                      (symbolic::ExprType{symbolic::ExprScalarKind::Int, 16}));
            const auto addition = symbolic::BinaryExpr::tryFrom(assignmentCast->operand());
            ASSERT_TRUE(addition.has_value());
            const auto promotion = symbolic::CastExpr::tryFrom(addition->left());
            ASSERT_TRUE(promotion.has_value());
            EXPECT_EQ(promotion->targetType(),
                      (symbolic::ExprType{symbolic::ExprScalarKind::Int, 32}));

#if ACSLG_NUMERICAL_INVARIANT_HAS_Z3
            auto &factory   = extracted.model.variables[xIndex].current.factory();
            const auto zero = symbolic::LiteralExpr{factory, short{0}};
            const auto ten  = symbolic::LiteralExpr{factory, short{10}};
            const auto candidate =
                extracted.model.variables[xIndex].current.greaterEqual(zero).logicalAnd(
                    extracted.model.variables[xIndex].current.lessEqual(ten));
            const auto verified = invariant::verifyInvariant(extracted.model, candidate);
            EXPECT_EQ(verified.status, invariant::VerificationStatus::Proved) << verified.reason;
#endif
        });
}

TEST_F(TransitionModelExtractorTest, ExtractsPromotedNarrowPreIncrement) {
    withFirstLoop(
        R"(
        short func(void) {
            short x = 0;
            while (x < 10)
                ++x;
            return x;
        }
    )",
        [&](const auto &loopEntry, const auto &loopInfo, const auto &condition) {
            const auto &info  = *loopInfo.entryAndCurrentInfo;
            const auto result = invariant::extractTransitionModel(
                *loopEntry.getPaths().front(), *info.symbolicLoopEntry->getPaths().front(),
                *info.symbolicLoopCurrent, condition, info.loopEntryPoint, exactLimits());
            ASSERT_EQ(result.status, invariant::TransitionExtractionStatus::Success)
                << result.reason;
            ASSERT_TRUE(result.extracted.has_value());

            const auto &extracted = *result.extracted;
            const auto xIndex     = variableIndex(extracted, "x");
            ASSERT_LT(xIndex, extracted.model.variables.size());
            const auto writeBack =
                symbolic::CastExpr::tryFrom(extracted.model.branches[0].nextValues[xIndex]);
            ASSERT_TRUE(writeBack.has_value());
            EXPECT_EQ(writeBack->targetType(),
                      (symbolic::ExprType{symbolic::ExprScalarKind::Int, 16}));
            const auto addition = symbolic::BinaryExpr::tryFrom(writeBack->operand());
            ASSERT_TRUE(addition.has_value());
            const auto promotion = symbolic::CastExpr::tryFrom(addition->left());
            ASSERT_TRUE(promotion.has_value());
            EXPECT_EQ(promotion->targetType(),
                      (symbolic::ExprType{symbolic::ExprScalarKind::Int, 32}));

#if ACSLG_NUMERICAL_INVARIANT_HAS_Z3
            auto &factory   = extracted.model.variables[xIndex].current.factory();
            const auto zero = symbolic::LiteralExpr{factory, short{0}};
            const auto ten  = symbolic::LiteralExpr{factory, short{10}};
            const auto candidate =
                extracted.model.variables[xIndex].current.greaterEqual(zero).logicalAnd(
                    extracted.model.variables[xIndex].current.lessEqual(ten));
            const auto verified = invariant::verifyInvariant(extracted.model, candidate);
            EXPECT_EQ(verified.status, invariant::VerificationStatus::Proved) << verified.reason;
#endif
        });
}

TEST_F(TransitionModelExtractorTest, RefutesOverflowingSignedNarrowPreIncrement) {
    withFirstLoop(
        R"(
        short func(void) {
            short x = 32767;
            while (x == 32767)
                ++x;
            return x;
        }
    )",
        [&](const auto &loopEntry, const auto &loopInfo, const auto &condition) {
            const auto &info  = *loopInfo.entryAndCurrentInfo;
            const auto result = invariant::extractTransitionModel(
                *loopEntry.getPaths().front(), *info.symbolicLoopEntry->getPaths().front(),
                *info.symbolicLoopCurrent, condition, info.loopEntryPoint, exactLimits());
            ASSERT_EQ(result.status, invariant::TransitionExtractionStatus::Success)
                << result.reason;
            ASSERT_TRUE(result.extracted.has_value());

#if ACSLG_NUMERICAL_INVARIANT_HAS_Z3
            const auto &model = result.extracted->model;
            const auto verified =
                invariant::verifyInvariant(model, symbolic::LiteralExpr{condition.factory(), true});
            EXPECT_EQ(verified.status, invariant::VerificationStatus::Refuted);
            EXPECT_EQ(verified.failedObligation, invariant::ObligationKind::Definedness);
            EXPECT_EQ(verified.failedBranch, 0U);
#endif
        });
}

TEST_F(TransitionModelExtractorTest, ExtractsUnsignedNarrowModuloAssignment) {
    withFirstLoop(
        R"(
        unsigned short func(void) {
            unsigned short x = 65535;
            while (x == 65535)
                x = x + 1;
            return x;
        }
    )",
        [&](const auto &loopEntry, const auto &loopInfo, const auto &condition) {
            const auto &info  = *loopInfo.entryAndCurrentInfo;
            const auto result = invariant::extractTransitionModel(
                *loopEntry.getPaths().front(), *info.symbolicLoopEntry->getPaths().front(),
                *info.symbolicLoopCurrent, condition, info.loopEntryPoint, exactLimits());
            ASSERT_EQ(result.status, invariant::TransitionExtractionStatus::Success)
                << result.reason;
            ASSERT_TRUE(result.extracted.has_value());

            const auto &extracted = *result.extracted;
            const auto xIndex     = variableIndex(extracted, "x");
            ASSERT_LT(xIndex, extracted.model.variables.size());
            const auto expectedEntry = symbolic::LiteralExpr{
                condition.factory(), std::numeric_limits<unsigned short>::max()};
            EXPECT_EQ(extracted.model.variables[xIndex].entry, expectedEntry);
            const auto assignmentCast =
                symbolic::CastExpr::tryFrom(extracted.model.branches[0].nextValues[xIndex]);
            ASSERT_TRUE(assignmentCast.has_value());
            EXPECT_EQ(assignmentCast->targetType(),
                      (symbolic::ExprType{symbolic::ExprScalarKind::UInt, 16}));

#if ACSLG_NUMERICAL_INVARIANT_HAS_Z3
            auto &factory = extracted.model.variables[xIndex].current.factory();
            const auto maximum =
                symbolic::LiteralExpr{factory, std::numeric_limits<unsigned short>::max()};
            const auto zero =
                symbolic::LiteralExpr{factory, static_cast<unsigned short>(0)};
            const auto candidate =
                extracted.model.variables[xIndex].current.equalTo(maximum).logicalOr(
                    extracted.model.variables[xIndex].current.equalTo(zero));
            const auto verified = invariant::verifyInvariant(extracted.model, candidate);
            EXPECT_EQ(verified.status, invariant::VerificationStatus::Proved) << verified.reason;
#endif
        });
}

TEST_F(TransitionModelExtractorTest, ExtractsUnsignedNarrowCompoundModuloAssignment) {
    withFirstLoop(
        R"(
        unsigned short func(void) {
            unsigned short x = 65535;
            while (x == 65535)
                x += 1;
            return x;
        }
    )",
        [&](const auto &loopEntry, const auto &loopInfo, const auto &condition) {
            const auto &info  = *loopInfo.entryAndCurrentInfo;
            const auto result = invariant::extractTransitionModel(
                *loopEntry.getPaths().front(), *info.symbolicLoopEntry->getPaths().front(),
                *info.symbolicLoopCurrent, condition, info.loopEntryPoint, exactLimits());
            ASSERT_EQ(result.status, invariant::TransitionExtractionStatus::Success)
                << result.reason;
            ASSERT_TRUE(result.extracted.has_value());

            const auto &extracted = *result.extracted;
            const auto xIndex     = variableIndex(extracted, "x");
            ASSERT_LT(xIndex, extracted.model.variables.size());
            const auto writeBack =
                symbolic::CastExpr::tryFrom(extracted.model.branches[0].nextValues[xIndex]);
            ASSERT_TRUE(writeBack.has_value());
            EXPECT_EQ(writeBack->targetType(),
                      (symbolic::ExprType{symbolic::ExprScalarKind::UInt, 16}));
            const auto addition = symbolic::BinaryExpr::tryFrom(writeBack->operand());
            ASSERT_TRUE(addition.has_value());
            const auto promotion = symbolic::CastExpr::tryFrom(addition->left());
            ASSERT_TRUE(promotion.has_value());
            EXPECT_EQ(promotion->targetType(),
                      (symbolic::ExprType{symbolic::ExprScalarKind::Int, 32}));

#if ACSLG_NUMERICAL_INVARIANT_HAS_Z3
            auto &factory = extracted.model.variables[xIndex].current.factory();
            const auto maximum =
                symbolic::LiteralExpr{factory, std::numeric_limits<unsigned short>::max()};
            const auto zero =
                symbolic::LiteralExpr{factory, static_cast<unsigned short>(0)};
            const auto candidate =
                extracted.model.variables[xIndex].current.equalTo(maximum).logicalOr(
                    extracted.model.variables[xIndex].current.equalTo(zero));
            const auto verified = invariant::verifyInvariant(extracted.model, candidate);
            EXPECT_EQ(verified.status, invariant::VerificationStatus::Proved) << verified.reason;
#endif
        });
}

TEST_F(TransitionModelExtractorTest, ExtractsUInt64CompoundModuloAssignment) {
    withFirstLoop(
        R"(
        unsigned long long func(void) {
            unsigned long long x = 18446744073709551615ULL;
            while (x == 18446744073709551615ULL)
                x += 1ULL;
            return x;
        }
    )",
        [&](const auto &loopEntry, const auto &loopInfo, const auto &condition) {
            const auto &info  = *loopInfo.entryAndCurrentInfo;
            const auto result = invariant::extractTransitionModel(
                *loopEntry.getPaths().front(), *info.symbolicLoopEntry->getPaths().front(),
                *info.symbolicLoopCurrent, condition, info.loopEntryPoint, exactLimits());
            ASSERT_EQ(result.status, invariant::TransitionExtractionStatus::Success)
                << result.reason;
            ASSERT_TRUE(result.extracted.has_value());

            const auto &extracted = *result.extracted;
            const auto xIndex     = variableIndex(extracted, "x");
            ASSERT_LT(xIndex, extracted.model.variables.size());
            const auto uint64Type =
                symbolic::ExprType{symbolic::ExprScalarKind::UInt, 64};
            EXPECT_EQ(extracted.model.variables[xIndex].current.getValType(), uint64Type);
            const auto update =
                symbolic::BinaryExpr::tryFrom(extracted.model.branches[0].nextValues[xIndex]);
            ASSERT_TRUE(update.has_value());
            EXPECT_EQ(update->operation(), symbolic::BinaryOp::Add);
            EXPECT_EQ(update->left().getValType(), uint64Type);
            EXPECT_EQ(update->right().getValType(), uint64Type);

#if ACSLG_NUMERICAL_INVARIANT_HAS_Z3
            auto &factory = extracted.model.variables[xIndex].current.factory();
            const auto maximum =
                symbolic::LiteralExpr{factory, std::numeric_limits<std::uint64_t>::max()};
            const auto zero = symbolic::LiteralExpr{factory, std::uint64_t{0}};
            const auto candidate =
                extracted.model.variables[xIndex].current.equalTo(maximum).logicalOr(
                    extracted.model.variables[xIndex].current.equalTo(zero));
            const auto verified = invariant::verifyInvariant(extracted.model, candidate);
            EXPECT_EQ(verified.status, invariant::VerificationStatus::Proved) << verified.reason;
#endif
        });
}

TEST_F(TransitionModelExtractorTest, ExtractsUnsignedShiftWithPromotedCount) {
    withFirstLoop(
        R"(
        unsigned func(unsigned x) {
            while (x > 1)
                x >>= 1;
            return x;
        }
    )",
        [&](const auto &loopEntry, const auto &loopInfo, const auto &condition) {
            const auto &info  = *loopInfo.entryAndCurrentInfo;
            const auto result = invariant::extractTransitionModel(
                *loopEntry.getPaths().front(), *info.symbolicLoopEntry->getPaths().front(),
                *info.symbolicLoopCurrent, condition, info.loopEntryPoint, exactLimits());
            ASSERT_EQ(result.status, invariant::TransitionExtractionStatus::Success)
                << result.reason;
            ASSERT_TRUE(result.extracted.has_value());

            const auto xIndex = variableIndex(*result.extracted, "x");
            ASSERT_LT(xIndex, result.extracted->model.variables.size());
            const auto update = symbolic::BinaryExpr::tryFrom(
                result.extracted->model.branches[0].nextValues[xIndex]);
            ASSERT_TRUE(update.has_value());
            EXPECT_EQ(update->operation(), symbolic::BinaryOp::ShiftRight);
            EXPECT_EQ(update->left().getValType(),
                      (symbolic::ExprType{symbolic::ExprScalarKind::UInt, 32}));
            EXPECT_EQ(update->right().getValType(),
                      (symbolic::ExprType{symbolic::ExprScalarKind::Int, 32}));

#if ACSLG_NUMERICAL_INVARIANT_HAS_Z3
            const auto verified = invariant::verifyInvariant(
                result.extracted->model, symbolic::LiteralExpr{condition.factory(), true});
            EXPECT_EQ(verified.status, invariant::VerificationStatus::Proved) << verified.reason;
#endif
        });
}

TEST_F(TransitionModelExtractorTest, ExtractsUInt64ShiftWithInt32Count) {
    withFirstLoop(
        R"(
        unsigned long long func(void) {
            unsigned long long x = 1ULL;
            int count = 63;
            while (x == 1ULL)
                x <<= count;
            return x;
        }
    )",
        [&](const auto &loopEntry, const auto &loopInfo, const auto &condition) {
            const auto &info  = *loopInfo.entryAndCurrentInfo;
            const auto result = invariant::extractTransitionModel(
                *loopEntry.getPaths().front(), *info.symbolicLoopEntry->getPaths().front(),
                *info.symbolicLoopCurrent, condition, info.loopEntryPoint, exactLimits());
            ASSERT_EQ(result.status, invariant::TransitionExtractionStatus::Success)
                << result.reason;
            ASSERT_TRUE(result.extracted.has_value());

            const auto &extracted = *result.extracted;
            const auto xIndex     = variableIndex(extracted, "x");
            const auto countIndex = variableIndex(extracted, "count");
            ASSERT_LT(xIndex, extracted.model.variables.size());
            ASSERT_LT(countIndex, extracted.model.variables.size());
            const auto update =
                symbolic::BinaryExpr::tryFrom(extracted.model.branches[0].nextValues[xIndex]);
            ASSERT_TRUE(update.has_value());
            EXPECT_EQ(update->operation(), symbolic::BinaryOp::ShiftLeft);
            EXPECT_EQ(update->left().getValType(),
                      (symbolic::ExprType{symbolic::ExprScalarKind::UInt, 64}));
            EXPECT_EQ(update->right().getValType(),
                      (symbolic::ExprType{symbolic::ExprScalarKind::Int, 32}));

#if ACSLG_NUMERICAL_INVARIANT_HAS_Z3
            auto &factory = extracted.model.variables[xIndex].current.factory();
            const auto one = symbolic::LiteralExpr{factory, std::uint64_t{1}};
            const auto highBit =
                symbolic::LiteralExpr{factory, std::uint64_t{1} << 63U};
            const auto sixtyThree = symbolic::LiteralExpr{factory, 63};
            const auto candidate =
                extracted.model.variables[xIndex]
                    .current.equalTo(one)
                    .logicalOr(extracted.model.variables[xIndex].current.equalTo(highBit))
                    .logicalAnd(
                        extracted.model.variables[countIndex].current.equalTo(sixtyThree));
            const auto verified = invariant::verifyInvariant(extracted.model, candidate);
            EXPECT_EQ(verified.status, invariant::VerificationStatus::Proved) << verified.reason;
#endif
        });
}

TEST_F(TransitionModelExtractorTest, ExtractsUnsignedBitClearingUpdate) {
    withFirstLoop(
        R"(
        unsigned func(unsigned x) {
            while (x != 0)
                x &= x - 1;
            return x;
        }
    )",
        [&](const auto &loopEntry, const auto &loopInfo, const auto &condition) {
            const auto &info     = *loopInfo.entryAndCurrentInfo;
            const auto prototype = invariant::extractTransitionModel(
                *loopEntry.getPaths().front(), *info.symbolicLoopEntry->getPaths().front(),
                *info.symbolicLoopCurrent, condition, info.loopEntryPoint, prototypeLimits());
            EXPECT_EQ(prototype.status, invariant::TransitionExtractionStatus::Unsupported);

            const auto result = invariant::extractTransitionModel(
                *loopEntry.getPaths().front(), *info.symbolicLoopEntry->getPaths().front(),
                *info.symbolicLoopCurrent, condition, info.loopEntryPoint, exactLimits());
            ASSERT_EQ(result.status, invariant::TransitionExtractionStatus::Success)
                << result.reason;
            ASSERT_TRUE(result.extracted.has_value());

            const auto xIndex = variableIndex(*result.extracted, "x");
            ASSERT_LT(xIndex, result.extracted->model.variables.size());
            const auto update = symbolic::BinaryExpr::tryFrom(
                result.extracted->model.branches[0].nextValues[xIndex]);
            ASSERT_TRUE(update.has_value());
            EXPECT_EQ(update->operation(), symbolic::BinaryOp::BitAnd);

#if ACSLG_NUMERICAL_INVARIANT_HAS_Z3
            const auto verified = invariant::verifyInvariant(
                result.extracted->model, symbolic::LiteralExpr{condition.factory(), true});
            EXPECT_EQ(verified.status, invariant::VerificationStatus::Proved) << verified.reason;
#endif
        });
}

#if ACSLG_NUMERICAL_INVARIANT_HAS_Z3
TEST_F(TransitionModelExtractorTest, RunsBoundedClause2InvPipelineOnRealLoopState) {
    withFirstLoop(R"(
        int func(int n) {
            int x = 0;
            while (x < n)
                ++x;
            return x;
        }
    )",
                  [&](const auto &loopEntry, const auto &loopInfo, const auto &condition) {
                      const auto &info = *loopInfo.entryAndCurrentInfo;
                      ModelAwareProvider provider;
                      const auto result = invariant::runClause2InvPrototype(
                          *loopEntry.getPaths().front(),
                          *info.symbolicLoopEntry->getPaths().front(), *info.symbolicLoopCurrent,
                          condition, info.loopEntryPoint, provider, {}, exactLimits());

                      ASSERT_EQ(result.status, invariant::Clause2InvPipelineStatus::Emitted)
                          << result.reason;
                      ASSERT_TRUE(result.acsl.has_value());
                      EXPECT_EQ(result.acsl->find("loop invariant "), 0U);
                      EXPECT_EQ(result.acsl->find("\\at("), std::string::npos);
                      EXPECT_EQ(provider.calls, 1U);
                      EXPECT_EQ(result.providerCalls, 1U);
                      EXPECT_LE(result.solverQueries, 64U);
                  });
}

TEST_F(TransitionModelExtractorTest, PipelineRejectsUnsafeModeAndInsufficientProofBudget) {
    withFirstLoop(
        R"(
        int func(int n) {
            int x = 0;
            while (x < n)
                ++x;
            return x;
        }
    )",
        [&](const auto &loopEntry, const auto &loopInfo, const auto &condition) {
            const auto &info = *loopInfo.entryAndCurrentInfo;
            ModelAwareProvider provider;
            const auto rejected = invariant::runClause2InvPrototype(
                *loopEntry.getPaths().front(), *info.symbolicLoopEntry->getPaths().front(),
                *info.symbolicLoopCurrent, condition, info.loopEntryPoint, provider);
            EXPECT_EQ(rejected.status, invariant::Clause2InvPipelineStatus::ExtractionFailed);
            EXPECT_EQ(provider.calls, 0U);

            invariant::ClauseSynthesisLimits noFinalProof;
            noFinalProof.clauseLimits.maxSolverQueries = 2;
            const auto exhausted                       = invariant::runClause2InvPrototype(
                *loopEntry.getPaths().front(), *info.symbolicLoopEntry->getPaths().front(),
                *info.symbolicLoopCurrent, condition, info.loopEntryPoint, provider, {},
                exactLimits(), noFinalProof);
            EXPECT_EQ(exhausted.status, invariant::Clause2InvPipelineStatus::BudgetExceeded);
            EXPECT_EQ(provider.calls, 0U);
        });
}
#endif
