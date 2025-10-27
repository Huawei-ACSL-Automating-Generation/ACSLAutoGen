// src/SpecGenerator/loopInvariantPlugins.cpp

#include "expr.h"
#include "macros.h"
#include "specGenerator.h"
#include <llvm-19/llvm/Support/Casting.h>
#include <memory>
#include <unordered_set>
#include "state.h"
#include "loopInvTemplates.h"
#include "utils.h"

namespace acslg::spec_generator {
    namespace symb = acslg::analyzer::symbolic;

    class CheckAndDumpLoopInfoPlugin : public LoopInvariantPlugin {
      public:
        CheckAndDumpLoopInfoPlugin(const std::string &ID) : id_(ID) {}
        std::string_view id() const override { return id_; }
        GenResultType generate(const analyzer::ProgramState &,
                               const analyzer::ProgramState &,
                               const LoopInfo &loopInfo) const override {
            if (loopInfo.loopEntryInfo_) {
                auto &loopEntryInfo = loopInfo.loopEntryInfo_.value();
                if (loopEntryInfo.symbolicLoopEntry_->getPaths().size() != 1) {
                    ERROR("`symbolicLoopEntry_` is in an invalid state");
                }
                INFO("`loopEntryInfo_` is std::set.");
                INFO(loopEntryInfo.symbolicLoopEntry_->dump());
            } else {
                INFO("`loopEntryInfo_` isn't std::set.");
            }

            if (loopInfo.indexInfo_) {
                auto &indexInfo = loopInfo.indexInfo_.value();
                INFO("`loopEntryInfo_` is std::set.");
                INFO("`indexRealAddr_`: " + indexInfo.indexRealAddr_->dump());
                INFO("`indexSymbolicValue_`: " + indexInfo.indexSymbolicValue_->dump());
                std::string opStr;
                switch (indexInfo.op_) {
#define BINARY_OPERATION(Name, Spelling)                                                           \
    case clang::BO_##Name: opStr = #Spelling; break;
#include <clang/AST/OperationKinds.def>
                    default: UNREACHABLE();
                }
                INFO("`op_`: " + opStr);
                INFO("`indexBound_`: " + indexInfo.indexBound_->dump());
                INFO("`preciseLoopCount_`: " + indexInfo.preciseLoopCount_->dump());
                INFO("`maxLoopCount_`: " + indexInfo.maxLoopCount_->dump());
                INFO("`indexPattern_`: " + indexInfo.indexPattern_.dump());
            } else {
                INFO("indexInfo_ isn't std::set.");
            }

            if (loopInfo.patternInfo_) {
                auto &patternInfo = loopInfo.patternInfo_.value();
                INFO("patternInfo_ is std::set.");
                for (auto &[addr, pattern] : patternInfo.patternsMap_) {
                    INFO("address: " + addr.get().dump());
                    if (pattern)
                        INFO("pattern: " + pattern.value().dump());
                    else
                        INFO("pattern: std::nullopt(too complex)");
                }
            } else {
                INFO("patternInfo_ isn't std::set.");
            }

            return GenResultType{.acsl            = std::nullopt,
                                 .acslUsedPoints  = {},
                                 .isContinue      = true,
                                 .perPathPostInfo = {}};
        }

      private:
        std::string id_;
    };
    REGISTER_ACSL_PLUGIN(CheckAndDumpLoopInfoPlugin, "checkAndDumpLoopInfo");

