/**
 * @file state.cpp
 * @brief Implements symbolic execution state management, path evaluation, and memory handling.
 */
#include "state.h"

#include <clang/AST/Type.h>
#include <llvm/Support/Casting.h>
#include <queue>
#include <unordered_map>
#include <memory>
#include <set>
#include <ranges>
#include <llvm/ADT/TypeSwitch.h>
#include <llvm/ADT/APSInt.h>
#include <clang/AST/Expr.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/Decl.h>
#include <clang/AST/StmtCXX.h>
#include <clang/AST/ParentMapContext.h>

#include "Symbolic/expr.h"
#include "Symbolic/aggregateExpr.h"
#include "macros.h"
#include "Utils/utils.h"
#include "SpecGenerator/specGenerator.h"
#include "crossTU.h"

namespace acslg::analyzer {
    using std::literals::string_literals::operator""s;

    namespace {
        std::optional<const clang::VarDecl *> getRootFromSymbol(const symbolic::Symbol &symbol) {
            if (auto *sv = symbolic::dyn_cast<const symbolic::SymbolValue>(&symbol))
                return sv->getFromRoot();
            if (auto *sa = symbolic::dyn_cast<const symbolic::SymbolAddress>(&symbol))
                return sa->getFromRoot();
            return std::nullopt;
        }

        std::unique_ptr<symbolic::Structure> makeStructureForRecord(
            symbolic::ExprFactory &factory,
            const clang::RecordDecl *record,
            std::unique_ptr<symbolic::Address> base,
            symbolic::SourcePoint point) {
            if (!record || !record->isCompleteDefinition())
                ERROR("Expected complete structure type.");
            record = record->getDefinition();
            auto from = symbolic::Addr{factory, factory.importAddress(*base)};
            return symbolic::cloneStructure(factory.structure(
                record, record->getASTContext().getASTRecordLayout(record), from.handle(),
                point));
        }

        std::unique_ptr<symbolic::Structure> makeStructureForBase(
            symbolic::ExprFactory &factory,
            clang::QualType type,
            std::unique_ptr<symbolic::Address> base,
            symbolic::SourcePoint point) {
            return makeStructureForRecord(
                factory, type->getAsRecordDecl(), std::move(base), point);
        }

        bool containsLocalVar(const symbolic::SymbolicExpr &expr,
                              const std::unordered_set<const clang::VarDecl *> &locals) {
            auto [usedSymbols, unusedSymbols] = symbolic::SymbolicExpr::collectUsedSymbols(expr);
            (void)unusedSymbols;
            for (const auto &entry : usedSymbols) {
                const auto &symbol = entry.second;
                auto root          = getRootFromSymbol(*symbol);
                if (!root)
                    continue;
                if (locals.contains(root.value()->getCanonicalDecl()))
                    return true;
            }
            return false;
        }

        std::unique_ptr<symbolic::SymbolicExpr> dropLocalConjuncts(
            symbolic::ExprFactory &factory,
            const symbolic::SymbolicExpr &expr,
            const std::unordered_set<const clang::VarDecl *> &locals) {
            if (const auto *bin = symbolic::dyn_cast<symbolic::BinaryOpExpr>(&expr);
                bin && bin->getOperator() == symbolic::BinaryOpExpr::Operator::LogicalAnd) {
                auto lhs = dropLocalConjuncts(factory, *bin->getLeft(), locals);
                auto rhs = dropLocalConjuncts(factory, *bin->getRight(), locals);
                if (!lhs && !rhs)
                    return nullptr;
                if (!lhs)
                    return rhs;
                if (!rhs)
                    return lhs;
                return factory
                    .binary(factory.importExpr(*lhs),
                            symbolic::BinaryOpExpr::Operator::LogicalAnd,
                            factory.importExpr(*rhs))
                    ->clone()
                    .into_underlying();
            }

            if (containsLocalVar(expr, locals))
                return nullptr;
            return factory.cloneExpr(factory.importExpr(expr)).into_underlying();
        }

        utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> buildUnknown(
            symbolic::ExprFactory &factory) {
            return factory.cloneExpr(factory.unknown());
        }

        utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>
        cloneExpr(symbolic::ExprFactory &factory, const symbolic::SymbolicExpr &expr) {
            return factory.cloneExpr(factory.importExpr(expr));
        }

        using symbolic::cloneStructure;
        using symbolic::cloneSymbolAddress;
        using symbolic::cloneVariableAddress;
        using symbolic::makeFieldAddress;
        using symbolic::makeStructure;
        using symbolic::makeVariableAddress;
    } // namespace

    /**
     * @brief Copy-construct a path, optionally sharing symbolic expressions instead of cloning.
     * @param other Path to copy from.
     * @param shallowCopy When true, reuse symbolic objects to avoid deep duplication.
     */
    Path::Path(const Path &other, bool shallowCopy)
        : memoryState_(other.context_.getExprFactory()), context_(other.context_),
          startPoint_(other.startPoint_) {
        if (shallowCopy) {
            for (const auto &cond : other.pathConditions_) {
                pathConditions_.emplace(cond);
            }
            currentState_ = other.currentState_;
            if (other.returnExpr_)
                returnExpr_.emplace(other.returnExpr_.value());
            else
                returnExpr_ = std::nullopt;
        } else {
            TODO();
        }
    }

    /**
     * @brief Swap all internal members with another path instance.
     * @param o Path whose contents will be exchanged.
     */
    void Path::swap(Path &o) noexcept {
        using std::swap;
        swap(currentState_, o.currentState_);
        swap(returnExpr_, o.returnExpr_);
        swap(pathConditions_, o.pathConditions_);
        swap(varAddr_, o.varAddr_);
        swap(memoryState_, o.memoryState_);
        swap(startPoint_, o.startPoint_);
    }

    /**
     * @brief Merge another compatible path into this one, reconciling memory, conditions, and
     * return state.
     * @param other Path to merge from; contexts and start points must match.
     */
    void Path::mergeWith(const Path &other) {
        if (&context_ != &other.context_)
            ERROR("mergeWith: context mismatch.");
        if (currentState_ != other.currentState_)
            ERROR("mergeWith: path state mismatch.");
        if (!(startPoint_ == other.startPoint_))
            ERROR("mergeWith: start point mismatch.");
        if (stmtCtx_ != other.stmtCtx_)
            ERROR("mergeWith: statement context mismatch.");

        // Union variable addresses so that both paths agree on storage locations.
        for (const auto &[var, addr] : other.varAddr_)
            varAddr_.emplace(
                var, cloneVariableAddress(context_.getExprFactory().importAddress(*addr)));

        // Collect all addresses touched by either path to merge differing symbolic values.
        MemoryModel::KeySet addresses;
        for (auto &&[addr, value] : memoryState_.flat())
            addresses.insert(addr);
        for (auto &&[addr, value] : other.memoryState_.flat())
            addresses.insert(addr);

        for (const auto &addrBox : addresses) {
            auto lhsVal = memoryState_.read(addrBox);
            auto rhsVal = other.memoryState_.read(addrBox);

            if (auto *fieldAddr = symbolic::dyn_cast<symbolic::FieldAddress>(&addrBox.get())) {
                if (fieldAddr->getDefinition() &&
                    fieldAddr->getDefinition()->getNameAsString() == "BigNum" &&
                    fieldAddr->getFieldIndex() == 4) {
                    DEBUG(
                        "mergeWith BigNum->data: lhs="
                        << (lhsVal ? lhsVal.value()->dump() : "<none>")
                        << " rhs=" << (rhsVal ? rhsVal.value()->dump() : "<none>") << " lhsFrom="
                        << (lhsVal && symbolic::isFrom(*lhsVal.value(), addrBox, startPoint_)
                                ? "yes"
                                : "no")
                        << " rhsFrom="
                        << (rhsVal && symbolic::isFrom(*rhsVal.value(), addrBox, other.startPoint_)
                                ? "yes"
                                : "no"));
                }
            }

            if (lhsVal && symbolic::isa<symbolic::Structure>(lhsVal.value().get().get()))
                continue;
            if (rhsVal && symbolic::isa<symbolic::Structure>(rhsVal.value().get().get()))
                continue;

            if (lhsVal && rhsVal) {
                if (*lhsVal.value() == *rhsVal.value())
                    continue;
            } else if (rhsVal) {
                if (symbolic::isFrom(*rhsVal.value(), addrBox, other.startPoint_))
                    continue;
            } else if (lhsVal) {
                if (symbolic::isFrom(*lhsVal.value(), addrBox, startPoint_))
                    continue;
            } else {
                UNREACHABLE();
            }
            if (auto *fieldAddr = symbolic::dyn_cast<symbolic::FieldAddress>(&addrBox.get())) {
                if (fieldAddr->getDefinition() &&
                    fieldAddr->getDefinition()->getNameAsString() == "BigNum" &&
                    fieldAddr->getFieldIndex() == 4) {
                    DEBUG("mergeWith BigNum->data: writing Unknown due to mismatch");
                }
            }
            memoryState_.write(addrBox, buildUnknown(context_.getExprFactory()).into_underlying());
        }

        // Intersect path conditions; a merged path must satisfy constraints from both sides.
        PathConditions intersected;
        intersected.reserve(std::min(pathConditions_.size(), other.pathConditions_.size()));
        for (const auto &cond : pathConditions_) {
            if (other.pathConditions_.find(cond) != other.pathConditions_.end())
                intersected.emplace(cond);
        }
        pathConditions_ = std::move(intersected);

        // Merge return expression if applicable
        if (currentState_ == PathState::Return) {
            if (!returnExpr_ || !other.returnExpr_)
                ERROR("mergeWith: Return state without return expression.");
            if (*returnExpr_.value() == *other.returnExpr_.value())
                return;
            returnExpr_.emplace(context_.getExprFactory().unknown());
        }
    }

    /**
     * @brief Rebuild symbolic values with a different creation point.
     * @param newStartPoint Source label to attribute to regenerated symbols.
     *
     * This clears memory and path conditions, then recreates each variable's symbolic value so
     * downstream substitutions reference the new point.
     */
    void Path::resymbolize(symbolic::SourcePoint newStartPoint) {
        memoryState_.clear();
        pathConditions_.clear();

        startPoint_ = std::move(newStartPoint);
        for (auto &[varDecl, addr] : varAddr_) {
            clang::QualType ty = varDecl->getType();

            auto symbol = getSymbol(ty, addr->addressClone().into_underlying(), startPoint_);
            updateMemory(*addr, std::move(symbol));
        }
    }

    /**
     * @brief Translate a left-hand side expression into a symbolic address.
     * @param lhs Expression serving as an assignment target.
     * @return Address box pointing to the storage location.
     *
     * The routine walks through decl references, array subscripts, pointer dereferences, and member
     * accesses, allocating unknown symbols on demand when the memory model lacks entries.
     */
    utils::not_null<std::unique_ptr<symbolic::Address>> Path::extractLValue(const clang::Expr *lhs) {
        auto lexpr = lhs->IgnoreParenImpCasts();
        if (auto declRef = dyn_cast<clang::DeclRefExpr>(lexpr)) {
            if (auto varDecl = llvm::dyn_cast<clang::VarDecl>(declRef->getDecl())) {
                if (auto it = varAddr_.find(varDecl->getCanonicalDecl()); it != varAddr_.end()) {
                    return cloneVariableAddress(
                        context_.getExprFactory().importAddress(*it->second));
                } else {
                    ERROR("varState has no `VarDecl*` of `DeclRefExpr`, undefined variable?");
                }
            }
            ERROR("Not a varDecl Ref.");
        }

        if (auto *arr = dyn_cast<clang::ArraySubscriptExpr>(lexpr)) {
            auto baseAddr = extractLValue(arr->getBase());
            if (const auto symbol = memoryState_.read(*baseAddr)) {
                auto symbolAddr =
                    symbolic::dyn_cast<const symbolic::SymbolAddress>(symbol.value().get().get());
                if (symbolAddr == nullptr)
                    ERROR("Value of ArraySubscriptExpr's base is not 'symbolic::SymbolAddress', "
                          "base is "
                          "neither pointer nor std::array?");
                auto idxEval    = evalExpr(arr->getIdx());
                if (idxEval.second.size() != 1)
                    ERROR("This location does not support control flow branches.");
                auto &factory = context_.getExprFactory();
                symbolic::Addr baseAddrFacade{factory, factory.importAddress(*symbolAddr)};
                symbolic::Expr idxExpr{factory, factory.importExpr(*idxEval.second[0])};
                auto resultAddr =
                    baseAddrFacade.withAddedOffset(idxExpr)->addressClone().into_underlying();
                if (!memoryState_.contains(*resultAddr)) {
                    auto newSymbol = getSymbol(
                        arr->getType(), resultAddr->addressClone().into_underlying(), startPoint_);
                    memoryState_.write(*resultAddr, std::move(newSymbol));
                }
                return resultAddr;
            } else {
                ERROR("memoryState_ has no ArraySubscriptExpr's base, base is neither pointer nor "
                      "array?");
            }
        }

        if (auto *uop = dyn_cast<clang::UnaryOperator>(lexpr)) {
            if (uop->getOpcode() == clang::UnaryOperatorKind::UO_Deref) {
                auto addrEval = evalExpr(uop->getSubExpr());
                if (addrEval.second.size() != 1)
                    TODO();

                auto addrExpr = std::move(addrEval.second[0]);
                if (auto addr = addrExpr->tryEvalAsSymbolAddr()) {
                    if (!memoryState_.contains(*addr.value())) {
                        auto symbol =
                            getSymbol(uop->getType(),
                                      addr.value()->addressClone().into_underlying(), startPoint_);
                        memoryState_.write(*addr.value(), std::move(symbol));
                    }
                    return std::move(addr).value().into_underlying();
                } else {
                    ERROR("Expected symbolic::Address in deref, got: " << addrExpr->dump());
                }
            }
        }

        if (auto *mem = dyn_cast<clang::MemberExpr>(lexpr)) {
            auto base = mem->getBase()->IgnoreParenImpCasts();
            auto FD   = llvm::dyn_cast<clang::FieldDecl>(mem->getMemberDecl());
            if (FD == nullptr)
                ERROR("RHS of memberExpr is not a `clang::FieldDecl`?");
            if (FD->getParent() == nullptr)
                ERROR("No parent!");

            auto fieldType = FD->getType();
            auto RD        = FD->getParent();

            if (mem->isArrow()) {
                auto addrEval = evalExpr(base);
                if (addrEval.second.size() != 1)
                    ERROR("This location does not support control flow branches.");
                auto baseExpr = std::move(addrEval.second[0]);
                auto baseAddr = baseExpr->tryEvalAsSymbolAddr();
                if (baseAddr == std::nullopt)
                    ERROR("Expected symbolic::Address for '->' base, got: " << baseExpr->dump());

                if (!memoryState_.contains(*baseAddr.value())) {
                    auto st = makeStructureForRecord(
                        context_.getExprFactory(), RD,
                        baseAddr.value()->addressClone().into_underlying(), startPoint_);
                    memoryState_.write(*baseAddr.value(), std::move(st));
                }
                return makeFieldAddress(context_.getExprFactory(), fieldType, RD,
                                        baseAddr.value()->addressClone().into_underlying(),
                                        FD->getFieldIndex());
            } else {
                auto baseAddr = extractLValue(base);

                if (!memoryState_.contains(*baseAddr)) {
                    auto st = makeStructureForRecord(
                        context_.getExprFactory(), RD, baseAddr->addressClone().into_underlying(),
                        startPoint_);
                    memoryState_.write(*baseAddr, std::move(st));
                }
                return makeFieldAddress(context_.getExprFactory(), fieldType, RD,
                                        baseAddr->addressClone().into_underlying(),
                                        FD->getFieldIndex());
            }
        }

        UNIMPLEMENT("Unsupported LHS expression: " << lexpr->getStmtClassName());
    }

    /**
     * @brief Fetch the symbolic value currently stored for a variable.
     * @param var Variable declaration to query.
     * @return Newly cloned symbolic expression representing the variable's value.
     */
    utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> Path::getVarState(
        const clang::VarDecl *var) const {
        auto canonicalVar = var->getCanonicalDecl();
        auto varIt        = varAddr_.find(canonicalVar);
        if (varIt == varAddr_.end()) {
            // Try to reuse an existing binding with the same identifier (e.g., merged paths with
            // different VarDecl instances). Do not allocate new memory here.
            for (const auto &[vd, addrPtr] : varAddr_) {
                if (vd && vd->getName() == canonicalVar->getName()) {
                    if (auto val = memoryState_.read(*addrPtr))
                        return cloneExpr(context_.getExprFactory(), *val.value());
                }
            }
            ERROR("SymbolValue '" + canonicalVar->getNameAsString() + "' has no allocated address");
        }
        auto addr  = varIt->second.get().get();
        auto value = memoryState_.read(*addr);
        if (value == std::nullopt)
            ERROR("SymbolValue '" + canonicalVar->getNameAsString() +
                  "' has no memory state entry for allocated address");
        return cloneExpr(context_.getExprFactory(), *value.value());
    }