    class LinearInvariantPlugin : public LoopInvariantPlugin {
      public:
        LinearInvariantPlugin(const std::string &ID) : id_(ID) {}
        std::string_view id() const override { return id_; }
        GenResultType generate(const analyzer::ProgramState &,
                               const analyzer::ProgramState &,
                               const LoopInfo &loopInfo) const override {
            if (loopInfo.loopEntryInfo_ == std::nullopt || loopInfo.indexInfo_ == std::nullopt)
                ERROR("Dependencies are not met.");

            auto &loopEntryInfo = loopInfo.loopEntryInfo_.value();
            auto &indexInfo     = loopInfo.indexInfo_.value();

            if (loopEntryInfo.symbolicLoopEntry_->getPaths().size() != 1) {
                ERROR("SymbolicLoopEntry_ has something wrong, check the SetLoopEntryPlugin?");
            }

            using mem_map_vector =
                std::vector<symb::AddressBoxMap<std::unique_ptr<symb::SymbolicExpr>>>;

            std::unique_ptr<symb::SymbolicExpr> loopCond;
            auto lhs = indexInfo.indexSymbolicValue_->clone();
            auto rhs = indexInfo.indexBound_->clone();
            switch (indexInfo.op_) {
                using enum clang::BinaryOperatorKind;
                using enum symb::BinaryOpExpr::Operator;
                case BO_LT: {
                    auto newRHS = std::make_unique<symb::BinaryOpExpr>(
                        rhs->clone(), Subtract, std::make_unique<symb::LiteralExpr>(1));
                    loopCond = std::make_unique<symb::BinaryOpExpr>(lhs->clone(), LessEqual,
                                                                    std::move(newRHS));
                    break;
                }
                case BO_GT: {
                    auto newRHS = std::make_unique<symb::BinaryOpExpr>(
                        rhs->clone(), Add, std::make_unique<symb::LiteralExpr>(1));
                    loopCond = std::make_unique<symb::BinaryOpExpr>(lhs->clone(), GreaterEqual,
                                                                    std::move(newRHS));
                    break;
                }
                case BO_LE:
                    loopCond = std::make_unique<symb::BinaryOpExpr>(lhs->clone(), LessEqual,
                                                                    std::move(rhs));
                    break;
                case BO_GE:
                    loopCond = std::make_unique<symb::BinaryOpExpr>(lhs->clone(), GreaterEqual,
                                                                    std::move(rhs));
                    break;
                case BO_NE: {
                    if (indexInfo.indexPattern_.step_ < 0) {
                        auto rhsPlus1 = std::make_unique<symb::BinaryOpExpr>(
                            rhs->clone(), Add, std::make_unique<symb::LiteralExpr>(1));
                        auto geExpr = std::make_unique<symb::BinaryOpExpr>(
                            lhs->clone(), GreaterEqual, std::move(rhsPlus1));
                        loopCond = std::move(geExpr);
                    } else if (indexInfo.indexPattern_.step_ > 0) {
                        auto rhsMinus1 = std::make_unique<symb::BinaryOpExpr>(
                            rhs->clone(), Subtract, std::make_unique<symb::LiteralExpr>(1));
                        auto leExpr = std::make_unique<symb::BinaryOpExpr>(lhs->clone(), LessEqual,
                                                                           std::move(rhsMinus1));
                        loopCond    = std::move(leExpr);
                    } else {
                        UNREACHABLE();
                    }
                    break;
                }
                default:
                    ERROR("Unexpected operator, check `setIndexPlugin` may solve this problem.");
            }
            DEBUG(loopCond->dump());

            auto loopEntry   = loopEntryInfo.symbolicLoopEntry_->clone();
            auto loopCurrent = loopEntryInfo.symbolicLoopEntry_->clone();

            loopCurrent->step(loopInfo.condExpr_);
            loopCurrent->step(loopInfo.bodyStmt_);
            loopCurrent->step(loopInfo.incStmt_);

            auto &paths = loopCurrent->getPaths();

            auto invsAndPaths =
                analyzer::buildLoopInvariant(std::move(loopCond), *loopEntry, *loopCurrent);

            if (invsAndPaths.size() != 1)
                UNIMPLEMENT("Only support one path now");

            std::string spec;
            std::vector<PostInfo> postStates;
            for (auto &[inv, postInfo] : invsAndPaths) {
                // TODO: use behavior
                if (inv != std::nullopt) {
                    spec += *inv;
                    spec += '\n';
                }
                postStates.emplace_back(std::move(postInfo.first), std::move(postInfo.second));
            }
            if (!spec.empty()) {
                // restd::move '\n'
                spec.pop_back();
            }

            if (spec.empty())
                return GenResultType{.acsl            = std::nullopt,
                                     .acslUsedPoints  = {},
                                     .isContinue      = true,
                                     .perPathPostInfo = std::move(postStates)};
            return GenResultType{.acsl            = std::move(spec),
                                 .acslUsedPoints  = {},
                                 .isContinue      = true,
                                 .perPathPostInfo = std::move(postStates)};
        }

      private:
        std::string id_;
    };
    REGISTER_ACSL_PLUGIN(LinearInvariantPlugin, "StInGXPlugin");

    // todo: deal with complex range
    class LoopAssignsPlugin : public LoopInvariantPlugin {
      public:
        LoopAssignsPlugin(const std::string &ID) : id_(ID) {}
        std::string_view id() const override { return id_; }
        GenResultType generate(const analyzer::ProgramState &preState,
                               const analyzer::ProgramState &loopEntry,
                               const LoopInfo &loopInfo) const override {
            if (loopInfo.loopEntryInfo_ == std::nullopt || loopInfo.indexInfo_ == std::nullopt ||
                loopInfo.patternInfo_ == std::nullopt)
                ERROR("Dependencies are not met.");

            auto &loopEntryInfo = loopInfo.loopEntryInfo_.value();
            auto &indexInfo     = loopInfo.indexInfo_.value();
            auto &patternInfo   = loopInfo.patternInfo_.value();

            if (loopEntryInfo.symbolicLoopEntry_->getPaths().size() != 1) {
                ERROR("SymbolicLoopEntry_ has something wrong, check the SetLoopEntryPlugin?");
            }

            auto &entryMS = loopEntryInfo.symbolicLoopEntry_->getPaths().at(0)->getMemoryState();

            std::vector<symb::AddressBox> assignedAddrs;
            std::vector<PostInfo> postInfo;

            // This plugin does not produce branches.
            postInfo.emplace_back();
            auto &memoryMap = postInfo.at(0).memoryMap_;
            auto &pathConds = postInfo.at(0).pathConds_;

            auto isLocal = [&](const symb::Address &addr) {
                auto root = addr.getFromRoot();
                if (root == std::nullopt)
                    TODO();
                auto &varAddrMap = preState.getPaths().at(0)->getVarAddr();
                if (!varAddrMap.contains(root.value()))
                    return true;
                return false;
            }; // isLocal end

            auto tryGetAsRange =
                [&](const symb::Address &addr) -> std::optional<symb::SymbolAddress> {
                auto symbolAddr = llvm::dyn_cast<const symb::SymbolAddress>(&addr);
                if (symbolAddr == nullptr)
                    return std::nullopt;

                auto from = symbolAddr->getFromAddr();
                // If the base address itself is x-step, then there is no need to check the
                // offset (or to check it for reliability).
                if (from == std::nullopt)
                    ERROR("Invalid state");
                if (auto it = patternInfo.patternsMap_.find(*from.value());
                    it != patternInfo.patternsMap_.end()) {
                    auto &pattern = it->second;
                    if (pattern == std::nullopt)
                        TODO();
                    auto result = *symbolAddr;
                    result.setOffset(
                        std::make_unique<symb::LiteralExpr>(symb::SymbolAddress::ZERO_OFFSET));
                    if (!indexInfo.preciseLoopCount_->isUnknown())
                        result.setLength(indexInfo.preciseLoopCount_->simplifiedExpr());
                    else
                        result.setLength(indexInfo.maxLoopCount_->simplifiedExpr());
                    return result;
                }

                auto offset = symbolAddr->getOffset();
                // Is offset x-step?
                if (auto symbolValue = llvm::dyn_cast<const symb::SymbolValue>(offset.get())) {
                    auto symbolValueFrom = symbolValue->getFromAddr();
                    if (symbolValueFrom == std::nullopt)
                        TODO();
                    if (auto it = patternInfo.patternsMap_.find(*symbolValueFrom.value());
                        it != patternInfo.patternsMap_.end()) {
                        auto &pattern = it->second;
                        if (pattern == std::nullopt)
                            TODO();
                        auto result = *symbolAddr;
                        result.setOffset(pattern.value().initialValue_->clone());
                        if (!indexInfo.preciseLoopCount_->isUnknown())
                            result.setLength(indexInfo.preciseLoopCount_->simplifiedExpr());
                        else
                            result.setLength(indexInfo.maxLoopCount_->simplifiedExpr());
                        return result;
                    } else {
                        TODO();
                    }
                }
                return std::nullopt;
            }; // tryGetAsRange end

            std::unordered_map<size_t, utils::not_null<std::unique_ptr<symb::SymbolicExpr>>>
                condsForInsert;
            for (auto &[addr, pattern] : patternInfo.patternsMap_) {
                using enum symb::BinaryOpExpr::Operator;
                if (isLocal(addr))
                    continue;
                if (auto range = tryGetAsRange(addr)) {
                    if (pattern) {
                        auto [_, ok] = memoryMap.emplace(
                            range.value(), symb::UnknownExpr::makeUnknown().into_underlying());
                        // todo
                        // std::make_unique<BinaryOpExpr>(pattern.value().initialValue_->clone(),
                        // Add,
                        //                           std::make_unique<LiteralExpr>(pattern.value().step_)));

                        // Deal with loops like
                        // {
                        //     p[0] = ...;
                        //     p[1] = ...;
                        //     p[2] = ...;
                        //     p += 3;
                        // }
                        // In which case `tryGetAsRange` will return same **range** for all three
                        // expressions.
                        // This unsound method currently exists solely to handle this special case.
                        if (!ok && !indexInfo.preciseLoopCount_->isUnknown())
                            UNREACHABLE();
                    } else {
                        auto [_, ok] = memoryMap.emplace(
                            range.value(), symb::UnknownExpr::makeUnknown().into_underlying());

                        // Deal with loops like
                        // {
                        //     p[0] = ...;
                        //     p[1] = ...;
                        //     p[2] = ...;
                        //     p += 3;
                        // }
                        // In which case `tryGetAsRange` will return same **range** for all three
                        // expressions.
                        // This unsound method currently exists solely to handle this special case.
                        if (!ok && !indexInfo.preciseLoopCount_->isUnknown())
                            UNREACHABLE();
                    }
                    assignedAddrs.emplace_back(std::move(range.value()));
                } else {
                    // Use lambda to eliminate nested if。
                    [&]() {
                        if (!pattern)
                            return;
                        if (!indexInfo.preciseLoopCount_->isUnknown()) {
                            // Loop count is precise (index's step is 1 or -1)

                            // init + step * loopCount
                            auto [_, ok] = memoryMap.emplace(
                                addr,
                                std::make_unique<symb::BinaryOpExpr>(
                                    pattern.value().initialValue_->clone(), Add,
                                    std::make_unique<symb::BinaryOpExpr>(
                                        std::make_unique<symb::LiteralExpr>(pattern.value().step_),
                                        Multiply, indexInfo.preciseLoopCount_->clone())));
                            if (!ok)
                                UNREACHABLE();
                        } else {
                            // index's step is not +-1.

                            // Just check.
                            if (std::abs(pattern.value().step_) !=
                                std::abs(indexInfo.indexPattern_.step_))
                                return;

                            auto pointAfterLoop = symb::SourcePoint::fromStmtAfter(
                                loopInfo.bodyStmt_,
                                loopEntryInfo.symbolicLoopEntry_->getContext().getSourceManager(),
                                loopEntryInfo.symbolicLoopEntry_->getContext().getLangOptions());

                            auto indexValueAfterLoop = getSymbol(
                                indexInfo.indexExpr_->getType(),
                                indexInfo.indexRealAddr_->addressClone().into_underlying(),
                                std::move(pointAfterLoop));

                            using enum symb::BinaryOpExpr::Operator;

                            // i >= n (step > 0) or
                            // i <= 0 (step < 0)
                            auto firstIndexCond =
                                (indexInfo.indexPattern_.step_ > 0
                                     ? std::make_unique<symb::BinaryOpExpr>(
                                           indexValueAfterLoop->clone(), GreaterEqual,
                                           indexInfo.indexBound_->clone())
                                     : std::make_unique<symb::BinaryOpExpr>(
                                           indexValueAfterLoop->clone(), LessEqual,
                                           indexInfo.indexBound_->clone()));

                            // i < n + step (step > 0) or
                            // i > 0 + step (step < 0)
                            auto secondIndexCond =
                                (indexInfo.indexPattern_.step_ > 0
                                     ? std::make_unique<symb::BinaryOpExpr>(
                                           indexValueAfterLoop->clone(), LessThan,
                                           std::make_unique<symb::BinaryOpExpr>(
                                               indexInfo.indexBound_->clone(), Add,
                                               std::make_unique<symb::LiteralExpr>(
                                                   indexInfo.indexPattern_.step_)))
                                     : std::make_unique<symb::BinaryOpExpr>(
                                           indexValueAfterLoop->clone(), GreaterThan,
                                           std::make_unique<symb::BinaryOpExpr>(
                                               indexInfo.indexBound_->clone(), Add,
                                               std::make_unique<symb::LiteralExpr>(
                                                   indexInfo.indexPattern_.step_))));

                            condsForInsert.emplace(firstIndexCond->hash(), firstIndexCond->clone());
                            condsForInsert.emplace(secondIndexCond->hash(),
                                                   secondIndexCond->clone());

                            // abs(i_post - i_init)
                            auto diff = (indexInfo.indexPattern_.step_ > 0
                                             ? std::make_unique<symb::BinaryOpExpr>(
                                                   indexValueAfterLoop->clone(), Subtract,
                                                   indexInfo.indexSymbolicValue_->clone())
                                             : std::make_unique<symb::BinaryOpExpr>(
                                                   indexInfo.indexSymbolicValue_->clone(), Subtract,
                                                   indexValueAfterLoop->clone()));

                            auto postValue = (pattern.value().step_ > 0
                                                  ? std::make_unique<symb::BinaryOpExpr>(
                                                        pattern.value().initialValue_->clone(), Add,
                                                        std::move(diff))
                                                  : std::make_unique<symb::BinaryOpExpr>(
                                                        pattern.value().initialValue_->clone(),
                                                        Subtract, std::move(diff)));

                            memoryMap.emplace(addr, std::move(postValue));
                        }
                    }();

                    // If all branches fails, fall into here
                    memoryMap.emplace(addr, symb::UnknownExpr::makeUnknown().into_underlying());
                    assignedAddrs.push_back(addr);
                }
            }

            for (auto &[_, cond] : condsForInsert) {
                pathConds.push_back(std::move(cond));
            }

            std::string specs;
            std::unordered_set<symb::SourcePoint> allUsedPoints;
            auto loopEntryPoint = loopEntryInfo.symbolicLoopEntry_->getStartPoint();
            std::unordered_set<size_t> insertedACSL{};
            for (auto &addr : assignedAddrs) {
                for (auto &path : loopEntry.getPaths()) {
                    auto concreteAddrExpr = addr.get().getSubstitutedExpr(*path, loopEntryPoint);
                    auto concreteAddr =
                        llvm::dyn_cast<const symb::Address>(concreteAddrExpr.get().get());
                    if (concreteAddr == nullptr)
                        UNREACHABLE();

                    auto acslExpected = concreteAddr->getACSLOfValue({});
                    if (!acslExpected &&
                        acslExpected.error() == symb::SymbolicExpr::GetACSLError::UnknownExpr)
                        acslExpected = addr.get().getACSLOfValue(
                            {.predefinedLabels = {{loopEntryPoint, "LoopEntry"}}}, loopEntryPoint);
                    if (!acslExpected) {
                        WARN("Value of {" + concreteAddr->dump() + "} getACSL failed.");
                        continue;
                    }
                    auto &[spec, usedPoints] = acslExpected.value();
                    auto specHash            = utils::hash_val(spec);
                    if (insertedACSL.contains(specHash))
                        continue;
                    insertedACSL.insert(specHash);
                    specs += spec + ", ";
                    allUsedPoints.insert(std::make_move_iterator(usedPoints.begin()),
                                         std::make_move_iterator(usedPoints.end()));
                }
            }

            if (specs.empty())
                return GenResultType{.acsl            = R"(loop assigns \nothing;)",
                                     .acslUsedPoints  = {},
                                     .isContinue      = true,
                                     .perPathPostInfo = {}};
            return GenResultType{.acsl =
                                     "loop assigns " + specs.substr(0, specs.length() - 2) + ";",
                                 .acslUsedPoints  = std::move(allUsedPoints),
                                 .isContinue      = true,
                                 .perPathPostInfo = std::move(postInfo)};
        }