    /// @brief Return a const reference to accumulated path conditions.
    const PathConditions &Path::getPathConditions() const { return pathConditions_; }

    utils::not_null<symbolic::VariableAddress *> Path::allocMemory(const clang::VarDecl *var,
                                                                   bool initSymbolic) {
        auto canonicalVar = var->getCanonicalDecl();
        if (varAddr_.contains(canonicalVar)) {
            // All `symbolic::VariableAddress` built from same canonicalVar are same.
            return varAddr_.at(canonicalVar).get().get();
        }
        auto newAddr = makeVariableAddress(context_.getExprFactory(), var);
        auto rawPtr  = newAddr.get();
        varAddr_.emplace(canonicalVar, std::move(newAddr));

        if (initSymbolic) {
            // Initialize with a symbolic value corresponding to the variable type.
            auto initSym =
                getSymbol(var->getType(), rawPtr->addressClone().into_underlying(), startPoint_);
            memoryState_.write(*rawPtr, std::move(initSym));
        } else {
            // Prevent uninitialized variables.
            memoryState_.write(*rawPtr, buildUnknown(context_.getExprFactory()).into_underlying());
        }
        return rawPtr;
    }

    /**
     * @brief Write a symbolic expression into memory at the provided address.
     * @param addr Target address for the write.
     * @param expr Symbolic value to store.
     */
    void Path::updateMemory(const symbolic::Address &addr,
                            utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> expr) {
        if (expr->isUnknown()) {
            if (auto *fieldAddr = symbolic::dyn_cast<symbolic::FieldAddress>(&addr)) {
                if (fieldAddr->getDefinition() &&
                    fieldAddr->getDefinition()->getNameAsString() == "BigNum" &&
                    fieldAddr->getFieldIndex() == 4) {
                    if (stmtCtx_) {
                        auto loc      = stmtCtx_->getBeginLoc();
                        auto &SM      = context_.getSourceManager();
                        auto presumed = SM.getPresumedLoc(loc);
                        if (presumed.isValid()) {
                            DEBUG("updateMemory Unknown BigNum->data at "
                                  << presumed.getFilename() << ":" << presumed.getLine() << ":"
                                  << presumed.getColumn());
                        }
                    } else {
                        DEBUG("updateMemory Unknown BigNum->data (no stmtCtx)");
                    }
                }
            }
        }
        memoryState_.write(addr, std::move(expr).into_underlying());
    }

    /**
     * @brief Update the symbolic value associated with a variable.
     * @param var Variable declaration being written.
     * @param expr New symbolic value.
     */
    void Path::updateVarState(utils::not_null<const clang::VarDecl *> var,
                              utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> expr) {
        auto canonicalVar = var->getCanonicalDecl();
        auto addrIt       = varAddr_.find(canonicalVar);
        if (addrIt == varAddr_.end())
            ERROR("SymbolValue has no allocated address");

        auto &addr = addrIt->second;
        memoryState_.write(*addr, std::move(expr).into_underlying());
    }

    /**
     * @brief Insert a new predicate into the path condition set.
     * @param cond Condition to add.
     */
    void Path::insertPathCondition(utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> cond) {
        pathConditions_.emplace(context_.getExprFactory().importExpr(*cond));
    }

    /**
     * @brief Create a deep copy of the path, duplicating memory and constraints.
     * @return Newly allocated clone.
     */
    std::unique_ptr<Path> Path::clone() const {
        auto cloned           = std::make_unique<Path>(context_, startPoint_);
        cloned->currentState_ = currentState_;
        for (const auto &entry : varAddr_)
            cloned->varAddr_.emplace(entry.first,
                                     cloneVariableAddress(
                                         context_.getExprFactory().importAddress(*entry.second)));
        cloned->memoryState_ = memoryState_;
        for (const auto &cond : pathConditions_)
            cloned->pathConditions_.emplace(cond);
        if (returnExpr_)
            cloned->returnExpr_.emplace(returnExpr_.value());
        else
            cloned->returnExpr_ = std::nullopt;
        cloned->stmtCtx_ = stmtCtx_;
        return cloned;
    }

    struct CallArgs {
        std::unique_ptr<Path> path;
        std::vector<std::unique_ptr<symbolic::SymbolicExpr>> args;
    };

    /**
     * @brief Bind call arguments to the callee's parameter slots within a fresh path.
     * @param calleePath [in] Path representing the callee's activation.
     * @param FD [in] Function declaration describing parameters.
     * @param args [in] Symbolic argument values to bind.
     */
    void bindParams(Path *calleePath,
                    const clang::FunctionDecl *FD,
                    const std::vector<std::unique_ptr<symbolic::SymbolicExpr>> &args) {
        const unsigned n = FD->getNumParams();
        assert(args.size() == n);

        for (unsigned i = 0; i < n; ++i) {
            const clang::ParmVarDecl *param = FD->getParamDecl(i);
            clang::QualType T               = param->getType();

            auto slot = calleePath->allocMemory(param, /*initSymbolic*/ true);

            if (T->isPointerType()) {
                auto m = args[i]->tryEvalAsSymbolAddr();
                if (!m) {
                    auto &SM     = FD->getASTContext().getSourceManager();
                    auto locStr  = FD->getLocation().isValid() ? FD->getLocation().printToString(SM)
                                                               : "<unknown>";
                    auto funcStr = FD->getQualifiedNameAsString();
                    auto paramStr = param->getNameAsString();
                    ERROR("pointer parameter expects address-like argument: func="
                          << funcStr << " param=" << paramStr << " index=" << i << " type="
                          << T.getAsString() << " loc=" << locStr << " arg=" << args[i]->dump());
                }
                calleePath->updateMemory(*slot, m.value()->addressClone().into_underlying());
            } else if (T->isStructureType()) {
                calleePath->updateMemory(*slot,
                                         cloneExpr(calleePath->getContext().getExprFactory(),
                                                   *args[i]));
            } else if (T->isArrayType()) {
                UNIMPLEMENT("array parameter");
            } else {
                calleePath->updateMemory(*slot,
                                         cloneExpr(calleePath->getContext().getExprFactory(),
                                                   *args[i]));
            }
        }
    }

    /**
     * @brief Evaluate call arguments, expanding paths when argument expressions branch.
     * @param basePath [in] Path on which to start evaluation.
     * @param call [in] Call expression containing arguments.
     * @return Collection of path/argument bundles, one per feasible combination.
     */
    static std::vector<CallArgs> evalCallArgs(Path *basePath, const clang::CallExpr *call) {
        std::vector<CallArgs> args;
        args.push_back({nullptr, {}});

        const unsigned n = call->getNumArgs();
        for (unsigned i = 0; i < n; ++i) {
            const clang::Expr *arg = call->getArg(i);
            std::vector<CallArgs> next;
            for (auto &evalArg : args) {
                Path *p = evalArg.path ? evalArg.path.get() : basePath;

                auto [newPaths, values] = p->evalExpr(arg);
                for (size_t j = 0; j < values.size(); ++j) {
                    CallArgs nc;
                    if (j == 0)
                        nc.path = std::move(evalArg.path);
                    else
                        nc.path = std::move(newPaths[j - 1]).into_underlying();
                    nc.args.reserve(evalArg.args.size() + 1);
                    auto &factory = p->getContext().getExprFactory();
                    for (auto &a : evalArg.args)
                        nc.args.emplace_back(
                            factory.cloneExpr(factory.importExpr(*a)).into_underlying());
                    nc.args.emplace_back(std::move(values[j]).into_underlying());
                    next.emplace_back(std::move(nc));
                }
            }
            // Carry forward all combinations produced so far so later arguments can branch again.
            args = std::move(next);
        }
        for (auto &c : args)
            if (!c.path)
                c.path = basePath->clone();
        return args;
    }

    /**
     * @brief Symbolically evaluate an expression, potentially forking paths on control flow.
     * @param expr [in] Expression node to evaluate.
     * @return Pair of newly forked paths and symbolic results for each outcome.
     *
     * The evaluator handles literals, references, unary/binary operators, casts, conditionals, and
     * calls. Branching expressions (e.g., logical operators) produce multiple results with cloned
     * path states to keep constraints consistent.
     */
    Path::EvalResult Path::evalExpr(const clang::Expr *expr) {
        if (!expr)
            ERROR("Fail to convert an empty clang::Expr");

        EvalResult eval_result =
            llvm::TypeSwitch<const clang::Expr *, EvalResult>(expr)
                .Case<clang::IntegerLiteral>([this](
                                                 const clang::IntegerLiteral *lit) -> EvalResult {
                    DEBUG("evaluating IntegerLiteral...");
                    llvm::APInt ap          = lit->getValue();
                    clang::QualType litType = lit->getType();
                    auto &factory           = context_.getExprFactory();
                    symbolic::Expr result = symbolic::LiteralExpr{factory, 0};

                    if (litType->isBooleanType()) {
                        result = symbolic::LiteralExpr{factory,
                                                       static_cast<bool>(ap.getZExtValue())};
                    } else if (litType->isUnsignedIntegerType()) {
                        if (ap.getBitWidth() <= 8)
                            result = symbolic::LiteralExpr{
                                factory, static_cast<unsigned char>(ap.getZExtValue())};
                        else if (ap.getBitWidth() <= 16)
                            result = symbolic::LiteralExpr{
                                factory, static_cast<unsigned short>(ap.getZExtValue())};
                        else if (ap.getBitWidth() <= 32)
                            result = symbolic::LiteralExpr{
                                factory, static_cast<unsigned int>(ap.getZExtValue())};
                        else if (ap.getBitWidth() <= 64)
                            result = symbolic::LiteralExpr{
                                factory, static_cast<uint64_t>(ap.getZExtValue())};
                        else
                            UNIMPLEMENT("Unsupported unsigned integer literal with bit width > 64: "
                                        << ap.getBitWidth());
                    } else {
                        if (ap.getBitWidth() <= 8)
                            result = symbolic::LiteralExpr{factory,
                                                           static_cast<char>(ap.getSExtValue())};
                        else if (ap.getBitWidth() <= 16)
                            result = symbolic::LiteralExpr{factory,
                                                           static_cast<short>(ap.getSExtValue())};
                        else if (ap.getBitWidth() <= 32)
                            result = symbolic::LiteralExpr{factory,
                                                           static_cast<int>(ap.getSExtValue())};
                        else if (ap.getBitWidth() <= 64)
                            result = symbolic::LiteralExpr{factory,
                                                           static_cast<int64_t>(ap.getSExtValue())};
                        else
                            UNIMPLEMENT("Unsupported signed integer literal with bit width > 64: "
                                        << ap.getBitWidth());
                    }

                    Formulas exprs;
                    exprs.reserve(1);
                    exprs.push_back(factory.cloneExpr(result.handle()));

                    return {std::vector<utils::not_null<std::unique_ptr<Path>>>{},
                            std::move(exprs)};
                })
                .Case<clang::BinaryOperator>([this](
                                                 const clang::BinaryOperator *binOp) -> EvalResult {
                    DEBUG("evaluating BinaryOperator...");
                    // TODO: maybe pack the logic in BO, ArraySub into a function?
                    EvalResult lhs                      = evalExpr(binOp->getLHS());
                    symbolic::BinaryOpExpr::Operator op = symbolic::getBinaryOp(binOp->getOpcode());
                    auto &factory                       = context_.getExprFactory();

                    std::vector<utils::not_null<std::unique_ptr<Path>>> outPaths;
                    Formulas outExprs;

                    size_t lhsCount = lhs.second.size();
                    for (size_t i = 0; i < lhsCount; ++i) {
                        auto lhsExpr    = std::move(lhs.second[i]);
                        Path *path      = (i == 0) ? this : lhs.first[i - 1].get().get();
                        EvalResult rhs  = path->evalExpr(binOp->getRHS());
                        size_t rhsCount = rhs.second.size();

                        for (size_t j = 0; j < rhsCount; ++j) {
                            auto rhsExpr = std::move(rhs.second[j]);

                            symbolic::Expr lhsFacade{factory, factory.importExpr(*lhsExpr)};
                            symbolic::Expr rhsFacade{factory, factory.importExpr(*rhsExpr)};
                            outExprs.emplace_back(
                                factory.cloneExpr(lhsFacade.binary(op, rhsFacade).handle()));

                            if (i == 0 && j == 0)
                                continue;

                            outPaths.emplace_back(j == 0 ? std::move(lhs.first[i - 1])
                                                         : std::move(rhs.first[j - 1]));
                        }
                    }

                    return {std::move(outPaths), std::move(outExprs)};
                })
                .Case<clang::ParenExpr>([this](const clang::ParenExpr *paren) -> EvalResult {
                    DEBUG("evaluating ParenExpr...");
                    return evalExpr(paren->getSubExpr());
                })
                .Case<clang::DeclRefExpr>([this](const clang::DeclRefExpr *declRef) -> EvalResult {
                    DEBUG("evaluating clang::DeclRefExpr...");
                    auto loc = declRef->getExprLoc();
                    if (loc.isValid()) {
                        auto &SM      = context_.getSourceManager();
                        auto presumed = SM.getPresumedLoc(loc);
                        if (presumed.isValid()) {
                            DEBUG("DeclRefExpr location: " << presumed.getFilename() << ":"
                                                           << presumed.getLine() << ":"
                                                           << presumed.getColumn());
                        }
                    }
                    if (const auto *varDecl = dyn_cast<clang::VarDecl>(declRef->getDecl())) {
                        auto varExpr = getVarState(varDecl);
                        if (varDecl->getType()->isPointerType()) {
                            DEBUG("DeclRefExpr pointer value: " << varExpr->dump());
                        }
                        // Ensure pointer-typed variables are represented as addresses, except for
                        // the null pointer constant 0.
                        if (varDecl->getType()->isPointerType() &&
                            !varExpr->tryEvalAsSymbolAddr()) {
                            if (auto *lit =
                                    symbolic::dyn_cast<symbolic::detail::LiteralExprNode>(varExpr.get().get());
                                lit && lit->getLiteralValue() == 0) {
                                // Keep NULL as Int(0) rather than fabricating a pointer.
                            } else {
                                WARN("DeclRefExpr to pointer '"
                                     << varDecl->getNameAsString()
                                     << "' has non-address value; fabricating symbolic address.");
                                auto addr = cloneSymbolAddress(
                                    symbolic::Addr::symbol(context_.getExprFactory(),
                                                           varDecl->getType()->getPointeeType(),
                                                           startPoint_)
                                        .handle());
                                varExpr = std::move(addr);
                            }
                        }
                        Formulas exprs;
                        exprs.push_back(std::move(varExpr));
                        return {std::vector<utils::not_null<std::unique_ptr<Path>>>{},
                                std::move(exprs)};
                    }

                    if (const auto *enumDecl =
                            dyn_cast<clang::EnumConstantDecl>(declRef->getDecl())) {
                        llvm::APSInt value = enumDecl->getInitVal();
                        symbolic::LiteralExpr litExpr{context_.getExprFactory(),
                                                      static_cast<int>(value.getSExtValue())};
                        Formulas exprs;
                        exprs.push_back(cloneExpr(context_.getExprFactory(), *litExpr));
                        return {std::vector<utils::not_null<std::unique_ptr<Path>>>{},
                                std::move(exprs)};
                    }

                    UNIMPLEMENT(
                        "Unsupported clang::Decl type: " << declRef->getDecl()->getDeclKindName());
                    return Path::EvalResult{};
                })
                .Case<clang::ArraySubscriptExpr>(
                    [this](const clang::ArraySubscriptExpr *arrSub) -> EvalResult {
                        DEBUG("evaluating ArraySubscriptExpr...");
                        auto variableAddr = extractLValue(arrSub->getBase());
                        std::unique_ptr<symbolic::SymbolAddress> addr;
                        if (const auto symbol = memoryState_.read(*variableAddr)) {
                            auto ptr = symbolic::dyn_cast<const symbolic::SymbolAddress>(
                                symbol.value().get().get());
                            if (ptr == nullptr)
                                ERROR("Value of ArraySubscriptExpr's base is not "
                                      "'symbolic::SymbolAddress', base "
                                      "is "
                                      "neither "
                                      "pointer nor "
                                      "array?");
                            addr = cloneSymbolAddress(
                                context_.getExprFactory().importAddress(*ptr));
                        } else {
                            ERROR("memoryState_ has no ArraySubscriptExpr's base, base is neither "
                                  "pointer "
                                  "nor "
                                  "array?");
                        }
                        EvalResult idx = evalExpr(arrSub->getIdx());

                        std::vector<utils::not_null<std::unique_ptr<Path>>> outPaths;
                        Formulas outExprs;

                        auto &factory = context_.getExprFactory();
                        auto baseAddrHandle = factory.importAddress(*addr);
                        for (size_t i = 0; i < idx.second.size(); ++i) {
                            symbolic::Addr baseAddr{factory, baseAddrHandle};
                            symbolic::Expr idxExpr{factory,
                                                   factory.importExpr(*idx.second[i])};
                            auto newAddr =
                                baseAddr.withOffset(idxExpr)->addressClone().into_underlying();
                            if (auto value = memoryState_.read(*newAddr); value == std::nullopt) {
                                auto elemType = arrSub->getType();
                                auto symbol =
                                    getSymbol(elemType, newAddr->addressClone().into_underlying(),
                                              startPoint_);
                                memoryState_.write(
                                    *newAddr, cloneExpr(context_.getExprFactory(), *symbol));
                                outExprs.emplace_back(std::move(symbol));
                            } else {
                                outExprs.emplace_back(
                                    cloneExpr(context_.getExprFactory(), *value.value()));
                            }
                            if (i > 0)
                                outPaths.emplace_back(std::move(idx.first[i - 1]));
                        }

                        return {std::move(outPaths), std::move(outExprs)};
                    })
                .Case<clang::CallExpr>([this](const clang::CallExpr *call) -> EvalResult {
                    DEBUG("evaluating clang::CallExpr...");
                    const clang::FunctionDecl *callee = call->getDirectCallee();
                    if (!callee) {
                        Formulas exprs;
                        exprs.emplace_back(
                            buildUnknown(context_.getExprFactory()).into_underlying());
                        std::vector<utils::not_null<std::unique_ptr<Path>>> empty;
                        return Path::EvalResult(std::move(empty), std::move(exprs));
                    }
                    static const std::set<std::string> ignoreNames = {
                        "llvm.dbg.declare", "llvm.lifetime.start", "llvm.lifetime.end", "printf",
                        "__assert_fail"};
                    std::string name = callee->getNameAsString();
                    DEBUG("CallExpr callee name: " << name);
                    std::string lowerName(name);
                    std::transform(
                        lowerName.begin(), lowerName.end(), lowerName.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (ignoreNames.contains(name) ||
                        lowerName.find("assert") != std::string::npos ||
                        lowerName.find("print") != std::string::npos) {
                        Formulas exprs;
                        exprs.emplace_back(
                            buildUnknown(context_.getExprFactory()).into_underlying());
                        std::vector<utils::not_null<std::unique_ptr<Path>>> empty;
                        return Path::EvalResult(std::move(empty), std::move(exprs));
                    }

                    if (name == "__builtin_expect") {
                        if (call->getNumArgs() < 1)
                            UNIMPLEMENT("__builtin_expect expects at least one argument.");
                        auto evalNoBranch = [this](const clang::Expr *e)
                            -> utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> {
                            auto ER = this->evalExpr(e);
                            if (ER.second.size() != 1 || !ER.first.empty())
                                ERROR("__builtin_expect argument must not branch or fork.");
                            return cloneExpr(context_.getExprFactory(), *ER.second[0]);
                        };
                        Formulas exprs;
                        exprs.emplace_back(evalNoBranch(call->getArg(0)));
                        std::vector<utils::not_null<std::unique_ptr<Path>>> empty;
                        return Path::EvalResult(std::move(empty), std::move(exprs));
                    }

                    /*
                    if (name == "__builtin___memset_chk") {
                        // Treat like memset: returns the destination pointer.
                        if (call->getNumArgs() < 3)
                            UNIMPLEMENT("__builtin___memset_chk expects at least 3 arguments.");
                        auto evalNoBranch = [this](const clang::Expr *e)
                            -> utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> {
                            auto ER = this->evalExpr(e);
                            if (ER.second.size() != 1 || !ER.first.empty())
                                ERROR("__builtin___memset_chk arguments must not branch or fork.");
                            return ER.second[0]->clone();
                        };
                        auto destExpr = evalNoBranch(call->getArg(0));
                        Formulas exprs;
                        exprs.emplace_back(std::move(destExpr));
                        std::vector<utils::not_null<std::unique_ptr<Path>>> empty;
                        return Path::EvalResult(std::move(empty), std::move(exprs));
                    }
                    */

                    if (name == "BSL_SAL_Malloc") {
                        // Minimal model: malloc(size) returns a fresh symbolic address, unless
                        // size is a known-zero constant (treated as NULL).
                        if (call->getNumArgs() != 1)
                            UNIMPLEMENT("BSL_SAL_Malloc expects exactly one size argument.");

                        auto evalNoBranch = [this](const clang::Expr *e)
                            -> utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> {
                            auto ER = this->evalExpr(e);
                            if (ER.second.size() != 1 || !ER.first.empty())
                                ERROR("BSL_SAL_Malloc argument must not branch or fork.");
                            return cloneExpr(context_.getExprFactory(), *ER.second[0]);
                        };

                        auto sizeExpr = evalNoBranch(call->getArg(0));
                        if (auto c = sizeExpr->tryEvalAsConstant(); c && *c == 0) {
                            Formulas exprs;
                            symbolic::LiteralExpr zero{context_.getExprFactory(), 0};
                            exprs.emplace_back(cloneExpr(context_.getExprFactory(), *zero));
                            std::vector<utils::not_null<std::unique_ptr<Path>>> empty;
                            return Path::EvalResult(std::move(empty), std::move(exprs));
                        }

                        auto retTy = call->getType();
                        if (!retTy->isPointerType()) {
                            Formulas exprs;
                            exprs.emplace_back(
                                buildUnknown(context_.getExprFactory()).into_underlying());
                            std::vector<utils::not_null<std::unique_ptr<Path>>> empty;
                            return Path::EvalResult(std::move(empty), std::move(exprs));
                        }

                        auto pointAfterCall = symbolic::SourcePoint::fromStmtAfter(
                            call, context_.getSourceManager(), context_.getLangOptions());
                        auto pointeeTy = retTy->getPointeeType();
                        auto addr = cloneSymbolAddress(
                            symbolic::Addr::symbol(context_.getExprFactory(), pointeeTy,
                                                   pointAfterCall)
                                .handle());

                        Formulas exprs;
                        exprs.emplace_back(std::move(addr));
                        std::vector<utils::not_null<std::unique_ptr<Path>>> empty;
                        return Path::EvalResult(std::move(empty), std::move(exprs));
                    }

                    // @WindOctober TODO: wrapped in specific function.
                    if (name == "BSL_SAL_Calloc") {
                        DEBUG("BSL_SAL_Calloc: enter, argc=" << call->getNumArgs());
                        // Extract element type T from any argument that syntactically contains
                        // `sizeof(T)`.

                        std::optional<clang::QualType> elemTyOpt;
                        for (unsigned i = 0; i < call->getNumArgs(); ++i) {
                            if (auto qt = acslg::utils::findSizeofQualType(call->getArg(i))) {
                                elemTyOpt = qt;
                                break;
                            }
                        }
                        if (!elemTyOpt) {
                            UNIMPLEMENT("BSL_SAL_Calloc expects a sizeof(T) in its arguments.");
                        }
                        clang::QualType elemTy = *elemTyOpt;
                        DEBUG("BSL_SAL_Calloc: elemTy=" << elemTy.getAsString());
                        auto pointAfterCall    = symbolic::SourcePoint::fromStmtAfter(
                            call, context_.getSourceManager(), context_.getLangOptions());

                        // Handle builtin scalar pointees (e.g., uint64_t, int64_t, bool, char).
                        if (acslg::utils::isBuiltinScalar(elemTy)) {
                            // Sanity check: exactly one syntactic occurrence of sizeof(T) is
                            // expected.
                            const size_t nSizeofs = acslg::utils::countSizeofInCall(call);
                            DEBUG("BSL_SAL_Calloc: builtin scalar, nSizeofs=" << nSizeofs);
                            if (nSizeofs != 1)
                                UNIMPLEMENT(
                                    "BSL_SAL_Calloc expects exactly one sizeof(T) for builtin T.");

                            // Compute sizeof(T) in bytes according to the target data layout.
                            auto &Ctx = this->context_.getASTContext();
                            const uint64_t sz =
                                static_cast<uint64_t>(Ctx.getTypeSizeInChars(elemTy).getQuantity());

                            // @WindOctober: TODO consider make this lambda function external.
                            // Evaluate both call arguments under the assumption of purity and
                            // single-result semantics.
                            auto evalNoBranch = [this](const clang::Expr *e)
                                -> utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> {
                                auto ER = this->evalExpr(e);
                                if (ER.second.size() != 1 || !ER.first.empty())
                                    ERROR("BSL_SAL_Calloc arguments must not branch or fork.");
                                return cloneExpr(context_.getExprFactory(), *ER.second[0]);
                            };
                            auto a0 = evalNoBranch(call->getArg(0));
                            auto a1 = evalNoBranch(call->getArg(1));
                            DEBUG("BSL_SAL_Calloc: arg0=" << a0->dump());
                            DEBUG("BSL_SAL_Calloc: arg1=" << a1->dump());

                            // Form the total-size expression by multiplying the two arguments.
                            using Op = symbolic::BinaryOpExpr::Operator;
                            auto &factory = context_.getExprFactory();
                            symbolic::Expr arg0Expr{factory, factory.importExpr(*a0)};
                            symbolic::Expr arg1Expr{factory, factory.importExpr(*a1)};
                            auto totalSizeBytes =
                                utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>{
                                    factory.cloneExpr(arg0Expr.binary(Op::Multiply, arg1Expr).handle())
                                        .into_underlying()};

                            // Remove exactly one multiplicative factor equal to sizeof(T) to obtain
                            // the element count. This corresponds to interpreting the product as
                            // "bytes = elems * sizeof(T)".
                            auto lengthInElems = acslg::analyzer::symbolic::strip_sizeof_factor(
                                std::move(totalSizeBytes), sz);
                            DEBUG("BSL_SAL_Calloc: lengthInElems=" << lengthInElems->dump());

                            // Allocate a fresh symbolic address anchored at the current allocation
                            // site.
                            auto addr = cloneSymbolAddress(
                                symbolic::Addr::symbol(context_.getExprFactory(), elemTy,
                                                       pointAfterCall)
                                    .handle());

                            // Note: The length is temporarily omitted since it conceptually
                            // represents the legal bound of accessible memory, rather than a
                            // physically distinct memory segment. If future semantics require
                            // explicit range tracking, this statement can be uncommented to
                            // re-enable length assignment.

                            // addr = addr->withLength(std::move(lengthInElems));

                            // Materialize the first element symbol at the allocated base address.
                            memoryState_.write(*addr, buildUnknown(context_.getExprFactory()));

                            Formulas exprs;
                            exprs.emplace_back(std::move(addr));
                            std::vector<utils::not_null<std::unique_ptr<Path>>> empty;
                            DEBUG("BSL_SAL_Calloc: return (builtin scalar)");
                            return Path::EvalResult(std::move(empty), std::move(exprs));
                        }

                        // Handle structure pointees consistent with the existing symbolic memory
                        // layout.
                        if (elemTy->isStructureType()) {
                            DEBUG("BSL_SAL_Calloc: structure type");
                            auto &factory = context_.getExprFactory();
                            auto zero     = symbolic::LiteralExpr{factory, 0};
                            auto addr     = symbolic::Addr::symbol(elemTy, pointAfterCall, zero)
                                            ->addressClone()
                                            .into_underlying();

                            // Build a Structure whose fields (and nested structs) are Unknown, then
                            // write it.
                            auto structVal = makeStructureForBase(
                                factory, elemTy, addr->addressClone().into_underlying(),
                                pointAfterCall);
                            memoryState_.write(*addr, std::move(structVal));

                            Formulas exprs;
                            exprs.emplace_back(std::move(addr));
                            std::vector<utils::not_null<std::unique_ptr<Path>>> empty;
                            DEBUG("BSL_SAL_Calloc: return (structure)");
                            return Path::EvalResult(std::move(empty), std::move(exprs));
                        }

                        // Reject unsupported pointee categories to preserve soundness.
                        UNIMPLEMENT(
                            "BSL_SAL_Calloc supports pointer-to-structure or builtin scalar only.");
                    }

                    if (name == "BSL_SAL_Free") {
                        // Compute the symbolic source location after the call.
                        auto pointAfterCall = symbolic::SourcePoint::fromStmtAfter(
                            call, context_.getSourceManager(), context_.getLangOptions());

                        // TODO: @WindOctober use a
                        auto evalNoBranch = [this](const clang::Expr *e)
                            -> utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> {
                            auto ER = this->evalExpr(e);
                            if (ER.second.size() != 1 || !ER.first.empty())
                                ERROR("BSL_SAL_Free argument must not branch or fork.");
                            return cloneExpr(context_.getExprFactory(), *ER.second[0]);
                        };

                        // Expect exactly one argument: the pointer to free.
                        if (call->getNumArgs() != 1)
                            UNIMPLEMENT("BSL_SAL_Free expects exactly one pointer argument.");
                        auto p = evalNoBranch(call->getArg(0));

                        // free(NULL) → no-op.
                        if (auto c = p->tryEvalAsConstant(); c && *c == 0) {
                            std::vector<utils::not_null<std::unique_ptr<Path>>> empty;
                            Formulas exprs;
                            return Path::EvalResult(std::move(empty), std::move(exprs));
                        }

                        // The argument must be a symbolic address.
                        auto maybeAddr = p->tryEvalAsSymbolAddr();
                        if (!maybeAddr)
                            UNIMPLEMENT("BSL_SAL_Free argument must be a valid pointer value.");

                        // Normalize to base address (offset = 0) for consistent memory handling.
                        auto freedAddr = std::move(*maybeAddr);
                        auto &factory = context_.getExprFactory();
                        symbolic::Addr normalizedFreedAddr{
                            factory, factory.importAddress(*freedAddr)};
                        auto normalizedFreedAddrHandle =
                            normalizedFreedAddr
                                .withOffset(symbolic::LiteralExpr{
                                    factory, static_cast<int64_t>(
                                                 symbolic::SymbolAddress::ZERO_OFFSET)})
                                .handle();
                        freedAddr = cloneSymbolAddress(normalizedFreedAddrHandle);

                        // Overwrite freed memory with an UnknownExpr (symbolic tombstone).
                        // This prevents later reads from reusing stale symbolic values.
                        memoryState_.write(*freedAddr, buildUnknown(context_.getExprFactory()));

                        // Record the freed address as a formula result (optional, for tracking).
                        Formulas exprs;
                        exprs.emplace_back(std::move(freedAddr).into_underlying());

                        std::vector<utils::not_null<std::unique_ptr<Path>>> empty;
                        return Path::EvalResult(std::move(empty), std::move(exprs));
                    }

                    auto modelMemcpy = [&](const clang::Expr *destArg, const clang::Expr *srcArg,
                                           const clang::Expr *countArg) -> EvalResult {
                        auto evalNoBranch = [this](const clang::Expr *e)
                            -> utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> {
                            auto ER = this->evalExpr(e);
                            if (ER.second.size() != 1 || !ER.first.empty())
                                ERROR("memcpy arguments must not branch or fork.");
                            return cloneExpr(context_.getExprFactory(), *ER.second[0]);
                        };

                        auto destExpr  = evalNoBranch(destArg);
                        auto srcExpr   = evalNoBranch(srcArg);
                        auto countExpr = evalNoBranch(countArg);

                        DEBUG("memcpy dest expr: " << destExpr->dump());

                        auto destAddr = destExpr->tryEvalAsSymbolAddr();
                        auto srcAddr  = srcExpr->tryEvalAsSymbolAddr();
                        if (!srcAddr)
                            UNIMPLEMENT("memcpy expects a pointer source argument.");

                        clang::QualType elemTy = srcAddr.value()->getPointeeType();
                        if (elemTy->isVoidType() || elemTy->isIncompleteType())
                            UNIMPLEMENT("memcpy requires a non-void pointee type for modeling.");

                        if (!destAddr)
                            UNIMPLEMENT("memcpy expects a pointer destination argument.");

                        auto &Ctx = this->context_.getASTContext();
                        const uint64_t sz =
                            static_cast<uint64_t>(Ctx.getTypeSizeInChars(elemTy).getQuantity());
                        if (sz == 0)
                            UNIMPLEMENT("memcpy requires a non-zero element size.");

                        auto lengthExpr = cloneExpr(context_.getExprFactory(), *countExpr);
                        bool noCopy     = false;
                        if (auto *lit =
                                symbolic::dyn_cast<symbolic::detail::LiteralExprNode>(lengthExpr.get().get())) {
                            const auto raw = static_cast<uint64_t>(lit->getLiteralValue());
                            if (raw == 0) {
                                noCopy = true;
                            } else if (sz > 1) {
                                if (raw % sz != 0)
                                    UNIMPLEMENT("memcpy size is not a multiple of element size.");
                                symbolic::LiteralExpr lengthLiteral{
                                    context_.getExprFactory(), raw / sz};
                                lengthExpr = cloneExpr(context_.getExprFactory(), *lengthLiteral);
                            }
                        } else if (sz > 1) {
                            lengthExpr = acslg::analyzer::symbolic::strip_sizeof_factor(
                                std::move(lengthExpr), sz);
                        }

                        Formulas exprs;
                        exprs.emplace_back(cloneExpr(context_.getExprFactory(), *destExpr));
                        std::vector<utils::not_null<std::unique_ptr<Path>>> empty;
                        if (noCopy)
                            return Path::EvalResult(std::move(empty), std::move(exprs));

                        auto &factory = context_.getExprFactory();
                        symbolic::Addr destBase{factory,
                                                factory.importAddress(*destAddr.value())};
                        symbolic::Expr length{factory, factory.importExpr(*lengthExpr)};
                        auto destRange =
                            destBase.withLength(length)->addressClone().into_underlying();

                        symbolic::Addr srcBase{factory,
                                               factory.importAddress(*srcAddr.value())};
                        auto srcBaseWithoutLength = srcBase.withoutLength();
                        symbolic::SymbolAddress::RangeIndex rangeIndex{"i"};
                        symbolic::Expr rangeIndexExpr{factory, factory.importExpr(rangeIndex)};
                        auto srcIndexed =
                            srcBaseWithoutLength.withAddedOffset(rangeIndexExpr)
                                ->addressClone()
                                .into_underlying();

                        auto valueExpr = symbolic::getSymbol(
                            elemTy, srcIndexed->addressClone().into_underlying(), startPoint_);
                        memoryState_.write(*destRange, std::move(valueExpr));
                        return Path::EvalResult(std::move(empty), std::move(exprs));
                    };

                    if (name == "memcpy" || name == "__builtin_memcpy") {
                        if (call->getNumArgs() < 3)
                            UNIMPLEMENT("memcpy expects three arguments.");
                        return modelMemcpy(call->getArg(0), call->getArg(1), call->getArg(2));
                    }

                    if (name == "memcpy_s") {
                        if (call->getNumArgs() < 4)
                            UNIMPLEMENT("memcpy_s expects four arguments.");
                        return modelMemcpy(call->getArg(0), call->getArg(2), call->getArg(3));
                    }

                    if (name == "memset_s") {
                        if (call->getNumArgs() < 4)
                            UNIMPLEMENT("memset_s expects four arguments.");
                        auto evalNoBranch = [this](const clang::Expr *e)
                            -> utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> {
                            auto ER = this->evalExpr(e);
                            if (ER.second.size() != 1 || !ER.first.empty())
                                ERROR("memset_s arguments must not branch or fork.");
                            return cloneExpr(context_.getExprFactory(), *ER.second[0]);
                        };

                        auto destExpr  = evalNoBranch(call->getArg(0));
                        auto countExpr = evalNoBranch(call->getArg(3));
                        auto pointAfterCall = symbolic::SourcePoint::fromStmtAfter(
                            call, context_.getSourceManager(), context_.getLangOptions());

                        // If dest is NULL or count is 0, just return dest.
                        if (auto c = destExpr->tryEvalAsConstant(); c && *c == 0) {
                            Formulas exprs;
                            exprs.emplace_back(std::move(destExpr));
                            std::vector<utils::not_null<std::unique_ptr<Path>>> empty;
                            return Path::EvalResult(std::move(empty), std::move(exprs));
                        }

                        auto destAddr = destExpr->tryEvalAsSymbolAddr();
                        if (!destAddr)
                            UNIMPLEMENT("memset_s expects a pointer destination argument.");

                        clang::QualType elemTy = destAddr.value()->getPointeeType();
                        if (auto argTy = call->getArg(0)->IgnoreParenImpCasts()->getType();
                            argTy->isPointerType()) {
                            elemTy = argTy->getPointeeType();
                        }
                        if (elemTy->isVoidType() || elemTy->isIncompleteType())
                            elemTy = this->context_.getASTContext().UnsignedCharTy;

                        auto &Ctx = this->context_.getASTContext();
                        const uint64_t sz =
                            static_cast<uint64_t>(Ctx.getTypeSizeInChars(elemTy).getQuantity());
                        if (sz == 0)
                            UNIMPLEMENT("memset_s requires a non-zero element size.");

                        auto lengthExpr = cloneExpr(context_.getExprFactory(), *countExpr);
                        bool noSet      = false;
                        if (auto *lit =
                                symbolic::dyn_cast<symbolic::detail::LiteralExprNode>(lengthExpr.get().get())) {
                            const auto raw = static_cast<uint64_t>(lit->getLiteralValue());
                            if (raw == 0) {
                                noSet = true;
                            } else if (sz > 1) {
                                if (raw % sz != 0)
                                    UNIMPLEMENT("memset_s size is not a multiple of element size.");
                                symbolic::LiteralExpr lengthLiteral{
                                    context_.getExprFactory(), raw / sz};
                                lengthExpr = cloneExpr(context_.getExprFactory(), *lengthLiteral);
                            }
                        } else if (sz > 1) {
                            lengthExpr = acslg::analyzer::symbolic::strip_sizeof_factor(
                                std::move(lengthExpr), sz);
                        }

                        Formulas exprs;
                        exprs.emplace_back(cloneExpr(context_.getExprFactory(), *destExpr));
                        std::vector<utils::not_null<std::unique_ptr<Path>>> empty;
                        if (noSet)
                            return Path::EvalResult(std::move(empty), std::move(exprs));

                        if (elemTy->isStructureType()) {
                            auto &factory = context_.getExprFactory();
                            symbolic::Addr destAddrFacade{
                                factory, factory.importAddress(*destAddr.value())};
                            auto destBase =
                                destAddrFacade.withoutLength()->addressClone().into_underlying();
                            auto structVal = makeStructureForBase(
                                factory, elemTy, destBase->addressClone().into_underlying(),
                                pointAfterCall);
                            memoryState_.write(*destBase, std::move(structVal));
                            return Path::EvalResult(std::move(empty), std::move(exprs));
                        }

                        auto &factory = context_.getExprFactory();
                        symbolic::Addr destBase{factory,
                                                factory.importAddress(*destAddr.value())};
                        symbolic::Expr length{factory, factory.importExpr(*lengthExpr)};
                        auto destRange =
                            destBase.withLength(length)->addressClone().into_underlying();
                        memoryState_.write(
                            *destRange,
                            buildUnknown(context_.getExprFactory()).into_underlying());
                        return Path::EvalResult(std::move(empty), std::move(exprs));
                    }

                    std::vector<utils::not_null<std::unique_ptr<Path>>> outPaths;
                    Formulas outExprs;

                    const clang::FunctionDecl *calleeWithBody = callee;
                    if (!calleeWithBody->hasBody()) {
                        if (auto *imported =
                                ctu::importDefinitionIfAvailable(calleeWithBody, context_)) {
                            calleeWithBody = imported;
                        }
                    }
                    if (calleeWithBody) {
                        if (auto *def = calleeWithBody->getDefinition())
                            calleeWithBody = def;
                    }

                    if (!calleeWithBody->hasBody()) {
                        if (calleeWithBody->getNumParams() == 0) {
                            outExprs.push_back(
                                buildUnknown(context_.getExprFactory()).into_underlying());
                            return {std::move(outPaths), std::move(outExprs)};
                        }
                        UNIMPLEMENT("Calling a function \"" + calleeWithBody->getNameAsString() +
                                    "\" with no visible body.");
                    }

                    auto callArgs = evalCallArgs(this, call);
                    auto callerSnapshot =
                        this->clone(); // keep caller locals/ctx intact across inline call
                    bool firstTaken = false;
                    for (size_t k = 0; k < callArgs.size(); ++k) {
                        auto initPath = std::move(callArgs[k].path);
                        bindParams(initPath.get(), calleeWithBody, callArgs[k].args);

                        auto func = std::make_unique<ACSLFunction>(calleeWithBody);
                        ProgramState calleeState(std::move(initPath), std::move(func), context_);
                        calleeState.step(calleeWithBody->getBody());

                        auto produced = calleeState.takeAllPaths();
                        for (size_t i = 0; i < produced.size(); ++i) {
                            auto p = std::move(produced[i]);
                            p->setPathState(PathState::Step);
                            // Restore caller locals/params into callee return path.
                            for (const auto &[vd, addrPtr] : callerSnapshot->varAddr_) {
                                if (!p->varAddr_.contains(vd)) {
                                    p->varAddr_.emplace(
                                        vd, cloneVariableAddress(
                                                p->context_.getExprFactory().importAddress(
                                                    *addrPtr)));
                                }
                                if (auto val = callerSnapshot->memoryState_.read(*addrPtr)) {
                                    auto &dstAddr = p->varAddr_.at(vd);
                                    p->memoryState_.write(
                                        *dstAddr, cloneExpr(p->context_.getExprFactory(),
                                                            *val.value()));
                                }
                            }
                            // Restore statement context back to the caller.
                            // Do not merge with callerSnapshot here; keep callee state as-is.
                            p->stmtCtx_ = callerSnapshot->stmtCtx_;
                            utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> ret =
                                p->getReturnExpr()
                                    ? cloneExpr(p->context_.getExprFactory(),
                                                *p->getReturnExpr().value())
                                    : buildUnknown(p->context_.getExprFactory());

                            if (!firstTaken) {
                                this->swap(*p);
                                outExprs.emplace_back(std::move(ret));
                                firstTaken = true;
                            } else {
                                outExprs.emplace_back(std::move(ret));
                                outPaths.emplace_back(std::move(p));
                            }
                        }
                    }

                    if (!firstTaken) {
                        outExprs.emplace_back(
                            buildUnknown(context_.getExprFactory()).into_underlying());
                    }

                    return {std::move(outPaths), std::move(outExprs)};
                })
                .Case<clang::ConditionalOperator>(
                    [this](const clang::ConditionalOperator *condOp) -> EvalResult {
                        DEBUG("evaluating ConditionalOperator...");
                        EvalResult cond = evalExpr(condOp->getCond());

                        std::vector<utils::not_null<std::unique_ptr<Path>>> outPaths;
                        Formulas outExprs;

                        for (size_t i = 0; i < cond.second.size(); ++i) {
                            Path *condPath = (i == 0) ? this : cond.first[i - 1].get().get();
                            auto condExpr  = std::move(cond.second[i]);

                            // false branch
                            auto falsePath   = condPath->clone();
                            auto &factory    = context_.getExprFactory();
                            symbolic::Expr condFacade{factory, factory.importExpr(*condExpr)};
                            auto negatedCond = factory.cloneExpr(condFacade.logicalNot().handle());
                            falsePath->insertPathCondition(std::move(negatedCond));

                            EvalResult falseVal = falsePath->evalExpr(condOp->getFalseExpr());

                            // true branch
                            auto truePath = condPath;
                            truePath->insertPathCondition(std::move(condExpr));

                            EvalResult trueVal = truePath->evalExpr(condOp->getTrueExpr());

                            // merge results
                            for (size_t j = 0; j < trueVal.second.size(); ++j) {
                                outExprs.emplace_back(std::move(trueVal.second[j]));
                                if (j > 0)
                                    outPaths.emplace_back(std::move(trueVal.first[j - 1]));
                            }
                            for (size_t j = 0; j < falseVal.second.size(); ++j) {
                                outExprs.emplace_back(std::move(falseVal.second[j]));
                                if (j == 0)
                                    outPaths.emplace_back(std::move(falsePath));
                                else
                                    outPaths.emplace_back(std::move(falseVal.first[j - 1]));
                            }
                        }

                        return {std::move(outPaths), std::move(outExprs)};
                    })
                .Case<clang::UnaryOperator>([this](const clang::UnaryOperator *uop) -> EvalResult {
                    DEBUG("evaluating UnaryOperator...");
                    auto operand = evalExpr(uop->getSubExpr());
                    std::vector<utils::not_null<std::unique_ptr<Path>>> outPaths;
                    Formulas outExprs;

                    symbolic::UnaryOpExpr::Operator op;
                    switch (uop->getOpcode()) {
                        using enum clang::UnaryOperatorKind;
                        using enum symbolic::UnaryOpExpr::Operator;
                        case UO_Plus: op = Plus; break;
                        case UO_Minus: op = Minus; break;
                        case UO_LNot: op = LogicalNot; break;
                        case UO_Not: op = BitwiseNot; break;
                        case UO_PreInc: op = PreInc; break;
                        case UO_PreDec: op = PreDec; break;
                        case UO_PostInc: op = PostInc; break;
                        case UO_PostDec: op = PostDec; break;
                        case UO_AddrOf: op = AddrOf; break;
                        case UO_Deref: op = Dereference; break;
                        default:
                            UNIMPLEMENT("Unsupported UnaryOperator: "
                                        << uop->getOpcodeStr(uop->getOpcode()).str());
                    }

                    for (size_t i = 0; i < operand.second.size(); ++i) {
                        Path *path  = (i == 0 ? this : operand.first[i - 1].get().get());
                        auto unExpr = std::move(operand.second[i]);

                        // Prevent misuse by not capturing this and operand.
                        [this, &path, &unExpr, &op, &outExprs, &uop]() {
                            using enum symbolic::UnaryOpExpr::Operator;
                            if (op == PreInc || op == PostInc || op == PreDec || op == PostDec) {
                                // ++x / x++ / --x / x--
                                auto addr = path->extractLValue(uop->getSubExpr());
                                // old value
                                auto oldVal = path->memoryState_.read(*addr);
                                if (oldVal == std::nullopt)
                                    ERROR("memoryState_ doesn't contain addr.");
                                // compute new = old +/- 1
                                auto &factory = context_.getExprFactory();
                                symbolic::LiteralExpr one{factory, 1};
                                auto binOp  = (op == PreInc || op == PostInc)
                                                  ? symbolic::BinaryOpExpr::Operator::Add
                                                  : symbolic::BinaryOpExpr::Operator::Subtract;
                                symbolic::Expr oldValExpr{
                                    factory, factory.importExpr(*oldVal.value())};
                                auto newValExpr = oldValExpr.binary(binOp, one);
                                auto newVal     = cloneExpr(factory, *newValExpr);
                                // return pre vs post
                                if (op == PreInc || op == PreDec)
                                    outExprs.emplace_back(cloneExpr(factory, *newValExpr));
                                else {
                                    outExprs.emplace_back(cloneExpr(factory, *oldVal.value()));
                                }
                                // Writing back first will cause oldVal to become dangling.
                                // write back
                                path->memoryState_.write(*addr, std::move(newVal));
                            } else if (op == Dereference) {
                                // *x
                                auto addr = unExpr->tryEvalAsSymbolAddr();
                                if (addr == std::nullopt)
                                    ERROR("Expected symbolic::Address, got: " << unExpr->dump());
                                if (auto value = path->memoryState_.read(*addr.value());
                                    value == std::nullopt) {
                                    auto symbol =
                                        getSymbol(uop->getType(),
                                                  addr.value()->addressClone().into_underlying(),
                                                  startPoint_);
                                    path->memoryState_.write(
                                        *addr.value(), cloneExpr(path->context_.getExprFactory(),
                                                                 *symbol));
                                    outExprs.emplace_back(std::move(symbol));
                                } else {
                                    outExprs.emplace_back(
                                        cloneExpr(path->context_.getExprFactory(),
                                                  *value.value()));
                                }
                            } else if (op == AddrOf) {
                                // &x
                                auto addr = path->extractLValue(uop->getSubExpr());
                                outExprs.emplace_back(addr->addressClone().into_underlying());
                            } else {
                                auto &factory = context_.getExprFactory();
                                symbolic::Expr operandExpr{factory, factory.importExpr(*unExpr)};
                                outExprs.emplace_back(
                                    factory.cloneExpr(operandExpr.unary(op).handle()));
                            }
                        }();
                        if (i > 0)
                            outPaths.emplace_back(std::move(operand.first[i - 1]));
                    }

                    return {std::move(outPaths), std::move(outExprs)};
                })
                .Case<clang::CastExpr>([this](const clang::CastExpr *castExpr) -> EvalResult {
                    DEBUG("evaluating CastExpr...");
                    auto sub = evalExpr(castExpr->getSubExpr());
                    if (castExpr->getType()->isStructureType())
                        return sub;

                    auto targetType = symbolic::deriveType(castExpr->getType());
                    auto &factory   = context_.getExprFactory();

                    for (auto &subExpr : sub.second)
                        subExpr = factory.cloneExpr(
                            factory.withValType(factory.importExpr(*subExpr), targetType));

                    return {std::move(sub.first), std::move(sub.second)};
                })
                .Case<clang::MemberExpr>([this](const clang::MemberExpr *memberExpr) -> EvalResult {
                    DEBUG("evaluating MemberExpr...");
                    auto FD = dyn_cast_if_present<clang::FieldDecl>(memberExpr->getMemberDecl());
                    if (FD == nullptr)
                        UNREACHABLE();
                    if (!FD->getParent())
                        UNREACHABLE();
                    auto RD         = FD->getParent();
                    EvalResult base = evalExpr(memberExpr->getBase());
                    if (base.second.size() != 1)
                        ERROR("No control flow branching permitted within a pointer-to-member "
                              "expression.");
                    if (memberExpr->isArrow()) {
                        DEBUG("MemberExpr base value: " << base.second[0]->dump());
                    }

                    std::unique_ptr<symbolic::Structure> st;
                    auto baseExpr = std::move(base.second[0]).into_underlying();
                    if (memberExpr->isArrow()) {
                        auto baseAddr = baseExpr->tryEvalAsSymbolAddr();
                        if (baseAddr == std::nullopt) {
                            WARN("LHS of '->' is not an address; fabricating symbolic pointer to "
                                 "continue.");
                            auto newAddr = cloneSymbolAddress(
                                symbolic::Addr::symbol(
                                    context_.getExprFactory(),
                                    memberExpr->getBase()->getType()->getPointeeType(),
                                    startPoint_)
                                    .handle());
                            baseAddr = utils::not_null<std::unique_ptr<symbolic::SymbolAddress>>{
                                std::move(newAddr)};
                        }
                        auto val = memoryState_.read(*baseAddr.value());
                        DEBUG("MemberExpr base in memory: " << (val ? "yes" : "no"));
                        if (val == std::nullopt) {
                            st = makeStructureForRecord(
                                context_.getExprFactory(), RD,
                                baseAddr.value()->addressClone().into_underlying(), startPoint_);
                            memoryState_.write(*baseAddr.value(),
                                               cloneExpr(context_.getExprFactory(), *st));
                        } else if (auto stVal = symbolic::dyn_cast<const symbolic::Structure>(
                                       val.value().get().get())) {
                            st = cloneStructure(context_.getExprFactory().importExpr(*stVal));
                        } else {
                            ERROR("Dereferenced value is not a structure");
                        }
                    } else {
                        if (auto stExpr = symbolic::dyn_cast<symbolic::Structure>(baseExpr))
                            st = std::move(stExpr);
                        else
                            ERROR("LHS of '.' is not a structure");
                    }

                    size_t idx = FD->getFieldIndex();
                    if (idx >= st->getNumFields())
                        UNREACHABLE();
                    auto fieldValue = cloneExpr(context_.getExprFactory(), *st->getFieldValue(idx));
                    DEBUG("MemberExpr field " << FD->getNameAsString() << " idx=" << idx
                                              << " value: " << fieldValue->dump());
                    EvalResult result{};
                    result.second.push_back(std::move(fieldValue));
                    return result;
                })

                .Case<clang::ConstantExpr>([this](const clang::ConstantExpr *ce) -> EvalResult {
                    DEBUG("evaluating ConstantExpr...");
                    llvm::APSInt v;
                    bool ok = false;
                    if (ce->getResultAPValueKind() == clang::APValue::Int) {
                        v  = ce->getResultAsAPSInt();
                        ok = (v.getBitWidth() != 0);
                    }
                    if (!ok) {
                        EvalResult sub = evalExpr(ce->getSubExpr());
                        if (sub.second.size() != 1)
                            ERROR("ConstantExpr subExpr produced multiple results");
                        auto resultTy = symbolic::deriveType(ce->getType());
                        auto &factory = context_.getExprFactory();
                        symbolic::Expr subExpr{factory, factory.importExpr(*sub.second[0])};
                        sub.second[0] = factory.cloneExpr(subExpr.withType(resultTy).handle());
                        return {std::move(sub.first), std::move(sub.second)};
                    }
                    auto resultTy = symbolic::deriveType(ce->getType());
                    auto &factory = context_.getExprFactory();
                    auto lit =
                        v.isSigned()
                            ? symbolic::LiteralExpr{factory,
                                                    static_cast<int64_t>(v.getSExtValue())}
                            : symbolic::LiteralExpr{factory,
                                                    static_cast<uint64_t>(v.getZExtValue())};
                    EvalResult r;
                    r.second.emplace_back(factory.cloneExpr(lit.withType(resultTy).handle()));
                    return r;
                })
                .Case<clang::UnaryExprOrTypeTraitExpr>(
                    [this](const clang::UnaryExprOrTypeTraitExpr *uett) -> EvalResult {
                        DEBUG("evaluating UnaryExprOrTypeTraitExpr...");

                        // Obtain the AST context, which encodes target-dependent size/alignment.
                        auto &ctx = this->context_.getASTContext();

                        // Resolve the operand type for either type- or expression-based arguments.
                        auto operandQT = [&]() -> clang::QualType {
                            return uett->isArgumentType() ? uett->getArgumentType()
                                                          : uett->getArgumentExpr()->getType();
                        }();

                        uint64_t value = 0;
                        switch (uett->getKind()) {
                            using enum clang::UnaryExprOrTypeTrait;
                            case UETT_SizeOf:
                                // Size is measured in bytes per the target data layout.
                                value = static_cast<uint64_t>(
                                    ctx.getTypeSizeInChars(operandQT).getQuantity());
                                break;
                            case UETT_AlignOf:
                            case UETT_PreferredAlignOf:
                                // Alignment is reported in bytes as required by the ABI.
                                value = static_cast<uint64_t>(
                                    ctx.getTypeAlignInChars(operandQT).getQuantity());
                                break;
                            default:
                                UNIMPLEMENT("Unsupported UnaryExprOrTypeTraitExpr kind: "
                                            << static_cast<int>(uett->getKind()));
                        }

                        // Materialize a literal of the expression’s result type (typically size_t).
                        auto resultTy = symbolic::deriveType(uett->getType());
                        auto &factory = context_.getExprFactory();
                        auto lit      = symbolic::LiteralExpr{factory, value};

                        EvalResult r;
                        r.second.emplace_back(factory.cloneExpr(lit.withType(resultTy).handle()));
                        return r;
                    })

                .Default([](const clang::Expr *e) -> EvalResult {
                    UNIMPLEMENT("Unsupported clang::Expr type: " << e->getStmtClassName());
                    return Path::EvalResult{};
                });
        return eval_result;
    }

    std::string Path::dump() const {
        using namespace utils::dump_fmt;

        std::ostringstream oss;

        // ---- Header: Path + State ------------------------------------------------
        oss << type("Path") << " { " << key("state") << "=" << [&]() {
            switch (currentState_) {
                case Path::PathState::Step: return "Step";
                case Path::PathState::Continue: return "Continue";
                case Path::PathState::Break: return "Break";
                case Path::PathState::Return: return "Return";
                default: return "Unknown";
            }
        }() << " }\n";

        // ---- Return Expression ---------------------------------------------------
        oss << "  " << key("return") << ": ";
        if (returnExpr_) {
            oss << returnExpr_.value()->dump() << "\n";
        } else {
            oss << hint("null") << "\n";
        }

        // ---- Path Conditions -----------------------------------------------------
        oss << "  " << key("conditions") << ":\n";
        size_t condIdx = 0;
        for (const auto &cond : pathConditions_) {
            oss << "    "
                << "[" << lit(std::to_string(condIdx++)) << "] " << cond->dump() << "\n";
        }
        if (pathConditions_.empty()) {
            oss << "    " << hint("<empty>") << "\n";
        }

        // ---- SymbolValue -> Address -> Value mapping -------------------------------
        oss << "  " << key("var→addr→value") << ":\n";
        for (auto &[varDecl, addr] : varAddr_) {
            std::string name;
            if (auto opt = context_.getDeclInfo(varDecl)) {
                std::tie(name, std::ignore, std::ignore, std::ignore, std::ignore) = *opt;
            }

            oss << "    @" << (name.empty() ? hint("<unnamed>") : path(name)) << " " << op("->")
                << " " << addr->dump();

            if (auto value = memoryState_.read(*addr)) {
                oss << " " << op("->") << " " << value.value()->dump();
            } else {
                oss << " " << op("->") << " " << hint("null");
            }
            oss << "\n";
        }
        if (varAddr_.empty()) {
            oss << "    " << hint("<empty>") << "\n";
        }

        // ---- Memory State flat view ---------------------------------------------
        oss << "  " << key("memory") << ":\n";
        {
            bool any = false;
            for (auto &&[addr, value] : memoryState_.flat()) {
                any = true;
                oss << "    " << addr.get().dump() << " " << op("->") << " " << value->dump()
                    << "\n";
            }
            if (!any) {
                oss << "    " << hint("<empty>") << "\n";
            }
        }

        // ---- Statement Context (source snippet) ---------------------------------
        if (stmtCtx_) {
            if (auto opt = context_.getStmtInfo(stmtCtx_)) {
                const auto &sourceText = std::get<0>(*opt);
                if (!sourceText.empty()) {
                    oss << "  " << key("stmt") << ": " << path(sourceText.str()) << "\n";
                }
            }
        }

        return oss.str();
    }

    std::string ProgramState::dump() const {
        using namespace utils::dump_fmt;

        std::ostringstream oss;

        if (paths_.empty()) {
            oss << type("ProgramState") << " " << hint("<no paths>") << "\n";
            return oss.str();
        }

        oss << type("ProgramState") << " " << key("paths") << "="
            << lit(std::to_string(paths_.size())) << "\n";

        for (size_t i = 0; i < paths_.size(); ++i) {
            oss << "  " << key("path") << "[" << lit(std::to_string(i)) << "]\n";
            std::istringstream is(paths_[i]->dump());
            std::string line;
            while (std::getline(is, line)) {
                if (!line.empty())
                    oss << "    " << line << "\n";
                else
                    oss << "\n";
            }
        }

        return oss.str();
    }

    bool Path::isUnchanged(const symbolic::Address &addr, const Path &since) const {
        if (auto symbolAddr = symbolic::dyn_cast<const symbolic::SymbolAddress>(&addr)) {
            if (symbolAddr->getLength())
                return false;
        }
        auto value = memoryState_.read(addr);
        if (value == std::nullopt)
            return true; // Assume it has not been accessed yet.

        auto oldValue = since.getMemoryState().read(addr);
        if (oldValue)
            return *value.value() == *oldValue.value();
        return isFrom(*value.value(), addr, since.getStartPoint());
    }

    bool Path::is_point_to_structure(const symbolic::Address &addr) const {
        if (auto symbolAddr = symbolic::dyn_cast<const symbolic::SymbolAddress>(&addr)) {
            if (symbolAddr->getLength())
                return false;
        }
        auto opt = memoryState_.read(addr);
        if (!opt)
            return false;

        const symbolic::SymbolicExpr *expr = opt.value().get().get();
        return symbolic::isa<symbolic::Structure>(expr);
    }

    MemoryModel::MemoryModel() {
        factory_ = &symbolic::ExprFactoryScope::current();
    }

    MemoryModel::MemoryModel(symbolic::ExprFactory &factory) : factory_(&factory) {}

    auto MemoryModel::importValue(const symbolic::SymbolicExpr &value) -> StoredValue {
        return factory().importExpr(value);
    }

    auto MemoryModel::copyStoredValueFrom(const MemoryModel &other, StoredValue value)
        -> StoredValue {
        if (&factory() == &other.factory())
            return value;
        return importValue(*value);
    }

    MemoryModel::MemoryModel(const MemoryModel &other) : MemoryModel(other.factory()) {
        for (auto &[addr, value] : other.memoryMap_variableAddr_) {
            memoryMap_variableAddr_.emplace(addr, copyStoredValueFrom(other, value));
        }
        for (auto &[baseInfo, rangeValueMap] : other.memoryMap_constantRange_) {
            auto &mapToFill = memoryMap_constantRange_[baseInfo];
            for (auto &[range, value] : rangeValueMap) {
                mapToFill.emplace(range, copyStoredValueFrom(other, value));
            }
        }

        for (auto &[baseHash, addrValueMap] : other.memoryMap_symbolicRange_) {
            auto &mapToFill = memoryMap_symbolicRange_[baseHash];
            for (auto &[addr, value] : addrValueMap) {
                mapToFill.emplace(addr, copyStoredValueFrom(other, value));
            }
        }
    }

    MemoryModel &MemoryModel::operator=(const MemoryModel &other) {
        if (this == &other)
            return *this;

        clear();

        for (auto &[addr, value] : other.memoryMap_variableAddr_) {
            memoryMap_variableAddr_.emplace(addr, copyStoredValueFrom(other, value));
        }
        for (auto &[baseInfo, rangeValueMap] : other.memoryMap_constantRange_) {
            auto &mapToFill = memoryMap_constantRange_[baseInfo];
            for (auto &[range, value] : rangeValueMap) {
                mapToFill.emplace(range, copyStoredValueFrom(other, value));
            }
        }

        for (auto &[baseHash, addrValueMap] : other.memoryMap_symbolicRange_) {
            auto &mapToFill = memoryMap_symbolicRange_[baseHash];
            for (auto &[addr, value] : addrValueMap) {
                mapToFill.emplace(addr, copyStoredValueFrom(other, value));
            }
        }

        return *this;
    }

    std::optional<utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>> MemoryModel::read(
        const symbolic::Address &addr) const {
        if (auto varAddr = symbolic::dyn_cast<const symbolic::VariableAddress>(&addr)) {
            if (memoryMap_variableAddr_.contains(*varAddr))
                return memoryMap_variableAddr_.at(*varAddr)->clone();
            return std::nullopt;
        } else if (auto symbolAddr = symbolic::dyn_cast<const symbolic::SymbolAddress>(&addr)) {
            auto baseInfo = symbolAddr->getBaseInfo();

            if (memoryMap_constantRange_.contains(baseInfo)) {
                auto offset = symbolAddr->getOffset();
                if (auto constOffset = offset->tryEvalAsConstant();
                    constOffset &&
                    (!symbolAddr->getLength() ||
                     (symbolAddr->getLength().value()->tryEvalAsConstant() &&
                      symbolAddr->getLength().value()->tryEvalAsConstant().value() == 1))) {
                    if (constOffset.value() < 0)
                        ERROR("Negetive offset.");
                    auto unsignedOffset = static_cast<uint64_t>(constOffset.value());
                    auto &rangeExprMap  = memoryMap_constantRange_.at(baseInfo);
                    auto rangeForSearch =
                        std::pair{unsignedOffset, std::numeric_limits<uint64_t>::max()};
                    auto upperBoundIt = rangeExprMap.upper_bound(rangeForSearch);
                    auto firstLEIt    = upperBoundIt == rangeExprMap.begin() ? rangeExprMap.end()
                                                                             : prev(upperBoundIt);
                    if (firstLEIt == rangeExprMap.end() ||
                        firstLEIt->first.second <= unsignedOffset)
                        return std::nullopt;
                    return firstLEIt->second->getRangeIndexSubstituted(baseInfo, *offset);
                } else if (auto &len = symbolAddr->getLength();
                           constOffset && len && len.value()->tryEvalAsConstant()) {
                    UNIMPLEMENT(
                        "There doesn't appear to be a need for constant-range range queries at "
                        "this time.");
                }
            }

            if (!memoryMap_symbolicRange_.contains(baseInfo))
                return std::nullopt;
            auto &addrValueMap = memoryMap_symbolicRange_.at(baseInfo);
            if (symbolAddr->getLength() && symbolAddr->getLength().value()->tryEvalAsConstant() &&
                symbolAddr->getLength().value()->tryEvalAsConstant().value() == 1) {
                auto fakeRangeHandle =
                    factory().withoutLength(factory().importAddress(*symbolAddr));
                const auto &fakeRange = fakeRangeHandle.cast<symbolic::SymbolAddress>();
                auto it = addrValueMap.find(fakeRange);
                if (it == addrValueMap.end())
                    return std::nullopt;
                return it->second->clone();
            }
            auto it = addrValueMap.find(*symbolAddr);
            if (it == addrValueMap.end())
                return std::nullopt;
            return it->second->clone();
        } else if (auto fieldAddr = symbolic::dyn_cast<const symbolic::FieldAddress>(&addr)) {
            auto &baseAddr = fieldAddr->getBaseAddr();
            auto &index    = fieldAddr->getFieldIndex();
            auto baseValue = read(*baseAddr);
            if (baseValue == std::nullopt)
                return std::nullopt;
            auto baseSt = symbolic::dyn_cast<const symbolic::Structure>(baseValue.value().get().get());
            if (baseSt == nullptr)
                ERROR("Value of address from a `fieldAddress` is not a structure.");
            return baseSt->getFieldValue(index)->clone();
        }
        UNREACHABLE();
    }

    void MemoryModel::write(const symbolic::Address &addr,
                            utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>> value) {
        if (auto varAddr = symbolic::dyn_cast<const symbolic::VariableAddress>(&addr)) {
            memoryMap_variableAddr_.insert_or_assign(*varAddr, importValue(*value));
            return;
        } else if (auto symbolAddr = symbolic::dyn_cast<const symbolic::SymbolAddress>(&addr)) {
            auto valueHandle = importValue(*value);
            auto baseInfo = symbolAddr->getBaseInfo();

            auto constOffset = symbolAddr->getOffset()->tryEvalAsConstant();
            auto &len        = symbolAddr->getLength();
            auto constLen    = len ? len.value()->tryEvalAsConstant() : std::nullopt;

            if (constOffset && (!len || constLen)) {
                if (constOffset.value() < 0 || (constLen && constLen.value() <= 0))
                    ERROR("Constant offset must be greater or equal to zero and length must be "
                          "greater than zero.");
                // constant range
                auto unsignedOffset = static_cast<uint64_t>(constOffset.value());
                auto unsignedLen = constLen ? static_cast<uint64_t>(constLen.value()) : uint64_t{1};
                memoryMap_constantRange_.try_emplace(
                    baseInfo, std::map<ConstRange, StoredValue>{});
                auto &rangeValueMap = memoryMap_constantRange_.at(baseInfo);
                auto range          = std::pair{unsignedOffset, unsignedOffset + unsignedLen};
                auto rangeForSearch =
                    std::pair{unsignedOffset, std::numeric_limits<uint64_t>::max()};
                auto endIt        = rangeValueMap.end();
                auto upperBoundIt = rangeValueMap.upper_bound(rangeForSearch);
                auto firstLEIt = upperBoundIt == rangeValueMap.begin() ? endIt : prev(upperBoundIt);

                auto &[range_leftBound, range_rightBound] = range;
                if (firstLEIt != endIt && firstLEIt->first.second > range.first) {
                    auto &[firstLE_leftBound, firstLE_rightBound] = firstLEIt->first;
                    auto &firstLE_value                           = firstLEIt->second;
                    if (firstLE_leftBound < range_leftBound) {
                        auto leftRange = std::pair{firstLE_leftBound, range_leftBound};
                        rangeValueMap.insert_or_assign(leftRange, firstLE_value);
                    }
                    if (firstLE_rightBound > range_rightBound) {
                        auto rightRange = std::pair{range_rightBound, firstLE_rightBound};
                        rangeValueMap.insert_or_assign(rightRange, firstLE_value);
                    }
                    rangeValueMap.erase(firstLEIt);
                }
                if (upperBoundIt != endIt && upperBoundIt->first.first < range.second) {
                    auto &[upperBound_leftBound, upperBound_rightBound] = upperBoundIt->first;
                    auto &upperBound_value                              = upperBoundIt->second;
                    if (upperBound_rightBound > range_rightBound) {
                        auto rightRange = std::pair{range_rightBound, upperBound_rightBound};
                        rangeValueMap.insert_or_assign(rightRange, upperBound_value);
                    }
                    rangeValueMap.erase(upperBoundIt);
                }
                rangeValueMap.insert_or_assign(range, valueHandle);
                return;
            }
            // symbolic range
            auto &addrValueMap = memoryMap_symbolicRange_[baseInfo];
            if (symbolAddr->getLength() && symbolAddr->getLength().value()->tryEvalAsConstant() &&
                symbolAddr->getLength().value()->tryEvalAsConstant() == 1) {
                auto fakeRangeHandle =
                    factory().withoutLength(factory().importAddress(*symbolAddr));
                const auto &fakeRange = fakeRangeHandle.cast<symbolic::SymbolAddress>();
                addrValueMap.insert_or_assign(fakeRange, valueHandle);
                return;
            }
            addrValueMap.insert_or_assign(*symbolAddr, valueHandle);
            return;
        } else if (auto fieldAddr = symbolic::dyn_cast<const symbolic::FieldAddress>(&addr)) {
            auto &baseAddr = fieldAddr->getBaseAddr();
            auto &index    = fieldAddr->getFieldIndex();
            if (fieldAddr->getDefinition() &&
                fieldAddr->getDefinition()->getNameAsString() == "BigNum" && index == 4) {
                DEBUG("write BigNum->data with: " << value->dump());
            }
            auto baseValue = read(*baseAddr);
            if (baseValue == std::nullopt)
                ERROR("Structure isn't existed in MemoryModel, insert it first.");
            auto baseSt = symbolic::dyn_cast<symbolic::Structure>(baseValue.value().get().get());
            if (baseSt == nullptr)
                ERROR("Value of address from a `fieldAddress` is not a structure.");
            auto updated = baseSt->withFieldValue(index, std::move(value));
            std::unique_ptr<symbolic::SymbolicExpr> updatedExpr =
                std::move(updated).into_underlying();
            write(*baseAddr, utils::not_null<std::unique_ptr<symbolic::SymbolicExpr>>{
                                 std::move(updatedExpr)});
            return;
        }
        UNREACHABLE();
    }

    bool MemoryModel::contains(const symbolic::Address &addr) const {
        return read(addr) ? true : false;
    }

    // MemoryModel::flat_view MemoryModel::flat() { return MemoryModel::flat_view{*this}; }
    const MemoryModel::flat_view MemoryModel::flat() const {
        return MemoryModel::flat_view{const_cast<MemoryModel &>(*this)};
    }

    void MemoryModel::eraseExpiredLocals(
        const std::unordered_set<const clang::VarDecl *> &localVars) {
        std::erase_if(memoryMap_variableAddr_, [&](auto const &kv) {
            auto fromRoot = kv.first.getFromRoot();
            if (fromRoot == std::nullopt)
                UNREACHABLE(); // VarriableAddress should have a *from*.
            return localVars.contains(fromRoot.value());
        });
        std::erase_if(memoryMap_constantRange_, [&](auto const &kv) {
            auto fromRoot = kv.first.getFromRoot();
            // check that if fromRoot is empty, then the address is allocated by
            // `malloc` function, thus should not be deleted from the memory model;
            if (fromRoot == std::nullopt)
                return false;
            return localVars.contains(fromRoot.value());
        });
        std::erase_if(memoryMap_symbolicRange_, [&](auto const &kv) {
            auto fromRoot = kv.first.getFromRoot();
            // check that if fromRoot is empty, then the address is allocated by
            // `malloc` function, thus should not be deleted from the memory model;
            if (fromRoot == std::nullopt)
                return false;
            return localVars.contains(fromRoot.value());
        });
    }

    void MemoryModel::mergeConstantRanges() {
        for (auto &[base, cmap] : memoryMap_constantRange_) {
            if (cmap.empty())
                continue;

            // 1) Move to a vector to allow reordering and in-place merging
            std::vector<std::pair<ConstRange, StoredValue>> v;
            v.reserve(cmap.size());
            for (auto &kv : cmap)
                v.emplace_back(kv.first, kv.second);
            cmap.clear();

            // Sort by offset (ascending)
            auto byOffset = [](auto &a, auto &b) {
                return a.first.first < b.first.first; // compare offset
            };
            std::sort(v.begin(), v.end(), byOffset);

            // 2) Single-pass merge
            std::vector<std::pair<ConstRange, StoredValue>> merged;
            merged.reserve(v.size());

            auto pushOrMerge = [&](std::pair<ConstRange, StoredValue> &&cur) {
                if (merged.empty()) {
                    merged.push_back(std::move(cur));
                    return;
                }
                auto &[pr, pexpr] = merged.back();
                auto &[cr, cexpr] = cur;

                uint64_t pRight = pr.second;
                uint64_t cLeft = cr.first, cRight = cr.second;

                // Adjacent endpoints and equal values → coalesce by extending the length
                if (pRight == cLeft && *pexpr->simplifiedExpr() == *cexpr->simplifiedExpr()) {
                    pr.second = cRight;
                } else {
                    merged.push_back(std::move(cur));
                }
            };

            for (auto &e : v)
                pushOrMerge(std::move(e));

            // 3) Write back (map keeps ranges sorted by key)
            for (auto &e : merged) {
                cmap.emplace(e.first, std::move(e.second));
            }
        }
    }

    void MemoryModel::mergeSymbolicRanges() {
        using SA     = symbolic::SymbolAddress;
        using Expr   = symbolic::SymbolicExpr;

        auto valueEquivalent = [](StoredValue a, StoredValue b) -> bool {
            return *a->simplifiedExpr() == *b->simplifiedExpr();
        };

        // Compute hash of (a + b) by constructing a factory-backed BinaryOp(Add), simplifying,
        // then hashing.
        auto addedHash = [this](const Expr &a, const Expr &b) {
            return factory_
                ->binary(factory_->importExpr(a), symbolic::BinaryOpExpr::Operator::Add,
                         factory_->importExpr(b))
                ->simplifiedExpr()
                ->hash();
        };

        for (auto &[base, umap] : memoryMap_symbolicRange_) {
            if (umap.empty())
                continue;

            struct Item {
                SA key;                             ///< Symbolic address (offset[/length])
                StoredValue val;                    ///< Stored value expression
                std::optional<uint64_t> constOff{}; ///< If offset folds to constant
                std::optional<uint64_t> constLen{}; ///< If length folds to constant
                size_t offHash{0};                  ///< hash(offset.simplified)
                size_t lenHash{0}; ///< hash(length.simplified) if range (debug/consistency)
                size_t rightHash{
                    0}; ///< hash((offset+length).simplified) or hash(offset+1) for non-range
                size_t valHash{0}; ///< hash(val.simplified) for coarse grouping
                bool used{false};  ///< Whether this edge is already merged into a chain
            };

            std::vector<Item> items;
            items.reserve(umap.size());

            // (1) Move entries to items and precompute hashes/constants
            for (auto &[addr, expr] : umap) {
                Item it{addr, std::move(expr)};

                it.constOff = it.key.getOffset()->tryEvalAsConstant();
                if (auto &len = it.key.getLength()) {
                    it.constLen = len.value()->tryEvalAsConstant();
                }

                // Both offset and length constant → should have been inserted into constant map.
                if (it.constOff && it.constLen)
                    ERROR("Offset and length are both constant, they should be inserted into "
                          "memoryMap_constantRange_ instead.");

                // Precompute endpoint/value hashes (using simplified forms)
                it.offHash = it.key.getOffset()->simplifiedExpr()->hash();
                it.valHash = it.val->simplifiedExpr()->hash();
                if (auto &len = it.key.getLength())
                    it.lenHash = len.value()->simplifiedExpr()->hash();

                // Right endpoint hash:
                // - range: hash(offset + length)
                // - non-range: hash(offset + 1) → single-address treated as [off, off+1)
                if (auto &len = it.key.getLength()) {
                    it.rightHash = addedHash(*it.key.getOffset(), *len.value());
                } else {
                    it.rightHash = addedHash(*it.key.getOffset(), symbolic::detail::LiteralExprNode{1});
                }

                items.emplace_back(std::move(it));
            }
            umap.clear();

            // (2) Group by value-hash (coarse buckets to cut comparisons)
            std::unordered_map<size_t, std::vector<size_t>> groups;
            groups.reserve(items.size() * 2); // perf: reduce rehashes; not semantically required
            for (size_t i = 0; i < items.size(); ++i) {
                groups[items[i].valHash].push_back(i);
            }

            // Result map being rebuilt for this BaseInfo
            std::unordered_map<SA, StoredValue> newMap;
            newMap.reserve(items.size());

            // (3) For each value group, build adjacency and emit chains
            for (auto &[vh, idxs] : groups) {
                // Lh → outgoing edges; Rh → incoming edges
                std::unordered_map<size_t, std::vector<size_t>> outByLeft, inByRight;
                outByLeft.reserve(idxs.size() * 2);
                inByRight.reserve(idxs.size() * 2);

                for (size_t i : idxs) {
                    outByLeft[items[i].offHash].push_back(i);
                    inByRight[items[i].rightHash].push_back(i);
                }

                // Try to emit a maximal chain starting from a given edge
                auto tryEmitChain = [&](size_t startIdx) {
                    if (items[startIdx].used)
                        return;

                    // Seed the merged key from the first segment
                    auto mergedKey   = std::make_unique<SA>(items[startIdx].key);
                    size_t cur       = startIdx;
                    size_t rightHash = items[cur].rightHash;
                    items[cur].used  = true;

                    // Greedily follow a UNIQUE successor whose Lh == current Rh and value equivalent
                    while (true) {
                        auto it = outByLeft.find(rightHash);
                        if (it == outByLeft.end())
                            break;

                        size_t next_idx = SIZE_MAX;
                        for (size_t j : it->second) {
                            if (items[j].used)
                                continue;
                            if (valueEquivalent(items[cur].val, items[j].val)) {
                                if (next_idx == SIZE_MAX) {
                                    next_idx = j;
                                } else {
                                    // Ambiguity (multiple candidates): stop conservatively
                                    UNIMPLEMENT(
                                        "Multiple `symbolAddresses` with the same offset can "
                                        "be merged. Process this when encountered.");
                                }
                            }
                        }
                        if (next_idx == SIZE_MAX)
                            break;

                        auto &itemToBeMerged = items[next_idx];

                        // Append the successor's length:
                        // - If successor is range: add its length expression
                        // - If successor is non-range: add 1
                        auto mergedHandle = factory().importAddress(*mergedKey);
                        if (auto &len = itemToBeMerged.key.getLength()) {
                            mergedHandle =
                                factory().withAddedLength(mergedHandle,
                                                          factory().importExpr(*len.value()));
                        } else {
                            mergedHandle =
                                factory().withAddedLength(mergedHandle, factory().literal(1));
                        }
                        mergedKey = std::make_unique<SA>(mergedHandle.cast<SA>());

                        // Advance to successor
                        rightHash           = itemToBeMerged.rightHash;
                        itemToBeMerged.used = true;
                        cur                 = next_idx;
                    }

                    // Emit the merged interval with the value from the starting edge
                    newMap.emplace(std::move(*mergedKey), std::move(items[startIdx].val));
                };

                // Prefer starting at "obvious starts": Lh with zero in-degree
                for (auto &[leftHash, outs] : outByLeft) {
                    size_t indeg = 0;
                    if (auto it = inByRight.find(leftHash); it != inByRight.end())
                        indeg = it->second.size();
                    if (indeg == 0) {
                        for (size_t e : outs) {
                            if (!items[e].used)
                                tryEmitChain(e);
                        }
                    } else if (indeg != 1) {
                        // Not an error per se, but indicates possible forks; we log for visibility.
                        WARN("Multiple `symbolAddresses` with the same offset are present.");
                    }
                }

                // Handle remaining edges (cycles or ambiguous starts) conservatively
                for (size_t e : idxs) {
                    if (!items[e].used) {
                        WARN("If this location is reached, it indicates either a cycle is present "
                             "or a merge has multiple candidates, and the result may be incorrect "
                             "or unstable.");
                        tryEmitChain(e);
                    }
                }
            }

            // (4) Write merged map back for this BaseInfo
            memoryMap_symbolicRange_[base] = std::move(newMap);
        }
    }

    ProgramState::ProgramState(std::unique_ptr<Path> initialPath,
                               std::unique_ptr<ACSLFunction> func,
                               context::ACSLGContext &context)
        : func_(std::move(func)), context_(context),
          startPoint_(symbolic::SourcePoint::fromFuncDecl(func_->getFunctionDecl(),
                                                          context_.getSourceManager(),
                                                          context_.getLangOptions())) {
        paths_.push_back(std::move(initialPath));
    }

    ProgramState::ProgramState(std::unique_ptr<ACSLFunction> func, context::ACSLGContext &context)
        : func_(std::move(func)), context_(context),
          startPoint_(symbolic::SourcePoint::fromFuncDecl(func_->getFunctionDecl(),
                                                          context_.getSourceManager(),
                                                          context_.getLangOptions())) {}

    ProgramState::ProgramState(const ProgramState &other)
        : func_(other.func_->clone()), context_(other.context_), startPoint_(other.startPoint_) {
        paths_.reserve(other.paths_.size());
        std::ranges::transform(other.paths_, std::back_inserter(paths_),
                               [](auto &path) { return path->clone(); });
    }

    ProgramState &ProgramState::operator=(ProgramState &&other) {
        if (&context_ != &other.context_)
            ERROR("Different contexts!");
        func_       = std::move(other.func_);
        paths_      = std::move(other.paths_);
        startPoint_ = std::move(other.startPoint_);
        return *this;
    }

    // @WindOctober: A preliminary scan of the function is also required to identify all
    // global variables (i.e., variables whose scope is greater than or equal
    // to the current function). These variables must then be either properly
    // initialized or subjected to special handling.

    void ProgramState::init() {
        auto FD       = func_->getFunctionDecl();
        auto initPath = std::make_unique<Path>(context_, startPoint_);
        for (const clang::ParmVarDecl *param : FD->parameters())
            (void)initPath->allocMemory(param, /*initSymbolic*/ true);
        paths_.clear();
        paths_.push_back(std::move(initPath));
    }

    void ProgramState::step(const clang::Stmt *stmt) {
        if (!stmt)
            return;
        llvm::TypeSwitch<const clang::Stmt *, void>(stmt)
            .Case<clang::CompoundStmt>([this](const clang::CompoundStmt *cs) {
                DEBUG("stepping clang::CompoundStmt...");
                for (const clang::Stmt *child : cs->children()) {
                    if (child)
                        step(child);
                }
            })
            .Case<clang::IfStmt>([this](const clang::IfStmt *ifStmt) {
                DEBUG("stepping IfStmt...");
                std::vector<const clang::Expr *> branchConds;
                std::vector<const clang::Stmt *> branchStmts;
                branchConds.push_back(ifStmt->getCond());
                branchStmts.push_back(ifStmt->getThen());
                if (ifStmt->getElse())
                    branchStmts.push_back(ifStmt->getElse());
                else
                    branchStmts.push_back(nullptr);

                stepBranch(branchConds, branchStmts);
            })
            .Case<clang::ReturnStmt>([this](const clang::ReturnStmt *retStmt) {
                DEBUG("stepping ReturnStmt...");
                setReturnExpr(retStmt->getRetValue());
                setStates(Path::PathState::Return, NULL);
            })
            .Case<clang::DeclStmt>([this](const clang::DeclStmt *declStmt) {
                DEBUG("stepping clang::DeclStmt...");
                addNewDecls(declStmt);
            })
            .Case<clang::BinaryOperator>([this](const clang::BinaryOperator *binOp) {
                DEBUG("stepping BinaryOperator...");
                if (utils::ignoreTopBinop(binOp))
                    return;
                if (!utils::isAssignOp(binOp))
                    UNIMPLEMENT("BinaryOperator not implemented: " << binOp->getOpcode());
                updateVarState(binOp);
            })
            .Case<clang::ParenExpr>([this](const clang::ParenExpr *parenExpr) {
                DEBUG("stepping clang::ParenExpr...");
                step(parenExpr->getSubExpr());
            })
            .Case<clang::Expr>([this](const clang::Expr *expr) {
                DEBUG("stepping clang::Expr...");
                stepExpr(expr);
            })
            .Case<clang::ImplicitCastExpr>(
                [](const clang::ImplicitCastExpr *) -> std::unique_ptr<symbolic::SymbolicExpr> {
                    UNREACHABLE();
                })
            .Case<clang::CaseStmt>([this](const clang::CaseStmt *caseStmt) {
                DEBUG("stepping CaseStmt...");
                // Can only be met during step(SwitchStmt), just ignore it.
                step(caseStmt->getSubStmt());
            })
            .Case<clang::DefaultStmt>([this](const clang::DefaultStmt *defaultStmt) {
                DEBUG("stepping DefaultStmt...");
                // Can only be met during step(SwitchStmt), just ignore it.
                step(defaultStmt->getSubStmt());
            })
            .Case<clang::SwitchStmt>([this](const clang::SwitchStmt *switchStmt) {
                DEBUG("stepping SwitchStmt...");
                auto prevStmtCtx = this->stmtCtx_;
                setStmtCtx(switchStmt);

                if (switchStmt->hasInitStorage())
                    UNIMPLEMENT("Unsupported Switch type, Cond has init statement: "
                                << switchStmt->getCond());
                if (auto bodyStmt =
                        dyn_cast_if_present<clang::CompoundStmt>(switchStmt->getBody())) {
                    if (isa_and_present<clang::CaseStmt>(bodyStmt->body_front())) {
                        stepSimpleSwitch(switchStmt);
                    } else {
                        UNIMPLEMENT("Unsupported Switch type, body's first clang::Stmt is not "
                                    "CaseStmt: "
                                    << switchStmt->getBody());
                    }
                } else {
                    WARN("A SwtichStmt without clang::CompoundStmt body (why?) has been "
                         "ignored: "
                         << switchStmt);
                }

                resetBreakState();
                setStmtCtx(prevStmtCtx);
            })
            .Case<clang::ForStmt>([this](const clang::ForStmt *forStmt) {
                DEBUG("stepping clang::ForStmt...");
                auto prevStmtCtx = this->stmtCtx_;
                setStmtCtx(forStmt);
                stepLoop(forStmt);
                resetBreakState();
                setStmtCtx(prevStmtCtx);
            })
            .Case<clang::WhileStmt>([this](const clang::WhileStmt *whileStmt) {
                DEBUG("stepping clang::WhileStmt...");
                auto prevStmtCtx = this->stmtCtx_;
                setStmtCtx(whileStmt);
                stepLoop(whileStmt);
                resetBreakState();
                setStmtCtx(prevStmtCtx);
            })
            .Case<clang::DoStmt>([this](const clang::DoStmt *doStmt) {
                DEBUG("stepping clang::DoStmt...");
                auto prevStmtCtx = this->stmtCtx_;
                setStmtCtx(doStmt);
                stepLoop(doStmt);
                resetBreakState();
                setStmtCtx(prevStmtCtx);
            })
            .Case<clang::CXXForRangeStmt>([this](const clang::CXXForRangeStmt *rangeStmt) {
                DEBUG("stepping CXXForRangeStmt...");
                auto prevStmtCtx = this->stmtCtx_;
                this->stmtCtx_   = rangeStmt;
                stepLoop(rangeStmt);
                resetBreakState();
                this->stmtCtx_ = prevStmtCtx;
            })
            .Case<clang::BreakStmt>([this](const clang::BreakStmt *) {
                DEBUG("stepping BreakStmt...");
                setStates(Path::PathState::Break, stmtCtx_);
            })
            .Case<clang::ContinueStmt>([](const clang::ContinueStmt *) {
                DEBUG("stepping ContinueStmt...");
                TODO();
            })
            .Case<clang::NullStmt>([](const clang::NullStmt *) { DEBUG("stepping NullStmt..."); })
            .Default([](const clang::Stmt *s) {
                UNIMPLEMENT("Unsupported clang::Stmt type: " << s->getStmtClassName());
            });

        auto localVars = utils::collectLocalVars(stmt);
        for (auto &path : paths_) {
            auto &memoryState = path->getMutMemoryState();
            std::erase_if(path->varAddr_, [&](auto &&kv) { return localVars.contains(kv.first); });
            if (!localVars.empty()) {
                PathConditions filtered;
                filtered.reserve(path->pathConditions_.size());
                for (auto &cond : path->pathConditions_) {
                    auto stripped =
                        dropLocalConjuncts(path->context_.getExprFactory(), *cond, localVars);
                    if (!stripped)
                        continue;
                    filtered.emplace(path->context_.getExprFactory().importExpr(*stripped));
                }
                path->pathConditions_ = std::move(filtered);
            }
            memoryState.eraseExpiredLocals(localVars);
            memoryState.mergeRanges();
        }
        return;
    }

    void ProgramState::stepExpr(const clang::Expr *expr) {
        std::vector<utils::not_null<std::unique_ptr<Path>>> updatedPaths;

        for (auto &path : paths_) {
            if (!path->isActive()) {
                updatedPaths.push_back(std::move(path));
                continue;
            }

            auto [newPathGroup, exprGroup] = path->evalExpr(expr);
            updatedPaths.push_back(std::move(path));

            for (size_t i = 0; i < newPathGroup.size(); ++i) {
                updatedPaths.push_back(std::move(newPathGroup[i]));
            }
        }

        paths_ = std::move(updatedPaths);
    }

    void ProgramState::stepBranch(const std::vector<const clang::Expr *> &branchConds,
                                  const std::vector<const clang::Stmt *> &branchStmts) {
        assert(branchStmts.size() == branchConds.size() + 1);

        std::vector<std::unique_ptr<ProgramState>> clones;
        std::vector<const ProgramState *> statesForMerge;

        auto splitPair = splitActiveInactive();
        size_t n       = branchConds.size();
        for (size_t i = 0; i < n; ++i) {
            auto newState = splitPair.first->clone();

            std::vector<utils::not_null<std::unique_ptr<Path>>> updatedPaths;

            for (auto &path : newState->paths_) {
                Path::EvalResult eval = path->evalExpr(branchConds[i]);

                size_t m = eval.second.size();
                for (size_t j = 0; j < m; ++j) {
                    auto newPath = (j == 0) ? std::move(path) : std::move(eval.first[j - 1]);
                    auto cond    = std::move(eval.second[j]);
                    if (auto lit = cond->evalToConstExpr()) {
                        const bool isTrue = (lit->getLiteralValue() != 0);

                        if (!isTrue) {
                            continue;
                        } else {
                            updatedPaths.push_back(std::move(newPath));
                            continue;
                        }
                    }

                    newPath->insertPathCondition(std::move(cond));
                    updatedPaths.push_back(std::move(newPath));
                }
            }

            newState->paths_ = std::move(updatedPaths);

            newState->step(branchStmts[i]);

            statesForMerge.push_back(newState.get());
            clones.push_back(std::move(newState));
        }

        std::queue<std::pair<utils::not_null<std::unique_ptr<Path>>, size_t>> worklist;
        std::vector<utils::not_null<std::unique_ptr<Path>>> finalPaths;

        for (auto &path : splitPair.first->paths_) {
            worklist.emplace(std::move(path), 0);
        }

        while (!worklist.empty()) {
            auto [path, idx] = std::move(worklist.front());
            worklist.pop();

            if (idx == branchConds.size()) {
                finalPaths.push_back(std::move(path));
                continue;
            }

            Path::EvalResult eval = path->evalExpr(branchConds[idx]);

            for (size_t j = 0; j < eval.second.size(); ++j) {
                auto newPath = (j == 0) ? std::move(path) : std::move(eval.first[j - 1]);
                auto cond    = std::move(eval.second[j]);

                if (auto lit = cond->evalToConstExpr()) {
                    const bool isTrue = (lit->getLiteralValue() != 0);
                    if (isTrue) {
                        continue;
                    } else {
                        worklist.emplace(std::move(newPath), idx + 1);
                        continue;
                    }
                }

                newPath->insertPathCondition(createLNotExpr(std::move(cond)));
                worklist.emplace(std::move(newPath), idx + 1);
            }
        }

        splitPair.first->paths_ = std::move(finalPaths);
        splitPair.first->step(branchStmts.back());

        statesForMerge.push_back(splitPair.first.get());
        auto mergedActive = merge(statesForMerge);
        for (auto &path : splitPair.second->paths_)
            mergedActive->paths_.push_back(std::move(path));

        paths_ = std::move(mergedActive->paths_);
    }

    void ProgramState::stepLoop(const clang::Stmt *loopStmt) {
        auto preState  = clone();
        auto loopEntry = preState->clone();

        if (auto forLoop = dyn_cast<clang::ForStmt>(loopStmt); forLoop && forLoop->getInit())
            loopEntry->step(forLoop->getInit());

        if (const auto *doWhileStmt = dyn_cast<clang::DoStmt>(loopStmt)) {
            auto cond = doWhileStmt->getCond()->IgnoreParenImpCasts();
            auto body = doWhileStmt->getBody();
            if (auto *literal = llvm::dyn_cast<clang::IntegerLiteral>(cond);
                literal && literal->getValue() == 0) {
                step(body);
                return;
            }
        }

        auto [loopInfo, ok] = spec_generator::parseLoopInfo(*preState, *loopEntry, loopStmt);

        spec_generator::EmitLoopInvResult res;
        if (ok) {
            res = emitLoopInvariant(*preState, *loopEntry, loopInfo);
        } else {
            parseComplexLoopInfo(*preState, *loopEntry, loopInfo);
            res = emitLoopInvariant(*preState, *loopEntry, loopInfo,
                                    "ComplexPathInsensitiveLoopInv", "ComplexPathSensitiveLoopInv");
        }
        INFO(res.acsl);

        auto beginLoc = loopStmt->getSourceRange().getBegin();
        context_.insertText(beginLoc, res.acsl, /*after*/ false,
                            /*indentNewLines*/ true);
        context_.insertUsedPoints(std::move(res.usedPoints));

        if (this == res.postState.get())
            UNREACHABLE();
        *this = std::move(*res.postState);

        // INFO(this->dump());
    }

    void ProgramState::setStates(Path::PathState state, const clang::Stmt *stmt) {
        for (auto &pathPtr : paths_) {
            if (pathPtr->isActive()) {
                pathPtr->setPathState(state);
                pathPtr->stmtCtx_ = stmt;
            }
        }
    }

    void ProgramState::setReturnExpr(const clang::Expr *expr) {
        if (expr == nullptr) {
            for (auto &pathPtr : paths_) {
                if (!pathPtr->isActive())
                    continue;
                pathPtr->setReturnExpr(std::nullopt);
            }
            return;
        }
        expr = expr->IgnoreParenImpCasts();
        DEBUG("ReturnStmt expr: " << expr->getStmtClassName());

        std::vector<utils::not_null<std::unique_ptr<Path>>> updatedPaths;

        for (auto &pathPtr : paths_) {
            if (!pathPtr->isActive()) {
                updatedPaths.emplace_back(std::move(pathPtr));
                continue;
            }

            Path::EvalResult eval = pathPtr->evalExpr(expr);
            auto &generatedPaths  = eval.first;
            Formulas &results     = eval.second;

            size_t n = results.size();
            for (size_t i = 0; i < n; ++i) {
                auto newPath = i == 0 ? std::move(pathPtr) : std::move(generatedPaths[i - 1]);

                DEBUG("ReturnStmt value: " << results[i]->dump());
                newPath->setReturnExpr(std::move(results[i]));
                updatedPaths.emplace_back(std::move(newPath));
            }
        }

        paths_ = std::move(updatedPaths);
    }

    void ProgramState::updateVarState(const clang::BinaryOperator *binOp) {
        std::vector<utils::not_null<std::unique_ptr<Path>>> updatedPaths;

        for (auto &path : paths_) {
            if (!path->isActive()) {
                updatedPaths.push_back(std::move(path));
                continue;
            }

            Path::EvalResult eval;

            if (binOp->isCompoundAssignmentOp()) {
                symbolic::BinaryOpExpr::Operator op =
                    symbolic::getCompoundAssignOp(binOp->getOpcode());
                auto &factory = context_.getExprFactory();

                Path::EvalResult lhs = path->evalExpr(binOp->getLHS());

                std::vector<utils::not_null<std::unique_ptr<Path>>> outPaths;
                Formulas outExprs;

                for (size_t i = 0; i < lhs.second.size(); ++i) {
                    auto &lhsPath = (i == 0) ? *path : *lhs.first[i - 1];

                    Path::EvalResult rhs = lhsPath.evalExpr(binOp->getRHS());

                    for (size_t j = 0; j < rhs.second.size(); ++j) {
                        symbolic::Expr lhsExpr{factory,
                                               factory.importExpr(*lhs.second[i])};
                        symbolic::Expr rhsExpr{factory,
                                               factory.importExpr(*rhs.second[j])};
                        outExprs.emplace_back(lhsExpr.binary(op, rhsExpr)->clone());

                        if (i != 0 || j != 0)
                            outPaths.emplace_back(std::move(rhs.first[j - 1]));
                    }
                }
                eval = {std::move(outPaths), std::move(outExprs)};
            } else {
                eval = path->evalExpr(binOp->getRHS());
            }

            size_t n = eval.second.size();
            for (size_t i = 0; i < n; ++i) {
                auto newPath  = (i == 0) ? std::move(path) : std::move(eval.first[i - 1]);
                auto newValue = std::move(eval.second.at(i));
                auto dstAddr  = newPath->extractLValue(binOp->getLHS());
                if (auto *mem =
                        llvm::dyn_cast<clang::MemberExpr>(binOp->getLHS()->IgnoreParenImpCasts())) {
                    if (auto *FD = llvm::dyn_cast<clang::FieldDecl>(mem->getMemberDecl())) {
                        if (FD->getNameAsString() == "data") {
                            auto loc      = binOp->getExprLoc();
                            auto &SM      = context_.getSourceManager();
                            auto presumed = SM.getPresumedLoc(loc);
                            if (presumed.isValid()) {
                                DEBUG("assign to *.data at "
                                      << presumed.getFilename() << ":" << presumed.getLine() << ":"
                                      << presumed.getColumn() << " value=" << newValue->dump());
                            }
                        }
                    }
                }
                if (newValue->isUnknown()) {
                    if (auto *fieldAddr =
                            symbolic::dyn_cast<symbolic::FieldAddress>(dstAddr.get().get())) {
                        if (fieldAddr->getDefinition() &&
                            fieldAddr->getDefinition()->getNameAsString() == "BigNum" &&
                            fieldAddr->getFieldIndex() == 4) {
                            auto loc      = binOp->getExprLoc();
                            auto &SM      = context_.getSourceManager();
                            auto presumed = SM.getPresumedLoc(loc);
                            if (presumed.isValid()) {
                                DEBUG("assign Unknown to BigNum->data at "
                                      << presumed.getFilename() << ":" << presumed.getLine() << ":"
                                      << presumed.getColumn());
                            }
                        }
                    }
                }
                newPath->updateMemory(*dstAddr, std::move(newValue));
                updatedPaths.push_back(std::move(newPath));
            }
        }

        paths_ = std::move(updatedPaths);
    }

    void ProgramState::addNewDecls(const clang::DeclStmt *declStmt) {
        std::vector<const clang::VarDecl *> varDecls;
        for (auto it = declStmt->decl_begin(); it != declStmt->decl_end(); ++it) {
            clang::Decl *decl = *it;
            if (auto *varDecl = dyn_cast<clang::VarDecl>(decl)) {
                varDecls.push_back(varDecl);
                continue;
            }
            if (isa<clang::StaticAssertDecl>(decl))
                continue; // already enforced at compile time, nothing to track at runtime

            UNIMPLEMENT("Unhandled clang::Decl type: "s + decl->getDeclKindName());
        }
        for (const clang::VarDecl *varDecl : varDecls) {
            const clang::Expr *initExpr = varDecl->getInit();
            std::vector<utils::not_null<std::unique_ptr<Path>>> updatedPaths;

            for (auto &path : paths_) {
                if (!path->isActive()) {
                    updatedPaths.push_back(std::move(path));
                    continue;
                }

                auto varAddr = path->allocMemory(varDecl);

                if (initExpr == nullptr) {
                    // TODO: add default initialization for basic types.
                    WARN("Uninitialized variable " + varDecl->getNameAsString());

                    auto pointAfterDecl = symbolic::SourcePoint::fromStmtAfter(
                        declStmt, context_.getSourceManager(), context_.getLangOptions());

                    auto varType = varDecl->getType();
                    path->updateVarState(
                        varDecl,
                        symbolic::getSymbol(varType, varAddr->addressClone().into_underlying(),
                                            pointAfterDecl));

                    updatedPaths.push_back(std::move(path));
                    continue;
                }
                if (auto initListExpr = dyn_cast<clang::InitListExpr>(initExpr)) {
                    auto varType = varDecl->getType();

                    if (varType->isAnyPointerType() || varType->isArrayType()) {
                        TODO();
                    } else if (auto RD = varType->getAsRecordDecl();
                               RD != nullptr && varType->isStructureType()) {
                        if (!RD->isCompleteDefinition())
                            ERROR("Struct with incomplete definition!");

                        RD = RD->getDefinition();

                        auto st = makeStructureForRecord(
                            context_.getExprFactory(), RD,
                            varAddr->addressClone().into_underlying(), startPoint_);
                        if (initListExpr->getNumInits() != st->getNumFields())
                            ERROR("Initializer std::list size mismatches the struct's field "
                                  "count.");
                        for (size_t i = 0; i < st->getNumFields(); ++i) {
                            const clang::Expr *init = initListExpr->getInit(i);

                            Path::EvalResult eval = path->evalExpr(init);
                            if (eval.second.size() != 1)
                                UNIMPLEMENT(
                                    "No control flow branching permitted within an initializer "
                                    "list now.");
                            st = st->withFieldValue(i, std::move(eval.second[0])).into_underlying();
                        }

                        path->updateVarState(varDecl, std::move(st));
                        updatedPaths.push_back(std::move(path));
                        continue;
                    } else {
                        UNIMPLEMENT("An initializer std::list was used to initialize an "
                                    "unimplemented or "
                                    "incorrect type "s +
                                    varType->getTypeClassName() + ".");
                    }
                } else {
                    Path::EvalResult eval = path->evalExpr(initExpr);

                    size_t n = eval.second.size();
                    for (size_t i = 0; i < n; ++i) {
                        auto newPath  = (i == 0) ? std::move(path) : std::move(eval.first[i - 1]);
                        auto newValue = std::move(eval.second[i]);

                        if (newValue->isUnknown()) {
                            auto pointAfterDecl = symbolic::SourcePoint::fromStmtAfter(
                                declStmt, context_.getSourceManager(), context_.getLangOptions());

                            auto varType = varDecl->getType();
                            newValue     = symbolic::getSymbol(
                                varType, varAddr->addressClone().into_underlying(), pointAfterDecl);
                        }

                        newPath->updateVarState(varDecl, std::move(newValue));
                        updatedPaths.push_back(std::move(newPath));
                    }
                    continue;
                }
            }

            paths_ = std::move(updatedPaths);
        }
    }

    std::pair<std::unique_ptr<ProgramState>, std::unique_ptr<ProgramState>> ProgramState::
        splitActiveInactive() {
        auto activeState           = std::make_unique<ProgramState>(func_->clone(), context_);
        activeState->startPoint_   = startPoint_;
        auto inactiveState         = std::make_unique<ProgramState>(func_->clone(), context_);
        inactiveState->startPoint_ = startPoint_;

        for (auto &path : paths_) {
            if (path->isActive())
                activeState->paths_.push_back(std::move(path));
            else
                inactiveState->paths_.push_back(std::move(path));
        }
        paths_.clear();
        return {std::move(activeState), std::move(inactiveState)};
    }

    std::unique_ptr<ProgramState> ProgramState::merge(
        const std::vector<const ProgramState *> &states) {
        if (states.empty())
            ERROR("Nothing to be merged.");
        std::optional<utils::not_null<std::unique_ptr<ProgramState>>> merged{};

        for (auto &state : states) {
            if (merged == std::nullopt) {
                merged.emplace(state->clone());
                continue;
            }
            if (*state->getFunction() != *merged.value()->getFunction())
                ERROR("States to be merged are dealing with different functions.");
            if (&state->getContext() != &merged.value()->getContext())
                ERROR("States to be merged have different context.");
            if (state->getStartPoint() != merged.value()->getStartPoint())
                ERROR("States to be merged have different start point.");

            for (const auto &path : state->paths_)
                merged.value()->paths_.push_back(path->clone());
        }
        return std::move(merged).value().into_underlying();
    }

    std::unique_ptr<ProgramState> ProgramState::merge(
        const std::vector<std::unique_ptr<ProgramState>> &states) {
        if (states.empty())
            ERROR("Nothing to be merged.");
        std::optional<utils::not_null<std::unique_ptr<ProgramState>>> merged{};

        for (auto &state : states) {
            if (merged == std::nullopt) {
                merged.emplace(state->clone());
                continue;
            }
            if (*state->getFunction() != *merged.value()->getFunction())
                ERROR("States to be merged are dealing with different functions.");
            if (&state->getContext() != &merged.value()->getContext())
                ERROR("States to be merged have different context.");
            if (state->getStartPoint() != merged.value()->getStartPoint())
                ERROR("States to be merged have different start point.");

            for (const auto &path : state->paths_)
                merged.value()->paths_.push_back(path->clone());
        }
        return std::move(merged).value().into_underlying();
    }

    std::unique_ptr<ProgramState> ProgramState::clone(bool withPath) const {
        auto newState         = std::make_unique<ProgramState>(func_->clone(), context_);
        newState->startPoint_ = startPoint_;

        if (withPath) {
            for (const auto &path : paths_) {
                newState->paths_.push_back(path->clone());
            }
        }

        newState->stmtCtx_ = stmtCtx_;
        return newState;
    }

    std::unique_ptr<ProgramState> ProgramState::cloneWithPaths(
        std::vector<std::unique_ptr<Path>> &newPaths) const {
        auto clone = this->clone(false);
        clone->paths_.clear();
        clone->paths_.reserve(newPaths.size());
        clone->paths_.insert(clone->paths_.end(), make_move_iterator(newPaths.begin()),
                             make_move_iterator(newPaths.end()));
        return clone;
    }

    void ProgramState::resetBreakState() {
        for (auto &path : paths_) {
            if (path->getPathState() == Path::PathState::Break && path->stmtCtx_ == this->stmtCtx_)
                path->setPathState(Path::PathState::Step);
            if (path->getPathState() == Path::PathState::Continue)
                TODO();
        }
    }
    static void collectCaseBlocks(const clang::CompoundStmt *body,
                                  std::vector<std::vector<const clang::Stmt *>> &blocks,
                                  std::vector<const clang::Expr *> &conds) {
        if (!body)
            ERROR("dyn_cast failed for switch body.");
        blocks.clear();
        conds.clear();

        for (const clang::Stmt *top : body->body()) {
            if (auto cs = dyn_cast<const clang::CaseStmt>(top)) {
                auto cur = cs;
                while (cur) {
                    blocks.emplace_back();
                    conds.push_back(cur->getLHS());
                    const clang::Stmt *sub = cur->getSubStmt();
                    if (auto next = dyn_cast<const clang::CaseStmt>(sub)) {
                        cur = next;
                    } else {
                        if (sub) {
                            for (size_t i = 0; i < blocks.size(); ++i)
                                blocks[i].push_back(sub);
                        }
                        break;
                    }
                }
            } else if (auto ds = dyn_cast<const clang::DefaultStmt>(top)) {
                blocks.emplace_back();
                conds.push_back(nullptr);
                const clang::Stmt *sub = ds->getSubStmt();
                if (sub) {
                    for (size_t i = 0; i < blocks.size(); ++i)
                        blocks[i].push_back(sub);
                }
            } else {
                for (size_t i = 0; i < blocks.size(); ++i)
                    blocks[i].push_back(top);
            }
        }
    }

    std::vector<std::pair<std::unique_ptr<ProgramState>, std::unique_ptr<symbolic::SymbolicExpr>>> ProgramState::
        splitStateBySwitchCond(const clang::Expr *switchCond) {
        if (!switchCond) {
            TODO();
        }
        std::vector<std::pair<std::unique_ptr<ProgramState>, std::unique_ptr<symbolic::SymbolicExpr>>>
            result;

        for (auto &path : paths_) {
            auto evalResult = path->evalExpr(switchCond);

            if (evalResult.first.size() != 0) {
                ERROR("evalExpr produced unexpected side paths");
            }

            std::vector<std::unique_ptr<Path>> onePath;
            onePath.push_back(std::move(path).into_underlying());
            auto stateClone = cloneWithPaths(onePath);

            result.emplace_back(std::move(stateClone),
                                std::move(evalResult.second[0]).into_underlying());
        }

        return result;
    }

    void ProgramState::stepSimpleSwitch(const clang::SwitchStmt *switchStmt) {
        auto partitions = splitStateBySwitchCond(switchStmt->getCond());
        std::vector<std::vector<const clang::Stmt *>> blocks;
        std::vector<const clang::Expr *> conds;
        collectCaseBlocks(llvm::dyn_cast<clang::CompoundStmt>(switchStmt->getBody()), blocks,
                          conds);

        std::vector<std::unique_ptr<ProgramState>> finalStates;
        for (auto &pr : partitions) {
            auto current  = std::move(pr.first);
            auto symValue = std::move(pr.second);
            for (size_t i = 0; i < blocks.size(); ++i) {
                auto &stmts    = blocks[i];
                auto *caseCond = conds[i];
                if (caseCond == nullptr) {
                    for (auto *s : stmts)
                        current->step(s);
                    if (current->isInactive())
                        break;
                    continue;
                }

                // TODO: pack a static function in Path.
                Path tmpPath(context_, startPoint_);
                auto caseCondEval = tmpPath.evalExpr(caseCond);
                assert(caseCondEval.second.size() == 1);

                auto caseSymExpr = std::move(caseCondEval.second[0]);
                auto eqState     = current->clone();
                auto &factory    = context_.getExprFactory();

                symbolic::Expr symExpr{factory, factory.importExpr(*symValue)};
                symbolic::Expr caseExpr{factory, factory.importExpr(*caseSymExpr)};
                auto condExprEq = symExpr.equalTo(caseExpr);
                for (auto &p : eqState->paths_)
                    p->insertPathCondition(condExprEq->clone());

                for (auto *s : stmts)
                    eqState->step(s);

                auto condExprNe = symExpr.notEqualTo(caseExpr);
                for (auto &p : current->paths_)
                    p->insertPathCondition(condExprNe->clone());

                if (eqState->isInactive()) {
                    finalStates.push_back(std::move(eqState));
                } else {
                    std::vector<const ProgramState *> mergeInputs;
                    mergeInputs.push_back(eqState.get());
                    mergeInputs.push_back(current.get());
                    auto merged = merge(mergeInputs);
                    current     = std::move(merged);
                }
            }

            finalStates.push_back(std::move(current));
        }

        std::vector<const ProgramState *> ptrs;
        ptrs.reserve(finalStates.size());
        for (auto &st : finalStates)
            ptrs.push_back(st.get());

        auto merged = merge(ptrs);
        paths_      = std::move(merged->paths_);
    }

    bool ProgramState::isInactive() const {
        for (const auto &p : paths_) {
            if (p->isActive()) {
                return false;
            }
        }
        return true;
    }

    void ProgramState::resymbolize(symbolic::SourcePoint newStartPoint) {
        startPoint_ = std::move(newStartPoint);
        for (auto &path : paths_) {
            if (path->isActive()) {
                auto temp = std::move(path);
                temp->resymbolize(startPoint_);
                paths_.clear();
                paths_.push_back(std::move(temp));
                return;
            }
        }
    }

    std::optional<utils::not_null<std::unique_ptr<Path>>> ProgramState::takePath(size_t i) {
        if (i >= paths_.size())
            return std::nullopt;
        auto p = std::move(paths_.at(i));
        paths_.erase(paths_.begin() + i);
        return p;
    }

    std::vector<utils::not_null<std::unique_ptr<Path>>> ProgramState::takeAllPaths() {
        std::vector<utils::not_null<std::unique_ptr<Path>>> out;
        out.swap(paths_);
        return out;
    }

    void ProgramState::setStmtCtx(const clang::Stmt *stmtCtx) {
        for (auto &path : paths_) {
            if (!path->isActive())
                continue;
            assert(path->stmtCtx_ == stmtCtx_);
            path->stmtCtx_ = stmtCtx;
        }
        stmtCtx_ = stmtCtx;
    }
} // namespace acslg::analyzer