      private:
        std::string id_;
    };
    REGISTER_ACSL_PLUGIN(LoopAssignsPlugin, "loopAssigns");

    class ParadigmMaxMinPlugin : public LoopInvariantPlugin {
      public:
        ParadigmMaxMinPlugin(const std::string &ID) : id_(ID) {}
        std::string_view id() const override { return id_; }
        GenResultType generate(const analyzer::ProgramState &,
                               const analyzer::ProgramState &,
                               const LoopInfo &loopInfo) const override {
            if (loopInfo.loopEntryInfo_ == std::nullopt || loopInfo.indexInfo_ == std::nullopt ||
                loopInfo.patternInfo_ == std::nullopt)
                ERROR("Dependencies are not met.");

            auto &loopEntryInfo = loopInfo.loopEntryInfo_.value();
            auto &indexInfo     = loopInfo.indexInfo_.value();
            auto &patternInfo   = loopInfo.patternInfo_.value();

            if (loopEntryInfo.symbolicLoopEntry_->getPaths().size() != 1) {
                ERROR("SymbolicLoopEntry_ has something wrong, check the SetLoopEntryPlugin?");
            }

            // Only work when loop is 1-step.
            int64_t indexStep;
            if (auto it = patternInfo.patternsMap_.find(*indexInfo.indexRealAddr_);
                it != patternInfo.patternsMap_.end()) {
                if (it->second == std::nullopt)
                    ERROR("PatternsMap_ is in an invalid state");
                if ((*it->second).step_ != 1 && (*it->second).step_ != -1)
                    return GenResultType{.acsl            = std::nullopt,
                                         .acslUsedPoints  = {},
                                         .isContinue      = true,
                                         .perPathPostInfo = {}};
                else
                    indexStep = (*it->second).step_;
            } else {
                ERROR("PatternsMap_ is in an invalid state");
            }

            std::string spec;

            // Hook which deals with every if.
            auto ifVisitor = utils::StmtVisitor{[&](const clang::IfStmt *s) {
                if (s == nullptr)
                    return;

                std::optional<StringTemplate> specTemplate{std::nullopt};
                // Parameters for template filling, see StringTemplate for more information.
                std::optional<std::string> param_n{std::nullopt}, param_array{std::nullopt},
                    param_index{std::nullopt}, param_m{std::nullopt};
                // @SgtPepper114 2025/10/26: I'm in the middle of rewriting `regularForm` as
                // `getACSL`. This plugin is a beast and barely anyone uses it, so I'm not gonna
                // worry about the correct way to do it for now. If it breaks, just band-aid it by
                // fixing all the config and return value stuff of `getACSL`.

                if (auto acslExpected = indexInfo.indexRealAddr_->getACSLOfValue(
                        {.noStateLabelFunctionAt = true})) {
                    param_index = acslExpected.value().first;
                }
                if (auto acslExpected =
                        indexInfo.indexBound_->getACSL({.noStateLabelFunctionAt = true})) {
                    param_n = acslExpected.value().first;
                }

                auto getAddress = [&](const clang::Expr *expr)
                    -> std::optional<utils::not_null<std::unique_ptr<symb::Address>>> {
                    if (expr == nullptr)
                        return std::nullopt;
                    try {
                        // May pass some strange expr to extractAddress.
                        return loopEntryInfo.symbolicLoopEntry_->getPaths().at(0)->extractLValue(
                            expr);
                    } catch (...) { return std::nullopt; }
                }; // getAddress end

                // Is expr 'p[i]', *(p+i) or *it where it == p+i?
                auto parseIndexedArray = [&](const clang::Expr *expr) {
                    if (expr == nullptr)
                        return false;

                    if (auto arraySub = dyn_cast_if_present<clang::ArraySubscriptExpr>(
                            expr->IgnoreParenImpCasts())) {
                        // p[i]
                        auto idxAddr = getAddress(arraySub->getIdx());
                        // Is 'i' loop's index?
                        if (idxAddr == std::nullopt ||
                            *idxAddr.value() != *indexInfo.indexRealAddr_)
                            return false;

                        if (auto addr = getAddress(arraySub->getBase()))
                            if (auto acslExpected = addr.value()->getACSLOfValue(
                                    {.noStateLabelFunctionAt = true})) {
                                param_array = acslExpected.value().first;
                                return true;
                            }
                        return false;
                    } else if (auto unary = dyn_cast_if_present<clang::UnaryOperator>(
                                   expr->IgnoreParenImpCasts());
                               unary && unary->getOpcode() == clang::UnaryOperatorKind::UO_Deref) {
                        if (auto bin = dyn_cast_if_present<clang::BinaryOperator>(
                                unary->getSubExpr()->IgnoreParenImpCasts());
                            bin && bin->getOpcode() == clang::BinaryOperatorKind::BO_Add) {
                            // *(p+i)

                            // Is 'i' loop's index?
                            if (auto rhsAddr = getAddress(bin->getRHS());
                                rhsAddr == std::nullopt ||
                                *rhsAddr.value() != *indexInfo.indexRealAddr_)
                                return false;
                            if (auto addr = getAddress(bin->getLHS()))
                                if (auto acslExpected = addr.value()->getACSLOfValue(
                                        {.noStateLabelFunctionAt = true})) {
                                    param_array = acslExpected.value().first;
                                    return true;
                                }
                            return false;
                        } else if (auto declRef = dyn_cast_if_present<clang::DeclRefExpr>(
                                       unary->getSubExpr()->IgnoreParenImpCasts())) {
                            // *it

                            auto addr = getAddress(declRef);
                            if (addr == std::nullopt)
                                return false;
                            // Does this variable step same as loop?
                            if (auto it = patternInfo.patternsMap_.find(*addr.value());
                                it == patternInfo.patternsMap_.end() ||
                                it->second == std::nullopt || (*it->second).step_ != indexStep)
                                return false;
                            if (auto acslExpected = addr.value()->getACSLOfValue(
                                    {.noStateLabelFunctionAt = true})) {
                                param_array = acslExpected.value().first;
                                return true;
                            }
                            return false;
                        }
                    } else {
                        return false;
                    }
                    UNREACHABLE();
                }; // parseIndexedArray end

                auto isLocal = [&](const clang::VarDecl *varDecl) {
                    if (!loopEntryInfo.symbolicLoopEntry_->getPaths().at(0)->getVarAddr().contains(
                            varDecl))
                        return true;
                    return false;
                }; // isLocal end

                const clang::VarDecl *maxDecl{nullptr}; // max
                clang::Expr *elementExpr{nullptr};      // p[i]

                // Does if's condition has form 'max < p[i]' or 'p[i] > max'?
                auto ifCond = s->getCond();
                if (auto bin =
                        dyn_cast_if_present<clang::BinaryOperator>(ifCond->IgnoreParenImpCasts())) {
                    using enum clang::BinaryOperatorKind;
                    using enum clang::UnaryOperatorKind;

                    clang::DeclRefExpr *maxExpr{nullptr};
                    bool maxOnLeft = true;

                    // Where is 'max'?
                    if (auto declRef = dyn_cast_if_present<clang::DeclRefExpr>(
                            bin->getLHS()->IgnoreParenImpCasts())) {
                        maxExpr     = declRef;
                        elementExpr = bin->getRHS()->IgnoreParenImpCasts();
                    } else if (auto declRef = dyn_cast_if_present<clang::DeclRefExpr>(
                                   bin->getRHS()->IgnoreParenImpCasts())) {
                        maxExpr     = declRef;
                        elementExpr = bin->getLHS()->IgnoreParenImpCasts();
                        maxOnLeft   = false;
                    } else {
                        return;
                    }

                    // Operator is '<', '<=', '>' or '>='.
                    switch (bin->getOpcode()) {
                        case BO_LE:
                        case BO_LT:
                            if (llvm::isa<symb::SymbolValue>(*indexInfo.indexBound_))
                                specTemplate = maxOnLeft ? FIND_MAX_LOOP_WITH_VAR_BOUND
                                                         : FIND_MIN_LOOP_WITH_VAR_BOUND;
                            else
                                specTemplate = maxOnLeft ? FIND_MAX_LOOP_WITH_OTHER_BOUND
                                                         : FIND_MIN_LOOP_WITH_OTHER_BOUND;
                            break;
                        case BO_GE:
                        case BO_GT:
                            if (llvm::isa<symb::SymbolValue>(*indexInfo.indexBound_))
                                specTemplate = maxOnLeft ? FIND_MIN_LOOP_WITH_VAR_BOUND
                                                         : FIND_MAX_LOOP_WITH_VAR_BOUND;
                            else
                                specTemplate = maxOnLeft ? FIND_MIN_LOOP_WITH_OTHER_BOUND
                                                         : FIND_MAX_LOOP_WITH_OTHER_BOUND;
                            break;
                        default: return;
                    }
                    DEBUG("Operator matched.");

                    // Verify 'max'.
                    if (auto var = dyn_cast<clang::VarDecl>(maxExpr->getDecl());
                        var && var->getCanonicalDecl()) {
                        maxDecl = var->getCanonicalDecl();
                        if (isLocal(maxDecl))
                            return;
                        param_m = maxDecl->getNameAsString();
                    } else {
                        return;
                    }
                    DEBUG("Max matched.");

                    // Deal with p[i].
                    if (!parseIndexedArray(elementExpr))
                        return;
                    DEBUG("p[i] matched.");
                }

                if (specTemplate == std::nullopt || param_n == std::nullopt ||
                    param_array == std::nullopt || param_index == std::nullopt ||
                    param_m == std::nullopt)
                    UNREACHABLE();

                // Verify 'then' of if.
                if (auto thenStmt = s->getThen()) {
                    auto symbolState = loopEntryInfo.symbolicLoopEntry_->clone();
                    symbolState->step(thenStmt);
                    for (auto &path : symbolState->getPaths()) {
                        std::unique_ptr<symb::SymbolValue> maxVar{nullptr};
                        if (auto maxValue = path->getVarState(maxDecl);
                            llvm::isa<symb::SymbolValue>(*maxValue)) {
                            maxVar = std::unique_ptr<symb::SymbolValue>(
                                llvm::dyn_cast<symb::SymbolValue>(
                                    std::move(maxValue).into_underlying().release()));
                        } else {
                            return;
                        }

                        auto evalResult = path->evalExpr(elementExpr);
                        if (evalResult.second.size() != 1)
                            ERROR("Branch isn't permitted here.");
                        auto &elementValue = evalResult.second.front();
                        if (*maxVar != *elementValue)
                            return;
                    }
                } else {
                    return;
                }
                DEBUG("Then Verified.");

                // Verify 'else' of if.
                if (auto elseStmt = s->getElse()) {
                    auto symbolState = loopEntryInfo.symbolicLoopEntry_->clone();
                    symbolState->step(elseStmt);
                    for (auto &path : symbolState->getPaths()) {
                        if (auto varAddrIt = path->getVarAddr().find(maxDecl);
                            varAddrIt != path->getVarAddr().end()) {
                            auto &maxAddr = varAddrIt->second;
                            if (!path->isUnchanged(*maxAddr))
                                return;
                        } else {
                            ERROR("Can't find maxDecl after step, something must be wrong.");
                        }
                    }
                }
                DEBUG("Else Verified.");

                // Pretty sure we have found a 'find_max' loop.
                spec += (*specTemplate)
                            .to_string(NameMap{{"n", *param_n},
                                               {"array", *param_array},
                                               {"index", *param_index},
                                               {"m", *param_m}}) +
                        "\n";
            }}; // ifVisitor end
            ifVisitor.runOn(loopInfo.bodyStmt_);

            if (spec.empty())
                return GenResultType{.acsl            = std::nullopt,
                                     .acslUsedPoints  = {},
                                     .isContinue      = true,
                                     .perPathPostInfo = {}};

            spec.pop_back(); // earse \n
            return GenResultType{
                .acsl = spec, .acslUsedPoints = {}, .isContinue = true, .perPathPostInfo = {}};
        }

      private:
        std::string id_;
    };
    REGISTER_ACSL_PLUGIN(ParadigmMaxMinPlugin, "paradigmMaxMin");

    class LoopVariantPlugin : public LoopInvariantPlugin {
      public:
        LoopVariantPlugin(const std::string &ID) : id_(ID) {}
        std::string_view id() const override { return id_; }
        GenResultType generate(const analyzer::ProgramState &,
                               const analyzer::ProgramState &,
                               const LoopInfo &loopInfo) const override {
            if (loopInfo.indexInfo_ == std::nullopt)
                ERROR("Dependencies are not met.");

            auto &indexInfo = loopInfo.indexInfo_.value();

            // Yes, the expression of the loop variant is maxLoopCount. :)
            auto acslExpected = indexInfo.maxLoopCount_->simplifiedExpr()->getACSL(
                {.noStateLabelFunctionAt = true});
            if (!acslExpected) {
                WARN("Variant {" + indexInfo.maxLoopCount_->simplifiedExpr()->dump() +
                     "} getACSL failed.");
                return GenResultType{.acsl            = std::nullopt,
                                     .acslUsedPoints  = {},
                                     .isContinue      = true,
                                     .perPathPostInfo = {}};
            }
            auto spec = "loop variant " + acslExpected.value().first + ";";
            return GenResultType{.acsl            = std::move(spec),
                                 .acslUsedPoints  = {},
                                 .isContinue      = true,
                                 .perPathPostInfo = std::vector<PostInfo>{}};
        }

      private:
        std::string id_;
    };
    REGISTER_ACSL_PLUGIN(LoopVariantPlugin, "loopVariant");
} // namespace acslg::spec_generator