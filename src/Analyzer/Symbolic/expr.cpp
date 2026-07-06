/**
 * @file expr.cpp
 * @brief Implements symbolic expression hierarchy utilities and helpers for cloning, comparison,
 *        simplification, and ACSL conversion.
 */
#include "expr.h"

#include <clang/AST/Type.h>
#include <memory>
#include <optional>
#include <sstream>
#include <cstring>
#include <string_view>
#include <ranges>
#include <llvm/ADT/TypeSwitch.h>
#include <llvm/Support/Casting.h>

#include "aggregateExpr.h" // IWYU pragma: keep
#include "macros.h"
#include "utils.h"
#include "Analyzer/state.h"

namespace acslg::analyzer::symbolic {
    thread_local ExprFactory *ExprFactoryScope::current_ = nullptr;

    ExprFactoryScope::ExprFactoryScope(ExprFactory &factory)
        : previous_(current_) {
        current_ = &factory;
    }

    ExprFactoryScope::~ExprFactoryScope() {
        current_ = previous_;
    }

    ExprFactory &ExprFactoryScope::current() {
        if (current_ == nullptr)
            ERROR("No active ExprFactory.");
        return *current_;
    }

    bool ExprFactoryScope::hasCurrent() {
        return current_ != nullptr;
    }

    ExprHandle ExprFactory::intern(utils::not_null<std::unique_ptr<SymbolicExpr>> node) {
        const auto hash = node->hash();
        auto &bucket    = interned_[hash];

        ExprFactoryScope scope(*this);
        for (const auto *existing : bucket) {
            if (*existing == *node)
                return ExprHandle{existing};
        }

        auto *raw = node.get().get();
        owned_.push_back(std::move(node).into_underlying());
        bucket.push_back(raw);
        return ExprHandle{raw};
    }

    AddrHandle ExprFactory::internAddress(utils::not_null<std::unique_ptr<Address>> node) {
        std::unique_ptr<SymbolicExpr> exprNode = std::move(node).into_underlying();
        auto handle = intern(utils::not_null<std::unique_ptr<SymbolicExpr>>{std::move(exprNode)});
        return AddrHandle{cast<const Address>(handle.get().get())};
    }

    ExprHandle ExprFactory::withValType(ExprHandle expr, SymbolicExpr::Type newType) {
        if (expr->getValType() == newType)
            return expr;
        return intern(expr->cloneWithValType(newType));
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> SymbolicExpr::withValType(
        Type newType) const {
        if (ExprFactoryScope::hasCurrent()) {
            auto &factory = ExprFactoryScope::current();
            return factory.cloneExpr(
                factory.withValType(factory.importExpr(*this), newType));
        }
        return cloneWithValType(newType);
    }

    ExprHandle getSubstitutedValueHandle(ExprFactory &factory,
                                         const SymbolicExpr &expr,
                                         const HashExprHandleMap &hashToExprMap) {
        ExprFactoryScope scope(factory);

        struct Substituter {
            ExprFactory &factory;
            const HashExprHandleMap &substitutions;

            AddrHandle requireAddress(ExprHandle handle) const {
                if (auto *addr = handle.dyn_cast<const Address>())
                    return factory.importAddress(*addr);
                UNREACHABLE();
            }

            const SymbolAddress &requireRange(ExprHandle handle) const {
                if (auto *range = handle.dyn_cast<const SymbolAddress>()) {
                    if (range->getLength() == std::nullopt)
                        ERROR("Substituted expression should be a *range*");
                    return *range;
                }
                UNREACHABLE();
            }

            ExprHandle run(const SymbolicExpr &expr) const {
                if (auto it = substitutions.find(expr.hash()); it != substitutions.end())
                    return it->second;

                if (auto *literal = dyn_cast<const detail::LiteralExprNode>(&expr))
                    return literal->importInto(factory);
                if (isa<UnknownExpr>(&expr))
                    return factory.unknown();
                if (auto *rangeIndex = dyn_cast<const SymbolAddress::RangeIndex>(&expr))
                    return factory.rangeIndex(rangeIndex->getName());
                if (auto *varAddr = dyn_cast<const VariableAddress>(&expr))
                    return factory.variableAddress(varAddr->getFrom()).asExpr();
                if (auto *fieldAddr = dyn_cast<const FieldAddress>(&expr)) {
                    auto base = requireAddress(run(*fieldAddr->getBaseAddr()));
                    return factory
                        .fieldAddress(fieldAddr->getPointeeType(),
                                      fieldAddr->getDefinition(), base,
                                      fieldAddr->getFieldIndex())
                        .asExpr();
                }
                if (auto *symbolValue = dyn_cast<const SymbolValue>(&expr)) {
                    auto fromAddr = symbolValue->getFromAddr();
                    if (fromAddr == std::nullopt)
                        UNREACHABLE();
                    auto from = requireAddress(run(*fromAddr.value()));
                    return factory.symbolValue(symbolValue->getValType(), from,
                                               symbolValue->getFromPoint().value());
                }
                if (auto *symbolAddr = dyn_cast<const SymbolAddress>(&expr)) {
                    std::optional<AddrHandle> from;
                    if (auto fromAddr = symbolAddr->getFromAddr())
                        from = requireAddress(run(*fromAddr.value()));

                    std::optional<ExprHandle> length;
                    if (symbolAddr->getLength())
                        length = run(*symbolAddr->getLength().value());

                    return factory
                        .symbolAddress(symbolAddr->getPointeeType(), from,
                                       symbolAddr->getFromPoint().value(),
                                       run(*symbolAddr->getOffset()), length)
                        .asExpr();
                }
                if (auto *binary = dyn_cast<const BinaryOpExpr>(&expr)) {
                    return factory.binary(run(*binary->getLeft()), binary->getOperator(),
                                          run(*binary->getRight()));
                }
                if (auto *unary = dyn_cast<const UnaryOpExpr>(&expr))
                    return factory.unary(unary->getOperator(), run(*unary->getSub()));
                if (auto *structure = dyn_cast<const Structure>(&expr)) {
                    auto rebuilt = factory.importExpr(*structure);
                    for (size_t i = 0; i < structure->getNumFields(); ++i)
                        rebuilt = factory.withField(rebuilt, i,
                                                    run(*structure->getFieldValue(i)));
                    return rebuilt;
                }
                if (auto *sum = dyn_cast<const SumOverRange>(&expr)) {
                    return makeSumOverRangeHandle(factory, requireRange(run(sum->getRange())),
                                                  sum->getIndexName(),
                                                  sum->getFromPoint().value());
                }
                if (auto *quantifier = dyn_cast<const QuantifierOverRange>(&expr)) {
                    return makeQuantifierOverRangeHandle(
                        factory, requireRange(run(quantifier->getRange())),
                        quantifier->getIndexName(), quantifier->getQuantifier(),
                        *run(quantifier->getPredicate()));
                }
                if (auto *maxMin = dyn_cast<const MaxMinOverRange>(&expr)) {
                    return makeMaxMinOverRangeHandle(
                        factory, requireRange(run(maxMin->getRange())),
                        maxMin->getIndexName(), maxMin->getExtremum(),
                        run(maxMin->getExpr()), maxMin->getFromPoint().value());
                }

                ERROR("Unsupported SymbolicExpr node in handle value substitution.");
            }
        };

        return Substituter{factory, hashToExprMap}.run(expr);
    }

    ExprHandle getSubstitutedExprHandle(ExprFactory &factory,
                                        const SymbolicExpr &expr,
                                        const Path &pathSubTo,
                                        const SourcePoint &pointToSub) {
        ExprFactoryScope scope(factory);

        struct Substituter {
            ExprFactory &factory;
            const Path &pathSubTo;
            const SourcePoint &pointToSub;

            AddrHandle requireAddress(ExprHandle handle) const {
                if (auto *addr = handle.dyn_cast<const Address>())
                    return factory.importAddress(*addr);
                UNREACHABLE();
            }

            const SymbolAddress &requireRange(ExprHandle handle) const {
                if (auto *range = handle.dyn_cast<const SymbolAddress>()) {
                    if (range->getLength() == std::nullopt)
                        ERROR("Substituted expression should be a *range*");
                    return *range;
                }
                ERROR("Substituted expression should be a *range*");
            }

            ExprHandle simplified(ExprHandle handle) const {
                return factory.importExpr(*handle->simplifiedExpr());
            }

            ExprHandle run(const SymbolicExpr &expr) const {
                if (auto *literal = dyn_cast<const detail::LiteralExprNode>(&expr))
                    return literal->importInto(factory);
                if (isa<UnknownExpr>(&expr))
                    return factory.unknown();
                if (auto *rangeIndex = dyn_cast<const SymbolAddress::RangeIndex>(&expr))
                    return factory.rangeIndex(rangeIndex->getName());
                if (auto *varAddr = dyn_cast<const VariableAddress>(&expr))
                    return factory.variableAddress(varAddr->getFrom()).asExpr();
                if (auto *fieldAddr = dyn_cast<const FieldAddress>(&expr)) {
                    auto base = requireAddress(run(*fieldAddr->getBaseAddr()));
                    return factory
                        .fieldAddress(fieldAddr->getPointeeType(),
                                      fieldAddr->getDefinition(), base,
                                      fieldAddr->getFieldIndex())
                        .asExpr();
                }
                if (auto *symbolValue = dyn_cast<const SymbolValue>(&expr)) {
                    auto fromPoint = symbolValue->getFromPoint();
                    if (fromPoint && fromPoint.value() != pointToSub)
                        return factory.importExpr(expr);

                    auto fromAddr = symbolValue->getFromAddr();
                    if (fromAddr == std::nullopt)
                        UNREACHABLE();
                    auto realFromAddr = requireAddress(run(*fromAddr.value()));
                    if (auto value = pathSubTo.getMemoryState().readHandle(*realFromAddr))
                        return factory.importExpr(*value.value());

                    return factory.symbolValue(symbolValue->getValType(), realFromAddr,
                                               pathSubTo.getStartPoint());
                }
                if (auto *symbolAddr = dyn_cast<const SymbolAddress>(&expr)) {
                    auto fromPoint = symbolAddr->getFromPoint();
                    if (fromPoint && fromPoint.value() != pointToSub)
                        return factory.importExpr(expr);

                    std::optional<ExprHandle> length;
                    if (symbolAddr->getLength())
                        length = factory.importExpr(*symbolAddr->getLength().value());

                    auto fromAddr = symbolAddr->getFromAddr();
                    if (fromAddr == std::nullopt)
                        return factory
                            .symbolAddress(symbolAddr->getPointeeType(), std::nullopt,
                                           pointToSub, factory.importExpr(*symbolAddr->getOffset()),
                                           length)
                            .asExpr();

                    auto realFromAddr = requireAddress(run(*fromAddr.value()));
                    auto offset       = simplified(run(*symbolAddr->getOffset()));
                    if (symbolAddr->getLength())
                        length = simplified(run(*symbolAddr->getLength().value()));

                    if (auto value = pathSubTo.getMemoryState().readHandle(*realFromAddr)) {
                        auto realAddr = value.value()->tryEvalAsSymbolAddr();
                        if (realAddr == std::nullopt)
                            ERROR("This expr should be a `SymbolAddress");

                        Addr concreteAddr{factory, factory.importAddress(*realAddr.value())};
                        concreteAddr = concreteAddr.withAddedOffset(Expr{factory, offset});
                        if (length)
                            concreteAddr = concreteAddr.withLength(Expr{factory, length.value()});
                        return concreteAddr.asExpr().handle();
                    }

                    return factory
                        .symbolAddress(symbolAddr->getPointeeType(), realFromAddr,
                                       pathSubTo.getStartPoint(), offset, length)
                        .asExpr();
                }
                if (auto *binary = dyn_cast<const BinaryOpExpr>(&expr)) {
                    return factory.binary(run(*binary->getLeft()), binary->getOperator(),
                                          run(*binary->getRight()));
                }
                if (auto *unary = dyn_cast<const UnaryOpExpr>(&expr))
                    return factory.unary(unary->getOperator(), run(*unary->getSub()));
                if (auto *structure = dyn_cast<const Structure>(&expr)) {
                    auto rebuilt = factory.importExpr(*structure);
                    for (size_t i = 0; i < structure->getNumFields(); ++i)
                        rebuilt = factory.withField(rebuilt, i,
                                                    run(*structure->getFieldValue(i)));
                    return rebuilt;
                }
                if (auto *sum = dyn_cast<const SumOverRange>(&expr)) {
                    if (sum->getFromPoint().value() != pointToSub)
                        return factory.importExpr(expr);
                    return makeSumOverRangeHandle(factory, requireRange(run(sum->getRange())),
                                                  sum->getIndexName(),
                                                  pathSubTo.getStartPoint());
                }
                if (auto *quantifier = dyn_cast<const QuantifierOverRange>(&expr)) {
                    return makeQuantifierOverRangeHandle(
                        factory, requireRange(run(quantifier->getRange())),
                        quantifier->getIndexName(), quantifier->getQuantifier(),
                        *run(quantifier->getPredicate()));
                }
                if (auto *maxMin = dyn_cast<const MaxMinOverRange>(&expr)) {
                    auto range = requireRange(run(maxMin->getRange()));
                    auto body  = run(maxMin->getExpr());
                    if (maxMin->getFromPoint().value() == pointToSub)
                        TODO();
                    return makeMaxMinOverRangeHandle(factory, range, maxMin->getIndexName(),
                                                     maxMin->getExtremum(), body,
                                                     maxMin->getFromPoint().value());
                }

                ERROR("Unsupported SymbolicExpr node in handle path substitution.");
            }
        };

        return Substituter{factory, pathSubTo, pointToSub}.run(expr);
    }

    ExprHandle getRangeIndexSubstitutedHandle(ExprFactory &factory,
                                              const SymbolicExpr &expr,
                                              const SymbolAddrBaseInfo &rangeBase,
                                              ExprHandle indexExpr) {
        ExprFactoryScope scope(factory);

        struct Substituter {
            ExprFactory &factory;
            const SymbolAddrBaseInfo &rangeBase;
            ExprHandle indexExpr;

            AddrHandle requireAddress(ExprHandle handle) const {
                if (auto *addr = handle.dyn_cast<const Address>())
                    return factory.importAddress(*addr);
                UNREACHABLE();
            }

            const SymbolAddress &requireRange(ExprHandle handle) const {
                if (auto *range = handle.dyn_cast<const SymbolAddress>()) {
                    if (range->getLength() == std::nullopt)
                        ERROR("Substituted expression should be a *range*");
                    return *range;
                }
                UNREACHABLE();
            }

            ExprHandle run(const SymbolicExpr &expr) const {
                (void)rangeBase;

                if (auto *literal = dyn_cast<const detail::LiteralExprNode>(&expr))
                    return literal->importInto(factory);
                if (isa<UnknownExpr>(&expr))
                    return factory.unknown();
                if (isa<SymbolAddress::RangeIndex>(&expr))
                    return indexExpr;
                if (auto *varAddr = dyn_cast<const VariableAddress>(&expr))
                    return factory.variableAddress(varAddr->getFrom()).asExpr();
                if (auto *fieldAddr = dyn_cast<const FieldAddress>(&expr)) {
                    auto base = requireAddress(run(*fieldAddr->getBaseAddr()));
                    return factory
                        .fieldAddress(fieldAddr->getPointeeType(),
                                      fieldAddr->getDefinition(), base,
                                      fieldAddr->getFieldIndex())
                        .asExpr();
                }
                if (auto *symbolValue = dyn_cast<const SymbolValue>(&expr)) {
                    auto fromAddr = symbolValue->getFromAddr();
                    if (fromAddr == std::nullopt)
                        UNREACHABLE();
                    auto from = requireAddress(run(*fromAddr.value()));
                    return factory.symbolValue(symbolValue->getValType(), from,
                                               symbolValue->getFromPoint().value());
                }
                if (auto *symbolAddr = dyn_cast<const SymbolAddress>(&expr)) {
                    std::optional<AddrHandle> from;
                    if (auto fromAddr = symbolAddr->getFromAddr())
                        from = requireAddress(run(*fromAddr.value()));

                    std::optional<ExprHandle> length;
                    if (symbolAddr->getLength())
                        length = run(*symbolAddr->getLength().value());

                    return factory
                        .symbolAddress(symbolAddr->getPointeeType(), from,
                                       symbolAddr->getFromPoint().value(),
                                       run(*symbolAddr->getOffset()), length)
                        .asExpr();
                }
                if (auto *binary = dyn_cast<const BinaryOpExpr>(&expr))
                    return factory.binary(run(*binary->getLeft()), binary->getOperator(),
                                          run(*binary->getRight()));
                if (auto *unary = dyn_cast<const UnaryOpExpr>(&expr))
                    return factory.unary(unary->getOperator(), run(*unary->getSub()));
                if (auto *structure = dyn_cast<const Structure>(&expr)) {
                    auto rebuilt = factory.importExpr(*structure);
                    for (size_t i = 0; i < structure->getNumFields(); ++i)
                        rebuilt = factory.withField(rebuilt, i,
                                                    run(*structure->getFieldValue(i)));
                    return rebuilt;
                }
                if (auto *sum = dyn_cast<const SumOverRange>(&expr)) {
                    return makeSumOverRangeHandle(factory, requireRange(run(sum->getRange())),
                                                  sum->getIndexName(),
                                                  sum->getFromPoint().value());
                }
                if (auto *quantifier = dyn_cast<const QuantifierOverRange>(&expr)) {
                    return makeQuantifierOverRangeHandle(
                        factory, requireRange(run(quantifier->getRange())),
                        quantifier->getIndexName(), quantifier->getQuantifier(),
                        *run(quantifier->getPredicate()));
                }
                if (auto *maxMin = dyn_cast<const MaxMinOverRange>(&expr)) {
                    return makeMaxMinOverRangeHandle(
                        factory, requireRange(run(maxMin->getRange())),
                        maxMin->getIndexName(), maxMin->getExtremum(),
                        run(maxMin->getExpr()), maxMin->getFromPoint().value());
                }

                ERROR("Unsupported SymbolicExpr node in handle range-index substitution.");
            }
        };

        return Substituter{factory, rangeBase, indexExpr}.run(expr);
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> ExprChild::clone() const {
        if (ExprFactoryScope::hasCurrent()) {
            auto &factory = ExprFactoryScope::current();
            return factory.importAndCloneExpr(*get());
        }
        return get()->clone();
    }

    ExprChild ExprChild::copy() const {
        if (ExprFactoryScope::hasCurrent())
            return ExprChild{ExprFactoryScope::current().importExpr(*get())};
        if (handle_)
            return ExprChild{*handle_};
        return ExprChild{owned_->get()->clone()};
    }

    AddrHandle ExprFactory::importAddress(const Address &address) {
        return AddrHandle{cast<const Address>(importExpr(address).get().get())};
    }

    ExprHandle ExprFactory::importExpr(const SymbolicExpr &expr) {
        auto preserveImportedType = [this, &expr](ExprHandle imported) {
            if (imported->getValType() == expr.getValType())
                return imported;
            return withValType(imported, expr.getValType());
        };

        if (auto *literal = dyn_cast<detail::LiteralExprNode>(&expr))
            return preserveImportedType(literal->importInto(*this));

        if (isa<detail::UnknownExprNode>(&expr))
            return preserveImportedType(unknown());

        if (auto *index = dyn_cast<SymbolAddress::RangeIndex>(&expr))
            return preserveImportedType(rangeIndex(index->getName()));

        if (auto *unaryExpr = dyn_cast<detail::UnaryOpExprNode>(&expr))
            return preserveImportedType(
                unary(unaryExpr->getOperator(), importExpr(*unaryExpr->getSub())));

        if (auto *binaryExpr = dyn_cast<detail::BinaryOpExprNode>(&expr)) {
            auto left  = importExpr(*binaryExpr->getLeft());
            auto right = importExpr(*binaryExpr->getRight());
            return preserveImportedType(binary(left, binaryExpr->getOperator(), right));
        }

        if (auto *symbolAddr = dyn_cast<SymbolAddress>(&expr)) {
            auto base = symbolAddr->getBaseInfo();
            std::optional<AddrHandle> from;
            if (base.fromAddr_)
                from = internAddress(base.fromAddr_.value()->addressClone());

            std::optional<ExprHandle> length;
            if (const auto &legacyLength = symbolAddr->getLength(); legacyLength)
                length = importExpr(*legacyLength.value());

            return symbolAddress(base.pointeeType_, from, base.fromPoint_,
                                 importExpr(*symbolAddr->getOffset()), length)
                .asExpr();
        }

        if (auto *structure = dyn_cast<Structure>(&expr)) {
            std::vector<ExprHandle> fields;
            fields.reserve(structure->getNumFields());
            for (auto field : structure->fieldsValues())
                fields.push_back(importExpr(*field));
            return intern(std::make_unique<Structure>(structure->getInfo(), std::move(fields)));
        }

        if (auto *sum = dyn_cast<SumOverRange>(&expr)) {
            auto fromPoint = sum->getFromPoint();
            if (!fromPoint)
                ERROR("SumOverRange must have a source point.");
            return makeSumOverRangeHandle(
                *this, sum->getRange(), sum->getIndexName(), fromPoint.value());
        }

        if (auto *quantifier = dyn_cast<QuantifierOverRange>(&expr)) {
            return makeQuantifierOverRangeHandle(
                *this, quantifier->getRange(), quantifier->getIndexName(),
                quantifier->getQuantifier(), quantifier->getPredicate());
        }

        if (auto *maxMin = dyn_cast<MaxMinOverRange>(&expr)) {
            auto fromPoint = maxMin->getFromPoint();
            if (!fromPoint)
                ERROR("MaxMinOverRange must have a source point.");
            return makeMaxMinOverRangeHandle(
                *this, maxMin->getRange(), maxMin->getIndexName(), maxMin->getExtremum(),
                maxMin->getExpr(), fromPoint.value());
        }

        return intern(expr.clone());
    }

    namespace {
        std::optional<utils::not_null<std::unique_ptr<const Address>>> cloneAddress(
            std::optional<AddrHandle> handle) {
            if (!handle)
                return std::nullopt;
            std::unique_ptr<const Address> cloned =
                handle.value()->addressClone().into_underlying();
            return utils::not_null<std::unique_ptr<const Address>>{std::move(cloned)};
        }

        template <class... Ts> struct overloaded : Ts... {
            using Ts::operator()...;
        };

        template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

        using Type       = SymbolicExpr::Type;
        using ScalarKind = SymbolicExpr::ScalarKind;

        inline bool isIntLike(ScalarKind k) {
            return k == ScalarKind::Int || k == ScalarKind::UInt || k == ScalarKind::Bool;
        }

        inline uint64_t maskN(unsigned n) {
            return (n >= 64) ? ~uint64_t(0) : ((uint64_t(1) << n) - 1);
        }

        inline Type unify(Type a, Type b) {
            // Choose the wider integer type so arithmetic results have a consistent bit width.
            if (!isIntLike(a.kind) || !isIntLike(b.kind))
                return {ScalarKind::Void, 0};
            unsigned bw = std::max(a.bitWidth ? a.bitWidth : 1u, b.bitWidth ? b.bitWidth : 1u);
            if (bw <= 8)
                bw = 8;
            else if (bw <= 16)
                bw = 16;
            else if (bw <= 32)
                bw = 32;
            else
                bw = 64;
            bool uns = (a.kind == ScalarKind::UInt) || (b.kind == ScalarKind::UInt);
            return {uns ? ScalarKind::UInt : ScalarKind::Int, bw};
        }

        inline uint64_t coerceU(unsigned bw, uint64_t x) { return x & maskN(bw ? bw : 64); }
        inline int64_t coerceS(unsigned bw, uint64_t x) {
            x &= maskN(bw ? bw : 64);
            if (bw < 64 && (x & (uint64_t(1) << (bw - 1))))
                x |= ~maskN(bw);
            return (int64_t)x;
        }

        inline const detail::LiteralExprNode *literalNode(ExprHandle handle) {
            return cast<const detail::LiteralExprNode>(handle.get().get());
        }

        inline utils::not_null<std::unique_ptr<SymbolicExpr>> cloneConstLiteral(
            const detail::LiteralExprNode &literal) {
            return ExprFactoryScope::current().cloneExpr(ExprHandle{&literal});
        }

        inline const detail::LiteralExprNode *makeLiteralFromUnifiedType(Type t,
                                                                         bool asBool,
                                                                         uint64_t raw) {
            auto &factory = ExprFactoryScope::current();
            if (asBool)
                return literalNode(factory.literal(asBool));

            unsigned bw = t.bitWidth ? t.bitWidth : 64;
            if (t.kind == ScalarKind::UInt) {
                uint64_t u = coerceU(bw, raw);
                if (bw <= 16)
                    return literalNode(factory.literal((unsigned short)u));
                if (bw <= 32)
                    return literalNode(factory.literal((unsigned int)u));
                return literalNode(factory.literal((uint64_t)u));
            } else { // Int
                int64_t s = coerceS(bw, raw);
                if (bw <= 16)
                    return literalNode(factory.literal((short)s));
                if (bw <= 32)
                    return literalNode(factory.literal((int)s));
                return literalNode(factory.literal((int64_t)s));
            }
        }

        inline bool isBooleanExpr(const SymbolicExpr &e) {
            if (auto *lit = dyn_cast<detail::LiteralExprNode>(&e))
                return lit->getLiteralValue() == 0 || lit->getLiteralValue() == 1 ||
                       e.getValType().kind == ScalarKind::Bool;

            if (auto *bo = dyn_cast<BinaryOpExpr>(&e)) {
                using BO = detail::BinaryOpExprNode::Operator;
                switch (bo->getOperator()) {
                    case BO::LogicalAnd:
                    case BO::LogicalOr:
                    case BO::LessThan:
                    case BO::GreaterThan:
                    case BO::LessEqual:
                    case BO::GreaterEqual:
                    case BO::Equal:
                    case BO::NotEqual: return true;
                    default: break;
                }
            }

            if (auto *uo = dyn_cast<UnaryOpExpr>(&e)) {
                using UO = detail::UnaryOpExprNode::Operator;
                if (uo->getOperator() == UO::LogicalNot)
                    return true;
            }

            return e.getValType().kind == ScalarKind::Bool;
        }

        inline uint64_t literalRawU(const detail::LiteralExprNode &L) {
            switch (L.getLiteralType()) {
                using enum detail::LiteralExprNode::LiteralType;
                case Boolean: return L.getLiteralValue() != 0 ? 1u : 0u;
                case Int: return (uint64_t)(int64_t)L.getLiteralValue();
                case UnsignedInt: return (uint64_t)L.getLiteralValue();
                case Short: return (uint64_t)(int64_t)L.getLiteralValue();
                case UnsignedShort: return (uint64_t)L.getLiteralValue();
                case Int64: return (uint64_t)(int64_t)L.getLiteralValue();
                case UInt64: return (uint64_t)L.getLiteralValue();
            }
            return 0;
        }
        inline bool literalAsBool(const detail::LiteralExprNode &L) { return L.getLiteralValue() != 0; }
    } // namespace

    ExprHandle ExprFactory::rangeIndex(std::string_view name) {
        return intern(std::make_unique<SymbolAddress::RangeIndex>(name));
    }

    ExprHandle ExprFactory::symbolValue(SymbolicExpr::Type varType,
                                        AddrHandle from,
                                        SourcePoint fromPoint) {
        auto clonedFrom = cloneAddress(from);
        if (!clonedFrom)
            ERROR("SymbolValue requires a source address.");
        return intern(std::make_unique<SymbolValue>(
            varType, std::move(clonedFrom.value()), std::move(fromPoint)));
    }

    ExprHandle ExprFactory::simplifiedBinary(ExprHandle left,
                                             BinaryOpExpr::Operator op,
                                             ExprHandle right) {
        auto simplified = std::make_unique<detail::BinaryOpExprNode>(
                              cloneExpr(left), op, cloneExpr(right))
                              ->simplifiedExpr();
        return importExpr(*simplified);
    }

    AddrHandle ExprFactory::variableAddress(utils::not_null<const clang::VarDecl *> from) {
        return internAddress(std::make_unique<VariableAddress>(from));
    }

    AddrHandle ExprFactory::symbolAddress(
        clang::QualType pointeeType,
        std::optional<AddrHandle> from,
        SourcePoint fromPoint,
        std::optional<ExprHandle> offset,
        std::optional<ExprHandle> length) {
        auto resolvedOffset =
            offset.value_or(literal(static_cast<int64_t>(SymbolAddress::ZERO_OFFSET)));
        return internAddress(std::make_unique<SymbolAddress>(
            pointeeType, cloneAddress(from), std::move(fromPoint), resolvedOffset, length));
    }

    AddrHandle ExprFactory::withOffset(AddrHandle address, ExprHandle offset) {
        const auto &symbolAddr = address.cast<SymbolAddress>();
        auto base              = symbolAddr.getBaseInfo();

        std::optional<AddrHandle> from;
        if (base.fromAddr_)
            from = internAddress(base.fromAddr_.value()->addressClone());

        std::optional<ExprHandle> length;
        if (const auto &existingLength = symbolAddr.getLength(); existingLength)
            length = importExpr(*existingLength.value());

        return symbolAddress(base.pointeeType_, from, base.fromPoint_, offset, length);
    }

    AddrHandle ExprFactory::withAddedOffset(AddrHandle address, ExprHandle extra) {
        const auto &symbolAddr = address.cast<SymbolAddress>();
        auto newOffset = simplifiedBinary(importExpr(*symbolAddr.getOffset()),
                                          detail::BinaryOpExprNode::Operator::Add, extra);
        return withOffset(address, newOffset);
    }

    AddrHandle ExprFactory::withSubtractedOffset(AddrHandle address, ExprHandle extra) {
        const auto &symbolAddr = address.cast<SymbolAddress>();
        auto newOffset = simplifiedBinary(importExpr(*symbolAddr.getOffset()),
                                          detail::BinaryOpExprNode::Operator::Subtract, extra);
        return withOffset(address, newOffset);
    }

    AddrHandle ExprFactory::withLength(AddrHandle address, ExprHandle length) {
        const auto &symbolAddr = address.cast<SymbolAddress>();
        auto base              = symbolAddr.getBaseInfo();

        std::optional<AddrHandle> from;
        if (base.fromAddr_)
            from = internAddress(base.fromAddr_.value()->addressClone());

        return symbolAddress(base.pointeeType_, from, base.fromPoint_,
                             importExpr(*symbolAddr.getOffset()), length);
    }

    AddrHandle ExprFactory::withAddedLength(AddrHandle address, ExprHandle extra) {
        const auto &symbolAddr = address.cast<SymbolAddress>();
        auto currentLength =
            symbolAddr.getLength() ? importExpr(*symbolAddr.getLength().value()) : literal(1);
        auto newLength = simplifiedBinary(currentLength, detail::BinaryOpExprNode::Operator::Add,
                                          extra);
        return withLength(address, newLength);
    }

    AddrHandle ExprFactory::withoutLength(AddrHandle address) {
        const auto &symbolAddr = address.cast<SymbolAddress>();
        auto base              = symbolAddr.getBaseInfo();

        std::optional<AddrHandle> from;
        if (base.fromAddr_)
            from = internAddress(base.fromAddr_.value()->addressClone());

        return symbolAddress(base.pointeeType_, from, base.fromPoint_,
                             importExpr(*symbolAddr.getOffset()), std::nullopt);
    }

    AddrHandle ExprFactory::fieldAddress(clang::QualType pointeeType,
                                         const clang::RecordDecl *record,
                                         AddrHandle baseAddr,
                                         size_t fieldIndex) {
        std::unique_ptr<const Address> base = baseAddr->addressClone().into_underlying();
        return internAddress(std::make_unique<FieldAddress>(
            pointeeType, record, utils::not_null<std::unique_ptr<const Address>>{std::move(base)},
            fieldIndex));
    }

    ExprHandle ExprFactory::structure(const clang::RecordDecl *record,
                                      const clang::ASTRecordLayout &layout,
                                      AddrHandle from,
                                      SourcePoint fromPoint) {
        if (!record || !record->isCompleteDefinition())
            ERROR("Incomplete struct definition");
        record = record->getDefinition();

        std::vector<ExprHandle> fields;
        fields.reserve(layout.getFieldCount());
        for (auto field : record->fields()) {
            const auto index    = field->getFieldIndex();
            clang::QualType fty = field->getType();
            auto fieldAddr      = fieldAddress(fty, record, from, index);

            if (fty->isStructureType()) {
                auto nestedRD = fty->getAsRecordDecl();
                if (!nestedRD || !nestedRD->isCompleteDefinition())
                    ERROR("Incomplete nested struct definition");
                nestedRD           = nestedRD->getDefinition();
                auto &nestedLayout = nestedRD->getASTContext().getASTRecordLayout(nestedRD);
                fields.push_back(structure(nestedRD, nestedLayout, fieldAddr, fromPoint));
            } else if (fty->isPointerType()) {
                fields.push_back(
                    symbolAddress(fty, std::optional<AddrHandle>{fieldAddr}, fromPoint).asExpr());
            } else if (fty->isArrayType()) {
                auto arrayType = llvm::cast<clang::ArrayType>(fty);
                auto elemTy    = arrayType->getElementType();
                std::optional<ExprHandle> length;
                if (auto *cat = llvm::dyn_cast<clang::ConstantArrayType>(fty.getTypePtr()))
                    length = literal(cat->getSize().getZExtValue());
                fields.push_back(symbolAddress(elemTy, std::optional<AddrHandle>{fieldAddr},
                                               fromPoint, std::nullopt, length)
                                     .asExpr());
            } else {
                fields.push_back(symbolValue(deriveType(fty), fieldAddr, fromPoint));
            }
        }

        if (fields.size() != layout.getFieldCount())
            UNREACHABLE();
        return intern(std::make_unique<Structure>(Structure::Info{record, layout},
                                                  std::move(fields)));
    }

    ExprHandle ExprFactory::withField(ExprHandle structure, size_t index, ExprHandle value) {
        const auto &structureNode = structure.cast<Structure>();
        if (index >= structureNode.getNumFields())
            ERROR("Out-of-bounds access");

        std::vector<ExprHandle> fields;
        fields.reserve(structureNode.getNumFields());
        size_t currentIndex = 0;
        for (auto field : structureNode.fieldsValues()) {
            fields.push_back(currentIndex == index ? value : importExpr(*field));
            ++currentIndex;
        }

        return intern(std::make_unique<Structure>(structureNode.getInfo(), std::move(fields)));
    }

    namespace {
        utils::not_null<std::unique_ptr<SymbolicExpr>> importThroughCurrentFactory(
            const SymbolicExpr &expr) {
            return ExprFactoryScope::current().importAndCloneExpr(expr);
        }

        ExprChild makeDefaultSymbolAddressOffsetChild() {
            auto &factory = ExprFactoryScope::current();
            return ExprChild{factory.literal(static_cast<int64_t>(SymbolAddress::ZERO_OFFSET))};
        }

        ExprChild makeSymbolAddressOffsetChild(
            std::optional<utils::not_null<std::unique_ptr<const SymbolicExpr>>> offset) {
            if (offset != std::nullopt)
                return ExprChild::fromConstOwned(std::move(offset.value()));
            return makeDefaultSymbolAddressOffsetChild();
        }

        ExprChild makeSymbolAddressOffsetChild(std::optional<ExprHandle> offset) {
            if (offset != std::nullopt)
                return ExprChild{offset.value()};
            return makeDefaultSymbolAddressOffsetChild();
        }
    } // namespace

    utils::not_null<std::unique_ptr<SymbolicExpr>> makeLiteralExpr(int64_t value) {
        return ExprFactoryScope::current().cloneExpr(ExprFactoryScope::current().literal(value));
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> makeUnaryExpr(
        UnaryOpExpr::Operator op,
        utils::not_null<std::unique_ptr<SymbolicExpr>> expr) {
        auto &factory = ExprFactoryScope::current();
        return factory.cloneExpr(factory.unary(op, factory.importExpr(*expr)));
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> makeBinaryExpr(
        utils::not_null<std::unique_ptr<SymbolicExpr>> lhs,
        BinaryOpExpr::Operator op,
        utils::not_null<std::unique_ptr<SymbolicExpr>> rhs) {
        auto &factory = ExprFactoryScope::current();
        return factory.cloneExpr(
            factory.binary(factory.importExpr(*lhs), op, factory.importExpr(*rhs)));
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> makeRangeIndexExpr(std::string_view name) {
        return ExprFactoryScope::current().cloneExpr(ExprFactoryScope::current().rangeIndex(name));
    }

    std::unique_ptr<SymbolValue> cloneSymbolValue(ExprHandle value) {
        return std::make_unique<SymbolValue>(value.cast<SymbolValue>());
    }

    std::unique_ptr<SymbolValue> makeSymbolValue(ExprFactory &factory,
                                                 SymbolicExpr::Type varType,
                                                 std::unique_ptr<Address> from,
                                                 SourcePoint fromPoint) {
        Addr fromAddr{factory, factory.importAddress(*from)};
        return cloneSymbolValue(
            factory.symbolValue(varType, fromAddr.handle(), std::move(fromPoint)));
    }

    std::unique_ptr<SymbolAddress> cloneSymbolAddress(AddrHandle address) {
        return std::make_unique<SymbolAddress>(address.cast<SymbolAddress>());
    }

    std::unique_ptr<SymbolAddress> cloneSymbolAddress(const SymbolAddress &address) {
        return cloneSymbolAddress(ExprFactoryScope::current().importAddress(address));
    }

    std::unique_ptr<SymbolAddress> makeSymbolAddress(ExprFactory &factory,
                                                     clang::QualType pointeeType,
                                                     SourcePoint fromPoint) {
        return cloneSymbolAddress(Addr::symbol(factory, pointeeType, std::move(fromPoint)).handle());
    }

    std::unique_ptr<SymbolAddress> makeSymbolAddress(clang::QualType pointeeType,
                                                     std::unique_ptr<Address> from,
                                                     SourcePoint fromPoint) {
        auto &factory = ExprFactoryScope::current();
        Addr fromAddr{factory, factory.importAddress(*from)};
        return cloneSymbolAddress(
            Addr::symbol(pointeeType, fromAddr, std::move(fromPoint)).handle());
    }

    std::unique_ptr<VariableAddress> cloneVariableAddress(AddrHandle address) {
        return std::make_unique<VariableAddress>(address.cast<VariableAddress>());
    }

    std::unique_ptr<VariableAddress> makeVariableAddress(
        ExprFactory &factory,
        utils::not_null<const clang::VarDecl *> from) {
        return cloneVariableAddress(factory.variableAddress(from));
    }

    std::unique_ptr<FieldAddress> cloneFieldAddress(AddrHandle address) {
        return std::make_unique<FieldAddress>(address.cast<FieldAddress>());
    }

    std::unique_ptr<FieldAddress> makeFieldAddress(ExprFactory &factory,
                                                   clang::QualType pointeeType,
                                                   const clang::RecordDecl *record,
                                                   std::unique_ptr<Address> base,
                                                   size_t fieldIndex) {
        auto fieldAddr =
            Addr{factory, factory.importAddress(*base)}.field(pointeeType, record, fieldIndex);
        return cloneFieldAddress(fieldAddr.handle());
    }

    std::unique_ptr<Structure> cloneStructure(ExprHandle structure) {
        return std::make_unique<Structure>(structure.cast<Structure>());
    }

    std::unique_ptr<Structure> makeStructure(ExprFactory &factory,
                                             const clang::RecordDecl *record,
                                             const clang::ASTRecordLayout &layout,
                                             std::unique_ptr<Address> from,
                                             SourcePoint fromPoint) {
        auto fromHandle = factory.importAddress(*from);
        return cloneStructure(factory.structure(record, layout, fromHandle, std::move(fromPoint)));
    }

    utils::not_null<std::unique_ptr<UnknownExpr>> UnknownExpr::makeUnknown() {
        auto &factory = ExprFactoryScope::current();
        auto cloned = factory.cloneExpr(factory.unknown()).into_underlying();
        auto unknown = dyn_cast<UnknownExpr>(cloned);
        if (!unknown)
            UNREACHABLE();
        return utils::not_null<std::unique_ptr<UnknownExpr>>{std::move(unknown)};
    }

    ExprHandle simplifiedExprHandle(ExprFactory &factory, const SymbolicExpr &expr) {
        ExprFactoryScope scope(factory);
        return factory.importExpr(*expr.simplifiedExpr());
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> SymbolicExpr::simplifiedExpr() const {
        return importThroughCurrentFactory(*this);
    }

    /**
     * @brief Construct a symbolic structure value with all fields initialized to Unknown.
     * @param ty [in] Structure qualified type.
     * @param baseAddr [in] Base address for the structure.
     * @param fromPoint [in] Source point used to tag created symbols.
     * @return Symbolic expression representing an unknown structure layout.
     */
    utils::not_null<std::unique_ptr<SymbolicExpr>> makeUnknownStructure(
        const clang::QualType &ty,
        utils::not_null<std::unique_ptr<const Address>> baseAddr,
        SourcePoint fromPoint) {
        const auto *RT = ty->getAs<clang::RecordType>();
        if (!RT || !RT->getDecl())
            UNIMPLEMENT("Invalid structure type in makeUnknownStructure.");
        const auto *RD = RT->getDecl()->getDefinition();
        if (!RD || !RD->isCompleteDefinition())
            UNIMPLEMENT("Incomplete struct definition in makeUnknownStructure.");
        const auto &layout = RD->getASTContext().getASTRecordLayout(RD);
        auto &factory = ExprFactoryScope::current();
        return factory.cloneExpr(
            factory.structure(RD, layout, factory.importAddress(*baseAddr),
                              std::move(fromPoint)));
    }

    /**
     * @brief Simplify a linear expression by rebuilding it as a minimal sum of terms.
     * @return Simplified clone when expression is linear; otherwise a plain clone.
     */
    utils::not_null<std::unique_ptr<SymbolicExpr>> SymbolicExpr::simplifiedExprIfLinear() const {
        if (!isLinear())
            return importThroughCurrentFactory(*this);
        auto [hashPtrMap, hashIdMap] = collectUsedSymbols(*this);

        auto &factory   = ExprFactoryScope::current();
        auto linearExpr = toLinearExpr(hashIdMap);
        std::optional<ExprHandle> result{};

        using enum detail::BinaryOpExprNode::Operator;
        for (auto [hash, symbol] : hashPtrMap) {
            auto expr = dyn_cast<const SymbolicExpr>(symbol.get());
            auto C = linearExpr.coefficient(Parma_Polyhedra_Library::Variable{hashIdMap.at(hash)})
                         .get_si();
            if (C == 0)
                continue;

            if (result == std::nullopt) {
                // Seed the accumulator with the first non-zero term.
                if (C == 1)
                    result = factory.importExpr(*expr);
                else
                    result = factory.binary(factory.literal(static_cast<int64_t>(C)),
                                            Multiply, factory.importExpr(*expr));
            } else {
                unsigned absC = std::abs(C);
                ExprHandle varExpr = factory.importExpr(*expr);
                if (absC != 1)
                    varExpr = factory.binary(factory.literal(static_cast<int64_t>(absC)),
                                             Multiply, factory.importExpr(*expr));
                // Combine the current polynomial with the new term using the sign of the
                // coefficient.
                result = factory.binary(result.value(), (C > 0 ? Add : Subtract), varExpr);
            }
        }
        if (auto inhomo = linearExpr.inhomogeneous_term().get_si();
            inhomo || result == std::nullopt) {
            if (result != std::nullopt) {
                // Append the constant term to the linear combination.
                result = factory.binary(
                    result.value(), (inhomo > 0 ? Add : Subtract),
                    factory.literal(static_cast<int64_t>(std::abs(inhomo))));
            } else
                result = factory.literal(static_cast<int64_t>(inhomo));
        }
        if (result == std::nullopt) {
            ERROR("Simplified expr is null! Something goes wrong.");
        }
        return factory.cloneExpr(result.value());
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> detail::LiteralExprNode::clone() const {
        switch (getLiteralType()) {
            case LiteralType::Boolean: return std::make_unique<detail::LiteralExprNode>(data_.boolValue);
            case LiteralType::Int: return std::make_unique<detail::LiteralExprNode>(data_.intValue);
            case LiteralType::UnsignedInt: return std::make_unique<detail::LiteralExprNode>(data_.uintValue);
            case LiteralType::Short: return std::make_unique<detail::LiteralExprNode>(data_.shortValue);
            case LiteralType::UnsignedShort:
                return std::make_unique<detail::LiteralExprNode>(data_.ushortValue);
            case LiteralType::Int64: return std::make_unique<detail::LiteralExprNode>(data_.int64Value);
            case LiteralType::UInt64: return std::make_unique<detail::LiteralExprNode>(data_.uint64Value);
        }

        UNREACHABLE();
    }

    ExprHandle detail::LiteralExprNode::importInto(ExprFactory &factory) const {
        switch (getLiteralType()) {
            case LiteralType::Boolean: return factory.literal(data_.boolValue);
            case LiteralType::Int: return factory.literal(data_.intValue);
            case LiteralType::UnsignedInt: return factory.literal(data_.uintValue);
            case LiteralType::Short: return factory.literal(data_.shortValue);
            case LiteralType::UnsignedShort: return factory.literal(data_.ushortValue);
            case LiteralType::Int64: return factory.literal(data_.int64Value);
            case LiteralType::UInt64: return factory.literal(data_.uint64Value);
        }

        UNREACHABLE();
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> detail::BinaryOpExprNode::clone() const {
        auto leftHandle  = left_.handle();
        auto rightHandle = right_.handle();
        if (leftHandle && rightHandle)
            return std::make_unique<BinaryOpExpr>(*leftHandle, op_, *rightHandle);

        if (ExprFactoryScope::hasCurrent()) {
            auto &factory = ExprFactoryScope::current();
            return factory.cloneExpr(factory.binary(factory.importExpr(*left_), op_,
                                                    factory.importExpr(*right_)));
        }

        return std::make_unique<BinaryOpExpr>(left_->clone(), op_, right_->clone());
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> detail::UnaryOpExprNode::clone() const {
        if (auto handle = expr_.handle())
            return std::make_unique<UnaryOpExpr>(op_, *handle);

        if (ExprFactoryScope::hasCurrent()) {
            auto &factory = ExprFactoryScope::current();
            return factory.cloneExpr(factory.unary(op_, factory.importExpr(*expr_)));
        }

        return std::make_unique<UnaryOpExpr>(op_, expr_->clone());
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> UnknownExpr::clone() const {
        return std::make_unique<UnknownExpr>();
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> SymbolValue::clone() const {
        return std::make_unique<SymbolValue>(*this);
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> SymbolAddress::clone() const {
        return std::make_unique<SymbolAddress>(*this);
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> VariableAddress::clone() const {
        return std::make_unique<VariableAddress>(*this);
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> FieldAddress::clone() const {
        return std::make_unique<FieldAddress>(*this);
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> Structure::clone() const {
        return std::make_unique<Structure>(*this);
    }

    utils::not_null<std::unique_ptr<Address>> SymbolAddress::addressClone() const {
        return std::make_unique<SymbolAddress>(*this);
    }

    utils::not_null<std::unique_ptr<Address>> VariableAddress::addressClone() const {
        return std::make_unique<VariableAddress>(*this);
    }

    utils::not_null<std::unique_ptr<Address>> FieldAddress::addressClone() const {
        return std::make_unique<FieldAddress>(*this);
    }

    int64_t detail::LiteralExprNode::getLiteralValue() const {
        switch (getLiteralType()) {
            case LiteralType::Boolean: return data_.boolValue;
            case LiteralType::Int: return data_.intValue;
            case LiteralType::UnsignedInt: return data_.uintValue;
            case LiteralType::Short: return data_.shortValue;
            case LiteralType::UnsignedShort: return data_.ushortValue;
            case LiteralType::Int64: return data_.int64Value;
            case LiteralType::UInt64: return data_.uint64Value;
        }

        UNREACHABLE();
        return 0;
    }

    size_t detail::LiteralExprNode::hash() const {
        size_t seed = utils::hash_val(getKind(), type_);

        switch (type_) {
            using enum LiteralType;
            case Boolean: return utils::hash_val(seed, data_.boolValue);
            case Int: return utils::hash_val(seed, data_.intValue);
            case UnsignedInt: return utils::hash_val(seed, data_.uintValue);
            case Short: return utils::hash_val(seed, data_.shortValue);
            case UnsignedShort: return utils::hash_val(seed, data_.ushortValue);
            case Int64: return utils::hash_val(seed, data_.int64Value);
            case UInt64: return utils::hash_val(seed, data_.uint64Value);
            default: ERROR("Wrong type.");
        }
    }

    size_t SymbolValue::hash() const {
        size_t seed =
            utils::hash_val(SymbolicExpr::getKind(), fromAddr_->hash(), fromPoint_.hash());
        return seed;
    }

    size_t detail::UnaryOpExprNode::hash() const {
        return utils::hash_val(getKind(), static_cast<size_t>(op_), expr_->hash());
    }

    size_t detail::BinaryOpExprNode::hash() const {
        return utils::hash_val(getKind(), static_cast<size_t>(op_), left_->hash(), right_->hash());
    }

    size_t SymbolAddress::hash() const {
        size_t seed = utils::hash_val(SymbolicExpr::getKind(), fromPoint_.hash(), offset_->hash(),
                                      length_ ? length_.value()->hash() : 0);

        seed = utils::hash_val(seed, fromAddr_ ? fromAddr_.value()->hash() : 0);
        return seed;
    }

    size_t VariableAddress::hash() const { return utils::hash_val(getKind(), from_.get()); }

    size_t FieldAddress::hash() const {
        return utils::hash_val(getKind(), baseAddr_->hash(), fieldIndex_);
    }

    size_t Structure::hash() const {
        auto seed = utils::hash_val(SymbolicExpr::getKind(), info_.definition_.get());
        for (auto &field : fields_)
            seed = utils::hash_val(seed, field->hash());
        return seed;
    }

    size_t UnknownExpr::hash() const { return utils::hash_val(getKind()); }

    std::string detail::LiteralExprNode::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;

        switch (getLiteralType()) {
            case LiteralType::Boolean:
                oss << type("Boolean") << "(" << lit(data_.boolValue ? "true" : "false") << ")";
                break;
            case LiteralType::Int:
                oss << type("Int") << "(" << lit(std::to_string(data_.intValue)) << ")";
                break;
            case LiteralType::UnsignedInt:
                oss << type("UnsignedInt") << "(" << lit(std::to_string(data_.uintValue)) << ")";
                break;
            case LiteralType::Short:
                oss << type("Short") << "(" << lit(std::to_string(data_.shortValue)) << ")";
                break;
            case LiteralType::UnsignedShort:
                oss << type("UnsignedShort") << "(" << lit(std::to_string(data_.ushortValue))
                    << ")";
                break;
            case LiteralType::Int64:
                oss << type("Int64") << "(" << lit(std::to_string(data_.int64Value)) << ")";
                break;
            case LiteralType::UInt64:
                oss << type("UInt64") << "(" << lit(std::to_string(data_.uint64Value)) << ")";
                break;
        }
        return oss.str();
    }

    std::string detail::BinaryOpExprNode::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        std::string opStr;
        switch (op_) {
            case Operator::Multiply: opStr = "*"; break;
            case Operator::Divide: opStr = "/"; break;
            case Operator::Remainder: opStr = "%"; break;
            case Operator::Add: opStr = "+"; break;
            case Operator::Subtract: opStr = "-"; break;
            case Operator::ShiftLeft: opStr = "<<"; break;
            case Operator::ShiftRight: opStr = ">>"; break;
            case Operator::LessThan: opStr = "<"; break;
            case Operator::GreaterThan: opStr = ">"; break;
            case Operator::LessEqual: opStr = "<="; break;
            case Operator::GreaterEqual: opStr = ">="; break;
            case Operator::Equal: opStr = "=="; break;
            case Operator::NotEqual: opStr = "!="; break;
            case Operator::BitAnd: opStr = "&"; break;
            case Operator::BitXor: opStr = "^"; break;
            case Operator::BitOr: opStr = "|"; break;
            case Operator::LogicalAnd: opStr = "&&"; break;
            case Operator::LogicalOr: opStr = "||"; break;
            default: opStr = "?"; break;
        }
        oss << "(" << left_->dump() << " " << op(opStr) << " " << right_->dump() << ")";
        return oss.str();
    }

    std::string detail::UnaryOpExprNode::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        std::string opStr;
        switch (op_) {
            case Operator::Plus: opStr = "+"; break;
            case Operator::Minus: opStr = "-"; break;
            case Operator::LogicalNot: opStr = "!"; break;
            case Operator::BitwiseNot: opStr = "~"; break;
            case Operator::PreInc: opStr = "++"; break;
            case Operator::PreDec: opStr = "--"; break;
            case Operator::PostInc: opStr = "++"; break;
            case Operator::PostDec: opStr = "--"; break;
            case Operator::AddrOf: opStr = "&"; break;
            case Operator::Dereference: opStr = "*"; break;
            default: opStr = "?"; break;
        }
        oss << op(opStr) << "(" << expr_->dump() << ")";
        return oss.str();
    }

    std::string UnknownExpr::dump() const { return utils::dump_fmt::hint("{unknown}"); }

    std::string SymbolValue::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        const auto &t = getValType();

        oss << type("SymbolValue") << "(";
        switch (t.kind) {
            case ScalarKind::Int: oss << "int"; break;
            case ScalarKind::UInt: oss << "uint"; break;
            case ScalarKind::Bool: oss << "bool"; break;
            case ScalarKind::Void: oss << "void"; break;
            case ScalarKind::Structure: ERROR("SymbolValue's ScalarKind should not be Structure");
        }
        oss << lit(std::to_string(t.bitWidth)) << ")";

        oss << " {" << key("from address") << "=";
        oss << key("addr") << ":" << fromAddr_->dump();
        oss << "}, ";

        oss << "{" << key("from point") << "=" << path(fromPoint_.dump()) << "}";
        return oss.str();
    }

    std::string SymbolAddress::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        oss << type("SymbolAddress");

        auto off = getOffset();
        if (length_ == std::nullopt)
            oss << "[" << off->dump() << "]";
        else
            oss << "[" << off->dump() << " " << hint("... +") << length_.value()->dump() << "]";

        oss << " {" << key("from") << "=";
        if (fromAddr_)
            oss << key("addr") << ":" << fromAddr_.value()->dump();
        else
            oss << hint("none");
        oss << "}, "
            << "{" << key("from point") << "=" << path(fromPoint_.dump()) << "}";
        return oss.str();
    }

    std::string VariableAddress::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        oss << type("VariableAddress") << " {" << key("from") << "=";
        if (auto *nd = llvm::dyn_cast<clang::NamedDecl>(from_.get()))
            oss << key("decl") << ":" << nd->getDeclKindName() << " "
                << path(nd->getQualifiedNameAsString());
        else
            oss << key("decl") << ":" << from_->getDeclKindName();
        oss << "}";
        return oss.str();
    }

    std::string FieldAddress::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        oss << type("FieldAddress") << " {" << key("from") << "=";
        oss << key("field of") << ":" << baseAddr_.get()->dump() << "["
            << utils::dump_fmt::lit(std::to_string(fieldIndex_)) << "]";
        oss << "}";
        return oss.str();
    }

    std::string Structure::Info::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        std::string structName = definition_->getNameAsString();
        uint64_t sizeBits      = static_cast<uint64_t>(layout_.getSize().getQuantity()) * 8;

        oss << type("Struct") << "(" << accent(structName) << ", " << key("size") << "="
            << lit(std::to_string(sizeBits)) << " " << hint("bits") << ")";
        return oss.str();
    }

    std::string Structure::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;

        oss << info_.dump();
        oss << ", " << key("fields") << "=[";
        for (size_t i = 0; i < fields_.size(); ++i) {
            oss << fields_[i]->dump();
            if (i + 1 < fields_.size())
                oss << ", ";
        }
        oss << "]";
        return oss.str();
    }

    std::string SourcePoint::dump() const {
        using namespace utils::dump_fmt;
        if (loc_.isInvalid())
            ERROR("Invalid SourcePoint.");

        auto ploc = SM_.getPresumedLoc(loc_);
        if (ploc.isInvalid())
            ERROR("Invalid presumed SourcePoint.");

        std::ostringstream oss;
        oss << path(ploc.getFilename()) << ":" << lit(std::to_string(ploc.getLine())) << ":"
            << lit(std::to_string(ploc.getColumn()));
        return oss.str();
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> detail::LiteralExprNode::doGetACSL(
        const SymbolicExpr::GetACSLConfig &,
        std::unordered_set<SourcePoint> &,
        std::optional<SourcePoint>,
        unsigned,
        bool) const {
        std::ostringstream oss;
        switch (getLiteralType()) {
            case LiteralType::Boolean: oss << (data_.boolValue ? "true" : "false"); break;
            case LiteralType::Int: oss << data_.intValue; break;
            case LiteralType::UnsignedInt: oss << data_.uintValue; break;
            case LiteralType::Short: oss << data_.shortValue; break;
            case LiteralType::UnsignedShort: oss << data_.ushortValue; break;
            case LiteralType::Int64: oss << data_.int64Value; break;
            case LiteralType::UInt64: oss << data_.uint64Value; break;
        }
        return oss.str();
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> detail::BinaryOpExprNode::doGetACSL(
        const SymbolicExpr::GetACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned parentPrec,
        bool isRightChild) const {
        using Op = detail::BinaryOpExprNode::Operator;

        auto emitBoolCmp = [&](const SymbolicExpr &boolExpr,
                               bool expectTrue) -> utils::expected<std::string, GetACSLError> {
            auto boolStr = callGetACSL(boolExpr, config, usedPoints, currentPoint, getPrecedence(op_), false);
            if (!boolStr)
                return boolStr.error();
            if (expectTrue)
                return boolStr.value();
            return std::string("!(") + boolStr.value() + ")";
        };

        if (op_ == Op::Equal || op_ == Op::NotEqual) {
            auto trySimplify = [&](const SymbolicExpr &lhs, const SymbolicExpr &rhs)
                -> std::optional<utils::expected<std::string, GetACSLError>> {
                auto lit = dyn_cast<detail::LiteralExprNode>(&rhs);
                if (!lit)
                    return std::nullopt;
                auto v = lit->getLiteralValue();
                if (v != 0 && v != 1)
                    return std::nullopt;
                if (!isBooleanExpr(lhs))
                    return std::nullopt;
                const bool expectTrue = (op_ == Op::Equal) ? (v == 1) : (v == 0);
                return emitBoolCmp(lhs, expectTrue);
            };

            if (auto r = trySimplify(*left_, *right_))
                return *r;
            if (auto r = trySimplify(*right_, *left_))
                return *r;
        }

        std::ostringstream oss;
        std::string opStr;
        switch (op_) {
#define BIN_OP(name, tok, prec, isRight)                                                           \
    case Operator::name: opStr = tok; break;
#include "operators.def"
            default: UNREACHABLE();
        }

        auto myPrec     = getPrecedence(op_);
        bool needParens = (myPrec < parentPrec) ||
                          (myPrec == parentPrec && isRightChild && !isRightAssociative(op_));

        auto leftStr = callGetACSL(*left_, config, usedPoints, currentPoint, myPrec, false);
        if (!leftStr)
            return leftStr.error();
        auto rightStr = callGetACSL(*right_, config, usedPoints, currentPoint, myPrec, true);
        if (!rightStr)
            return rightStr.error();
        oss << (needParens ? "(" : "") << leftStr.value() << " " << opStr << " " << rightStr.value()
            << (needParens ? ")" : "");
        return oss.str();
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> detail::UnaryOpExprNode::doGetACSL(
        const SymbolicExpr::GetACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned parentPrec,
        bool isRightChild) const {
        std::ostringstream oss;
        std::string opStr;
        switch (op_) {
#define UN_OP(name, tok, prec, isRight)                                                            \
    case Operator::name: opStr = tok; break;
#include "operators.def"
            default: UNREACHABLE();
        }

        auto myPrec     = getPrecedence(op_);
        bool needParens = (myPrec < parentPrec) ||
                          (myPrec == parentPrec && isRightChild && !isRightAssociative(op_));

        if (op_ == Operator::PostInc || op_ == Operator::PostDec) {
            auto subStr = callGetACSL(*expr_, config, usedPoints, currentPoint, myPrec, false);
            if (!subStr)
                return subStr.error();
            oss << (needParens ? "(" : "") << subStr.value() << opStr << (needParens ? ")" : "");
        } else {
            auto subStr = callGetACSL(*expr_, config, usedPoints, currentPoint, myPrec, true);
            if (!subStr)
                return subStr.error();
            oss << (needParens ? "(" : "") << opStr << subStr.value() << (needParens ? ")" : "");
        }
        return oss.str();
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> UnknownExpr::doGetACSL(
        const SymbolicExpr::GetACSLConfig &config,
        std::unordered_set<SourcePoint> &,
        std::optional<SourcePoint>,
        unsigned,
        bool) const {
        if (!config.UnknownExprAsError) {
            WARN("Output UnknownExpr's ACSL, something may go wrong.");
            return std::string{"{Unknown}"};
        }
        return SymbolicExpr::GetACSLError::UnknownExpr;
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> SymbolValue::doGetACSL(
        const SymbolicExpr::GetACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned parentPrec,
        bool isRightChild) const {
        auto [prefix, suffix] =
            details::getPrefixSuffixAndUpdateMap(config, usedPoints, currentPoint, fromPoint_);
        bool hasAt    = !(prefix.empty() || suffix.empty());
        auto valueStr = callGetACSLOfValueProxy(*fromAddr_, config, usedPoints, fromPoint_,
                                                hasAt ? /* enclosed in \\at() */ 0 : parentPrec,
                                                hasAt ? false : isRightChild);
        if (!valueStr)
            return valueStr.error();

        return prefix + std::move(valueStr.value()) + suffix;
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> SymbolAddress::doGetACSL(
        const SymbolicExpr::GetACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned parentPrec,
        bool isRightChild) const {
        if (length_)
            ERROR("Address range has no regularForm but regularFormOfValue.");
        if (fromAddr_ == std::nullopt)
            return SymbolicExpr::GetACSLError::HeapAddress;

        auto offsetStr = callGetACSL(*offset_, config, usedPoints, currentPoint,
                                     getPrecedence(Operator::Add), true);
        if (!offsetStr)
            return offsetStr.error();

        if (offsetStr.value() == "0")
            offsetStr.value().clear();

        auto [prefix, suffix] =
            details::getPrefixSuffixAndUpdateMap(config, usedPoints, currentPoint, fromPoint_);
        bool hasAt     = !(prefix.empty() || suffix.empty());
        bool hasOffset = !offsetStr.value().empty();

        auto nameStrExpected = [&, this]() {
            if (hasAt)
                return callGetACSLOfValue(*fromAddr_.value(), config, usedPoints, fromPoint_,
                                          /* enclosed in \\at() */ 0, false);
            if (hasOffset)
                return callGetACSLOfValue(*fromAddr_.value(), config, usedPoints, fromPoint_,
                                          getPrecedence(Operator::Add), false);
            return callGetACSLOfValue(*fromAddr_.value(), config, usedPoints, fromPoint_,
                                      parentPrec, isRightChild);
        }();
        if (!nameStrExpected)
            return nameStrExpected.error();

        auto nameStr = prefix + std::move(nameStrExpected.value()) + suffix;

        auto offsetedAddrStr = hasOffset ? std::move(nameStr) + " + " + std::move(offsetStr.value())
                                         : std::move(nameStr);

        if (hasOffset) {
            auto myPrec     = getPrecedence(Operator::Add);
            bool needParens = (myPrec < parentPrec) || (myPrec == parentPrec && isRightChild);
            return (needParens ? "(" : "") + std::move(offsetedAddrStr) + (needParens ? ")" : "");
        }

        return offsetedAddrStr;
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> VariableAddress::doGetACSL(
        const SymbolicExpr::GetACSLConfig &,
        std::unordered_set<SourcePoint> &,
        std::optional<SourcePoint>,
        unsigned parentPrec,
        bool isRightChild) const {
        bool needParens = details::isNeedParens(Operator::AddrOf, parentPrec, isRightChild);
        return (needParens ? "(" : "") + std::string{"&"} + from_->getNameAsString() +
               (needParens ? ")" : "");
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> FieldAddress::doGetACSL(
        const SymbolicExpr::GetACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned parentPrec,
        bool isRightChild) const {
        auto valueStr = doGetACSLOfValue(config, usedPoints, currentPoint,
                                         getPrecedence(Operator::AddrOf), true);
        if (!valueStr)
            return valueStr.error();

        bool needParens = details::isNeedParens(Operator::AddrOf, parentPrec, isRightChild);
        return (needParens ? "(" : "") + std::string{"&"} + std::move(valueStr.value()) +
               (needParens ? ")" : "");
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> SymbolAddress::doGetACSLOfValue(
        const SymbolicExpr::GetACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned parentPrec,
        bool isRightChild) const {
        if (fromAddr_ == std::nullopt)
            return SymbolicExpr::GetACSLError::HeapAddress;

        auto [prefix, suffix] =
            details::getPrefixSuffixAndUpdateMap(config, usedPoints, currentPoint, fromPoint_);
        bool hasAt = !(prefix.empty() || suffix.empty());

        auto nameStrExpected =
            callGetACSLOfValue(*fromAddr_.value(), config, usedPoints, fromPoint_,
                               hasAt ? 0 : getPrecedence(Operator::Subscript), false);
        if (!nameStrExpected)
            return nameStrExpected.error();

        auto nameStr = prefix + std::move(nameStrExpected.value()) + suffix;

        if (length_ == std::nullopt) {
            auto offsetStr = callGetACSL(*offset_, config, usedPoints, currentPoint,
                                         /* enclosed in [] */ 0, false);
            if (!offsetStr)
                return offsetStr.error();

            auto useDeref = offsetStr.value() == "0" && config.useDerefWithZeroOffset;
            std::string subedAddrStr;

            if (useDeref) {
                auto recalcNameStrExpected =
                    callGetACSLOfValue(*fromAddr_.value(), config, usedPoints, fromPoint_,
                                       hasAt ? 0 : getPrecedence(Operator::Dereference), true);
                if (!recalcNameStrExpected)
                    return recalcNameStrExpected.error();

                nameStr      = prefix + recalcNameStrExpected.value() + suffix;
                subedAddrStr = "*" + std::move(nameStr);
            } else
                subedAddrStr = std::move(nameStr) + "[" + std::move(offsetStr.value()) + "]";

            bool needParens = details::isNeedParens(
                (useDeref ? Operator::Dereference : Operator::Subscript), parentPrec, isRightChild);
            return (needParens ? "(" : "") + std::move(subedAddrStr) + (needParens ? ")" : "");
        }

        /*---------------- deal with range -----------------*/
        auto rangePrec = getPrecedence(Operator::Range);
        auto offsetStr = callGetACSL(*offset_, config, usedPoints, currentPoint, rangePrec, false);
        if (!offsetStr)
            return offsetStr.error();

        auto &factory = ExprFactoryScope::current();

        // offset + length - 1
        auto offsetPlusLength =
            factory.simplifiedBinary(factory.importExpr(*getOffset()),
                                     detail::BinaryOpExprNode::Operator::Add,
                                     factory.importExpr(*length_.value()));
        auto rightBound =
            factory.simplifiedBinary(offsetPlusLength,
                                     detail::BinaryOpExprNode::Operator::Subtract,
                                     factory.literal(int64_t{1}));
        auto rightBoundStr =
            callGetACSL(*rightBound.get(), config, usedPoints, currentPoint, rangePrec, true);
        if (!rightBoundStr)
            return rightBoundStr.error();

        auto subedAddrStr = std::move(nameStr) + "[" + std::move(offsetStr.value()) + " .. " +
                            std::move(rightBoundStr.value()) + "]";

        bool needParens = details::isNeedParens(Operator::Subscript, parentPrec, isRightChild);
        return (needParens ? "(" : "") + std::move(subedAddrStr) + (needParens ? ")" : "");
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> VariableAddress::doGetACSLOfValue(
        const SymbolicExpr::GetACSLConfig &,
        std::unordered_set<SourcePoint> &,
        std::optional<SourcePoint>,
        unsigned,
        bool) const {
        return from_->getNameAsString();
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> FieldAddress::doGetACSLOfValue(
        const SymbolicExpr::GetACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned parentPrec,
        bool isRightChild) const {
        auto baseStr = callGetACSLOfValue(*baseAddr_, config, usedPoints, currentPoint,
                                          getPrecedence(Operator::MemberAccess), false);
        if (!baseStr)
            return baseStr.error();
        if (baseStr.value().empty())
            ERROR("Empty base std::string");
        auto fields = definition_->fields();
        auto it     = std::ranges::next(fields.begin(), fieldIndex_, fields.end());
        if (it == fields.end())
            ERROR("Out-of-bounds access");
        auto fieldStr = it->getNameAsString();

        std::string concatenatedStr;
        concatenatedStr = baseStr.value() + "." + fieldStr;

        bool needParens = details::isNeedParens(Operator::MemberAccess, parentPrec, isRightChild);
        return (needParens ? "(" : "") + concatenatedStr + (needParens ? ")" : "");
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> Structure::doGetACSL(
        const SymbolicExpr::GetACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned parentPrec,
        bool isRightChild) const {
        auto from = getFrom();
        if (from == std::nullopt)
            return SymbolicExpr::GetACSLError::PartiallyModifiedStruct;

        auto &[fromAddr, fromPoint] = from.value();
        auto [prefix, suffix] =
            details::getPrefixSuffixAndUpdateMap(config, usedPoints, currentPoint, fromPoint);
        bool hasAt    = !(prefix.empty() || suffix.empty());
        auto valueStr = callGetACSLOfValueProxy(*fromAddr, config, usedPoints, fromPoint,
                                                hasAt ? /* enclosed in \\at() */ 0 : parentPrec,
                                                hasAt ? false : isRightChild);
        if (!valueStr)
            return valueStr.error();

        return prefix + std::move(valueStr.value()) + suffix;
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> detail::LiteralExprNode::simplifiedExpr() const {
        return simplifiedExprIfLinear(); // Here, unlike a direct `clone`, after
                                         // `simplifiedExprIfLinear`, all constants will have the
                                         // same type.
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> detail::BinaryOpExprNode::simplifiedExpr() const {
        if (isUnknown())
            return UnknownExpr::makeUnknown().into_underlying();
        if (isLinear())
            return simplifiedExprIfLinear();

        // Try constant folding first.
        if (auto c = evalToConstExpr())
            return cloneConstLiteral(*c);

        auto LHS = left_->simplifiedExpr();
        auto RHS = right_->simplifiedExpr();
        auto &factory = ExprFactoryScope::current();

        using Op = detail::BinaryOpExprNode::Operator;
        // Normalize comparisons against boolean literals to avoid chained equality like `x == 0 == 1`.
        if (op_ == Op::Equal || op_ == Op::NotEqual) {
            auto simplifyBoolCmp = [&](const SymbolicExpr &boolExpr,
                                       const SymbolicExpr &litExpr)
                -> std::unique_ptr<SymbolicExpr> {
                auto lit = dyn_cast<detail::LiteralExprNode>(&litExpr);
                if (!lit)
                    return nullptr;
                auto v = lit->getLiteralValue();
                if (v != 0 && v != 1)
                    return nullptr;
                if (!isBooleanExpr(boolExpr))
                    return nullptr;

                const bool expectTrue = (op_ == Op::Equal) ? (v == 1) : (v == 0);
                if (expectTrue)
                    return importThroughCurrentFactory(boolExpr).into_underlying();
                return factory.cloneExpr(
                                  factory.unary(
                                      detail::UnaryOpExprNode::Operator::LogicalNot,
                                      factory.importExpr(boolExpr)))
                    .into_underlying();
            };

            if (auto simplified = simplifyBoolCmp(*LHS, *RHS))
                return utils::not_null<std::unique_ptr<SymbolicExpr>>(std::move(simplified));
            if (auto simplified = simplifyBoolCmp(*RHS, *LHS))
                return utils::not_null<std::unique_ptr<SymbolicExpr>>(std::move(simplified));
        }

        // Simplify boolean short-circuit cases.
        if (op_ == Op::LogicalAnd) {
            if (auto lc = LHS->evalToConstExpr()) {
                if (!literalAsBool(*lc))
                    return cloneConstLiteral(*lc);
                return RHS; // lhs is true
            }
            if (auto rc = RHS->evalToConstExpr()) {
                if (!literalAsBool(*rc))
                    return cloneConstLiteral(*rc);
                return LHS; // rhs is true
            }
        } else if (op_ == Op::LogicalOr) {
            if (auto lc = LHS->evalToConstExpr()) {
                if (literalAsBool(*lc))
                    return cloneConstLiteral(*lc);
                return RHS; // lhs is false
            }
            if (auto rc = RHS->evalToConstExpr()) {
                if (literalAsBool(*rc))
                    return cloneConstLiteral(*rc);
                return LHS; // rhs is false
            }
        }

        return factory.cloneExpr(
            factory.binary(factory.importExpr(*LHS), op_, factory.importExpr(*RHS)));
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> detail::UnaryOpExprNode::simplifiedExpr() const {
        if (isUnknown())
            return UnknownExpr::makeUnknown().into_underlying();
        if (isLinear())
            return simplifiedExprIfLinear();
        auto subExpr = expr_->simplifiedExpr();
        auto &factory = ExprFactoryScope::current();
        return factory.cloneExpr(factory.unary(op_, factory.importExpr(*subExpr)));
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> SymbolAddress::simplifiedExpr() const {
        if (length_)
            ERROR("Address range is solely for address representation and should not be "
                  "used as an expression.");
        return importThroughCurrentFactory(*this);
    }

    const detail::LiteralExprNode *detail::LiteralExprNode::evalToConstExpr() const {
        auto &factory = ExprFactoryScope::current();
        switch (type_) {
            case LiteralType::Boolean: return literalNode(factory.literal(data_.boolValue));
            case LiteralType::Int: return literalNode(factory.literal(data_.intValue));
            case LiteralType::UnsignedInt: return literalNode(factory.literal(data_.uintValue));
            case LiteralType::Short: return literalNode(factory.literal(data_.shortValue));
            case LiteralType::UnsignedShort:
                return literalNode(factory.literal(data_.ushortValue));
            case LiteralType::Int64: return literalNode(factory.literal(data_.int64Value));
            case LiteralType::UInt64: return literalNode(factory.literal(data_.uint64Value));
        }
        return nullptr;
    }

    const detail::LiteralExprNode *detail::UnaryOpExprNode::evalToConstExpr() const {
        auto C = expr_->evalToConstExpr();
        if (!C)
            return nullptr;

        using Op    = detail::UnaryOpExprNode::Operator;
        auto vt     = expr_->getValType();
        unsigned bw = std::max(vt.bitWidth ? vt.bitWidth : 32u, 32u);

        switch (op_) {
            case Op::LogicalNot: {
                bool r = !literalAsBool(*C);
                return literalNode(ExprFactoryScope::current().literal(r));
            }
            case Op::BitwiseNot: {
                uint64_t x = coerceU(bw, literalRawU(*C));
                return makeLiteralFromUnifiedType({ScalarKind::UInt, bw}, false, (~x) & maskN(bw));
            }
            case Op::Plus: {
                if (vt.kind == ScalarKind::UInt)
                    return makeLiteralFromUnifiedType({ScalarKind::UInt, bw}, false,
                                                      coerceU(bw, literalRawU(*C)));
                else
                    return makeLiteralFromUnifiedType({ScalarKind::Int, bw}, false,
                                                      (uint64_t)coerceS(bw, literalRawU(*C)));
            }
            case Op::Minus: {
                int64_t s = -coerceS(bw, literalRawU(*C));
                return makeLiteralFromUnifiedType({ScalarKind::Int, bw}, false, (uint64_t)s);
            }
            case Op::PreInc:
            case Op::PreDec:
            case Op::PostInc:
            case Op::PostDec:
            case Op::AddrOf:
            case Op::Dereference:
            default: return nullptr;
        }
    }

    const detail::LiteralExprNode *detail::BinaryOpExprNode::evalToConstExpr() const {
        using BO = detail::BinaryOpExprNode::Operator;

        auto Lc = left_->evalToConstExpr();
        if (!Lc)
            return nullptr;

        if (op_ == BO::LogicalAnd) {
            if (!literalAsBool(*Lc))
                return literalNode(ExprFactoryScope::current().literal(false));
            auto Rc = right_->evalToConstExpr();
            if (!Rc)
                return nullptr;
            return literalNode(ExprFactoryScope::current().literal(literalAsBool(*Rc)));
        }
        if (op_ == BO::LogicalOr) {
            if (literalAsBool(*Lc))
                return literalNode(ExprFactoryScope::current().literal(true));
            auto Rc = right_->evalToConstExpr();
            if (!Rc)
                return nullptr;
            return literalNode(ExprFactoryScope::current().literal(literalAsBool(*Rc)));
        }

        auto Rc = right_->evalToConstExpr();
        if (!Rc)
            return nullptr;

        auto tgt = unify(left_->getValType(), right_->getValType());
        if (tgt.kind == ScalarKind::Void) {
            if (op_ == BO::Equal || op_ == BO::NotEqual) {
                auto emitBool = [](bool b) {
                    return literalNode(ExprFactoryScope::current().literal(b));
                };
                bool eq = (literalRawU(*Lc) == literalRawU(*Rc));
                return emitBool(op_ == BO::Equal ? eq : !eq);
            }
            return nullptr;
        }
        unsigned bw = tgt.bitWidth ? tgt.bitWidth : 64;

        auto emitBool = [](bool b) {
            return literalNode(ExprFactoryScope::current().literal(b));
        };

        if (tgt.kind == ScalarKind::UInt) {
            uint64_t L = coerceU(bw, literalRawU(*Lc));
            uint64_t R = coerceU(bw, literalRawU(*Rc));

            switch (op_) {
                case BO::Equal: return emitBool(L == R);
                case BO::NotEqual: return emitBool(L != R);
                case BO::LessThan: return emitBool(L < R);
                case BO::LessEqual: return emitBool(L <= R);
                case BO::GreaterThan: return emitBool(L > R);
                case BO::GreaterEqual: return emitBool(L >= R);

                case BO::Add: return makeLiteralFromUnifiedType(tgt, false, (L + R) & maskN(bw));
                case BO::Subtract:
                    return makeLiteralFromUnifiedType(tgt, false, (L - R) & maskN(bw));
                case BO::Multiply:
                    return makeLiteralFromUnifiedType(tgt, false, (L * R) & maskN(bw));
                case BO::Divide:
                    if (R == 0)
                        return nullptr;
                    return makeLiteralFromUnifiedType(tgt, false, L / R);
                case BO::Remainder:
                    if (R == 0)
                        return nullptr;
                    return makeLiteralFromUnifiedType(tgt, false, L % R);

                case BO::ShiftLeft:
                    if (R >= 64)
                        return nullptr;
                    return makeLiteralFromUnifiedType(tgt, false, (L << (unsigned)R) & maskN(bw));
                case BO::ShiftRight:
                    if (R >= 64)
                        return nullptr;
                    return makeLiteralFromUnifiedType(tgt, false, (L >> (unsigned)R));

                case BO::BitAnd: return makeLiteralFromUnifiedType(tgt, false, L & R);
                case BO::BitOr: return makeLiteralFromUnifiedType(tgt, false, L | R);
                case BO::BitXor: return makeLiteralFromUnifiedType(tgt, false, L ^ R);

                default: return nullptr;
            }
        } else {
            int64_t L = coerceS(bw, literalRawU(*Lc));
            int64_t R = coerceS(bw, literalRawU(*Rc));

            switch (op_) {
                case BO::Equal: return emitBool(L == R);
                case BO::NotEqual: return emitBool(L != R);
                case BO::LessThan: return emitBool(L < R);
                case BO::LessEqual: return emitBool(L <= R);
                case BO::GreaterThan: return emitBool(L > R);
                case BO::GreaterEqual: return emitBool(L >= R);

                case BO::Add: return makeLiteralFromUnifiedType(tgt, false, (uint64_t)(L + R));
                case BO::Subtract: return makeLiteralFromUnifiedType(tgt, false, (uint64_t)(L - R));
                case BO::Multiply: return makeLiteralFromUnifiedType(tgt, false, (uint64_t)(L * R));
                case BO::Divide:
                    if (R == 0)
                        return nullptr;
                    return makeLiteralFromUnifiedType(tgt, false, (uint64_t)(L / R));
                case BO::Remainder:
                    if (R == 0)
                        return nullptr;
                    return makeLiteralFromUnifiedType(tgt, false, (uint64_t)(L % R));

                case BO::ShiftLeft:
                    if ((uint64_t)R >= 64)
                        return nullptr;
                    return makeLiteralFromUnifiedType(tgt, false, (uint64_t(L) << (unsigned)R));
                case BO::ShiftRight:
                    if ((uint64_t)R >= 64)
                        return nullptr;
                    return makeLiteralFromUnifiedType(tgt, false, (uint64_t)(L >> (unsigned)R));

                case BO::BitAnd:
                    return makeLiteralFromUnifiedType(tgt, false, uint64_t(L) & uint64_t(R));
                case BO::BitOr:
                    return makeLiteralFromUnifiedType(tgt, false, uint64_t(L) | uint64_t(R));
                case BO::BitXor:
                    return makeLiteralFromUnifiedType(tgt, false, uint64_t(L) ^ uint64_t(R));

                default: return nullptr;
            }
        }
    }

    bool detail::LiteralExprNode::equal(const SymbolicExpr &expr) const {
        const auto liter = dyn_cast<const detail::LiteralExprNode>(&expr);
        if (!liter)
            return false;
        if (getValType() != expr.getValType())
            return false;

        return type_ == liter->type_ && getLiteralValue() == liter->getLiteralValue();
    }

    bool detail::BinaryOpExprNode::equal(const SymbolicExpr &expr) const {
        const auto binary = dyn_cast<const BinaryOpExpr>(&expr);
        if (!binary)
            return false;
        if (getValType() != expr.getValType())
            return false;

        return *left_ == *(binary->left_) && op_ == binary->op_ && *right_ == *(binary->right_);
    }

    bool detail::UnaryOpExprNode::equal(const SymbolicExpr &expr) const {
        const auto unary = dyn_cast<const UnaryOpExpr>(&expr);
        if (!unary)
            return false;
        if (getValType() != expr.getValType())
            return false;

        return op_ == unary->op_ && *expr_ == *(unary->expr_);
    }

    bool UnknownExpr::equal(const SymbolicExpr &expr) const {
        return expr.isUnknown() && getValType() == expr.getValType();
    }

    bool SymbolValue::equal(const SymbolicExpr &expr) const {
        const auto symbolValue = dyn_cast<const SymbolValue>(&expr);
        if (!symbolValue)
            return false;
        if (getValType() != expr.getValType())
            return false;

        if (fromPoint_ != symbolValue->fromPoint_)
            return false;

        return *fromAddr_ == *symbolValue->fromAddr_;
    }

    bool SymbolAddress::equal(const SymbolicExpr &expr) const {
        auto other = dyn_cast<const SymbolAddress>(&expr);
        if (!other)
            return false;
        if (getValType() != expr.getValType())
            return false;

        // compare from
        if (fromAddr_ && other->fromAddr_ && *fromAddr_.value() != *other->fromAddr_.value()) {
            return false;
        }

        if ((other->fromAddr_ == std::nullopt) ^ (fromAddr_ == std::nullopt))
            return false;

        if (fromPoint_ != other->fromPoint_)
            return false;

        // compare offset
        // todo: Need a `offsetEqual`, here is not correct now.
        if (*offset_->simplifiedExpr() != *other->getOffset()->simplifiedExpr()) {
            return false;
        }

        // compare range
        if (length_ != std::nullopt && other->length_ != std::nullopt) {
            if (*length_.value() != *(other->length_.value()))
                return false;
        } else if ((length_ == std::nullopt) ^ (other->length_ == std::nullopt)) {
            return false;
        }
        return true;
    }

    bool VariableAddress::equal(const SymbolicExpr &expr) const {
        auto other = dyn_cast<const VariableAddress>(&expr);
        if (!other)
            return false;
        if (getValType() != expr.getValType())
            return false;

        return from_ == other->from_;
    }

    bool FieldAddress::equal(const SymbolicExpr &expr) const {
        auto other = dyn_cast<const FieldAddress>(&expr);
        if (!other)
            return false;
        if (getValType() != expr.getValType())
            return false;

        return *baseAddr_ == *other->baseAddr_ && fieldIndex_ == other->fieldIndex_;
    }

    std::optional<utils::not_null<const clang::VarDecl *>> SymbolAddress::getFromRoot() const {
        if (fromAddr_ == std::nullopt)
            return std::nullopt;
        return fromAddr_.value()->getFromRoot();
    }

    std::optional<utils::not_null<const clang::VarDecl *>> SymbolAddrBaseInfo::getFromRoot() const {
        if (fromAddr_ == std::nullopt)
            return std::nullopt;
        return fromAddr_.value()->getFromRoot();
    }

    std::optional<utils::not_null<const clang::VarDecl *>> VariableAddress::getFromRoot() const {
        return from_;
    }

    std::optional<utils::not_null<const clang::VarDecl *>> FieldAddress::getFromRoot() const {
        return baseAddr_->getFromRoot();
    }

    std::optional<utils::not_null<const clang::VarDecl *>> SymbolValue::getFromRoot() const {
        return fromAddr_->getFromRoot();
    }

    bool Structure::Info::equal(const Structure::Info &other) const {
        if (definition_ != other.definition_)
            return false;
        return true;
    }

    bool Structure::Info::operator==(const Info &other) const { return equal(other); }

    bool Structure::equal(const SymbolicExpr &expr) const {
        const auto st = dyn_cast<const Structure>(&expr);
        if (!st)
            return false;
        if (getValType() != expr.getValType())
            return false;
        if (!info_.equal(st->info_))
            return false;
        return std::ranges::equal(fields_, st->fields_,
                                  [](auto &lhs, auto &rhs) { return *lhs == *rhs; });
    }

    std::optional<utils::not_null<std::unique_ptr<SymbolAddress>>> detail::BinaryOpExprNode::
        doTryEvalAsSymbolAddr() const {
        auto lhs = callTryEvalAsAddr(*left_), rhs = callTryEvalAsAddr(*right_);
        if (lhs && rhs)
            return std::nullopt;
        if (lhs == std::nullopt && rhs == std::nullopt)
            return std::nullopt;

        auto &factory = ExprFactoryScope::current();
        AddrHandle addr = lhs ? factory.importAddress(*lhs.value())
                              : factory.importAddress(*rhs.value());
        if (lhs) {
            if (!isValidOffsetOrLength(*right_))
                return std::nullopt;
            auto expr = factory.importExpr(*right_);
            switch (op_) {
                using enum Operator;
                case Add: addr = factory.withAddedOffset(addr, expr); break;
                case Subtract: addr = factory.withSubtractedOffset(addr, expr); break;

                default: return std::nullopt;
            }
        } else {
            if (!isValidOffsetOrLength(*left_))
                return std::nullopt;
            auto expr = factory.importExpr(*left_);
            switch (op_) {
                using enum Operator;
                case Add: addr = factory.withAddedOffset(addr, expr); break;
                case Subtract: return std::nullopt;
                default: return std::nullopt;
            }
        }
        return cloneSymbolAddress(addr);
    }

    std::optional<utils::not_null<std::unique_ptr<SymbolAddress>>> SymbolAddress::
        doTryEvalAsSymbolAddr() const {
        return cloneSymbolAddress(*this);
    }

    SymbolicExpr::UsedMap SymbolValue::collectUsedSymbols() const { return {{hash(), this}}; }

    SymbolicExpr::UsedMap detail::BinaryOpExprNode::collectUsedSymbols() const {
        auto lmap = left_->collectUsedSymbols();
        auto rmap = right_->collectUsedSymbols();
        lmap.insert(make_move_iterator(rmap.begin()), make_move_iterator(rmap.end()));
        return lmap;
    }

    SymbolicExpr::UsedMap detail::UnaryOpExprNode::collectUsedSymbols() const {
        return expr_->collectUsedSymbols();
    }

    SymbolicExpr::UsedMap SymbolAddress::collectUsedSymbols() const {
        if (length_)
            ERROR("Address range is solely for address representation and should not be "
                  "used as an expression.");
        return {{hash(), this}};
    }

    SymbolAddress::SymbolAddress(const SymbolAddress &other)
        : Address(other), Symbol(other), offset_(other.offset_.clone()),
          fromPoint_(other.fromPoint_), length_(std::nullopt) {
        if (other.length_)
            length_.emplace(other.length_.value().clone());
        if (other.fromAddr_ == std::nullopt)
            fromAddr_ = std::nullopt;
        else
            fromAddr_ = other.fromAddr_.value()->addressClone().into_underlying();
    }

    SymbolAddrBaseInfo::SymbolAddrBaseInfo(const SymbolAddrBaseInfo &other)
        : fromPoint_(other.fromPoint_) {
        if (other.fromAddr_ == std::nullopt)
            fromAddr_ = std::nullopt;
        else
            fromAddr_ = other.fromAddr_.value()->addressClone().into_underlying();
    }

    SymbolAddress::SymbolAddress(
        const clang::QualType pointeeType,
        std::optional<utils::not_null<std::unique_ptr<const Address>>> from,
        SourcePoint fromPoint,
        std::optional<utils::not_null<std::unique_ptr<const SymbolicExpr>>> offset,
        std::optional<utils::not_null<std::unique_ptr<const SymbolicExpr>>> length)
        : Address(SymbolicExpr::ExprKind::K_SymbolAddress,
                  SymbolicExpr::Type{SymbolicExpr::ScalarKind::UInt, 64},
                  pointeeType),
          Symbol(Kind::K_SymbolAddress), offset_(makeSymbolAddressOffsetChild(std::move(offset))),
          fromAddr_(std::move(from)), fromPoint_(fromPoint), length_(std::nullopt) {
        if (length != std::nullopt)
            length_.emplace(ExprChild::fromConstOwned(std::move(length.value())));
    }

    SymbolAddress::SymbolAddress(
        const clang::QualType pointeeType,
        std::optional<utils::not_null<std::unique_ptr<const Address>>> from,
        SourcePoint fromPoint,
        std::optional<ExprHandle> offset,
        std::optional<ExprHandle> length)
        : Address(SymbolicExpr::ExprKind::K_SymbolAddress,
                  SymbolicExpr::Type{SymbolicExpr::ScalarKind::UInt, 64},
                  pointeeType),
          Symbol(Kind::K_SymbolAddress), offset_(makeSymbolAddressOffsetChild(offset)),
          fromAddr_(std::move(from)), fromPoint_(fromPoint), length_(std::nullopt) {
        if (length != std::nullopt)
            length_.emplace(length.value());
    }

    utils::not_null<std::unique_ptr<SymbolAddress>> SymbolAddress::withOffset(
        utils::not_null<std::unique_ptr<SymbolicExpr>> offset) const {
        if (!isValidOffsetOrLength(*offset))
            ERROR("Invalid offset.");
        auto &factory = ExprFactoryScope::current();
        Addr addr{factory, factory.importAddress(*this)};
        Expr offsetExpr{factory, factory.importExpr(*offset)};
        return cloneSymbolAddress(addr.withOffset(offsetExpr).handle());
    }

    utils::not_null<std::unique_ptr<SymbolAddress>> SymbolAddress::withAddedOffset(
        utils::not_null<std::unique_ptr<SymbolicExpr>> extra) const {
        if (!isValidOffsetOrLength(*extra))
            ERROR("Invalid offset.");
        auto &factory = ExprFactoryScope::current();
        Addr addr{factory, factory.importAddress(*this)};
        Expr extraExpr{factory, factory.importExpr(*extra)};
        return cloneSymbolAddress(addr.withAddedOffset(extraExpr).handle());
    }

    utils::not_null<std::unique_ptr<SymbolAddress>> SymbolAddress::withSubtractedOffset(
        utils::not_null<std::unique_ptr<SymbolicExpr>> extra) const {
        if (!isValidOffsetOrLength(*extra))
            ERROR("Invalid offset.");
        auto &factory = ExprFactoryScope::current();
        Addr addr{factory, factory.importAddress(*this)};
        Expr extraExpr{factory, factory.importExpr(*extra)};
        return cloneSymbolAddress(addr.withSubtractedOffset(extraExpr).handle());
    }

    utils::not_null<std::unique_ptr<SymbolAddress>> SymbolAddress::withResetOffset() const {
        auto &factory = ExprFactoryScope::current();
        Addr addr{factory, factory.importAddress(*this)};
        LiteralExpr zero{factory, static_cast<int64_t>(ZERO_OFFSET)};
        return cloneSymbolAddress(addr.withOffset(zero).handle());
    }

    utils::not_null<std::unique_ptr<SymbolAddress>> SymbolAddress::withLength(
        utils::not_null<std::unique_ptr<SymbolicExpr>> len) const {
        if (!isValidOffsetOrLength(*len))
            ERROR("Invalid Length.");
        auto &factory = ExprFactoryScope::current();
        Addr addr{factory, factory.importAddress(*this)};
        Expr length{factory, factory.importExpr(*len)};
        return cloneSymbolAddress(addr.withLength(length).handle());
    }

    utils::not_null<std::unique_ptr<SymbolAddress>> SymbolAddress::withAddedLength(
        utils::not_null<std::unique_ptr<SymbolicExpr>> extra) const {
        if (!isValidOffsetOrLength(*extra))
            ERROR("Invalid offset.");
        auto &factory = ExprFactoryScope::current();
        Addr addr{factory, factory.importAddress(*this)};
        Expr extraExpr{factory, factory.importExpr(*extra)};
        return cloneSymbolAddress(addr.withAddedLength(extraExpr).handle());
    }

    utils::not_null<std::unique_ptr<SymbolAddress>> SymbolAddress::withoutLength() const {
        auto &factory = ExprFactoryScope::current();
        Addr addr{factory, factory.importAddress(*this)};
        return cloneSymbolAddress(addr.withoutLength().handle());
    }

    size_t SymbolAddrBaseInfo::hash() const {
        return utils::hash_val(fromPoint_.hash(), fromAddr_ ? fromAddr_.value()->hash() : 0);
    }

    std::optional<utils::not_null<std::unique_ptr<SymbolicExpr>>> SymbolAddress::getRightBound()
        const {
        // Not sure return which one is better, offset_+1 or nullopt.
        if (length_ == std::nullopt)
            return std::nullopt;
        auto &factory = ExprFactoryScope::current();
        auto rightBound = factory.binary(factory.importExpr(*offset_),
                                         detail::BinaryOpExprNode::Operator::Add,
                                         factory.importExpr(*length_.value()));
        return factory.cloneExpr(rightBound);
    }

    SymbolAddrBaseInfo SymbolAddress::getBaseInfo() const {
        if (fromAddr_ == std::nullopt)
            return SymbolAddrBaseInfo{std::nullopt, fromPoint_, pointeeType_};
        return SymbolAddrBaseInfo{fromAddr_.value()->addressClone().into_underlying(), fromPoint_,
                                  pointeeType_};
    }

    int SymbolAddress::getDimension() const {
        if (fromAddr_ == std::nullopt)
            return -1;
        if (auto dim = fromAddr_.value()->getDimension(); dim >= 0)
            return dim + 1;
        return -1;
    }

    int VariableAddress::getDimension() const { return 0; }

    int FieldAddress::getDimension() const { return baseAddr_->getDimension(); }

    VariableAddress::VariableAddress(const VariableAddress &other)
        : Address(other), from_(other.from_) {}

    VariableAddress &VariableAddress::operator=(const VariableAddress &other) {
        if (this == &other)
            return *this;
        Address::operator=(other);
        from_ = other.from_;
        return *this;
    }

    FieldAddress::FieldAddress(const FieldAddress &other)
        : Address(other), definition_(other.definition_),
          baseAddr_(other.baseAddr_->addressClone().into_underlying()),
          fieldIndex_(other.fieldIndex_) {}

    FieldAddress &FieldAddress::operator=(const FieldAddress &other) {
        if (this == &other)
            return *this;
        Address::operator=(other);
        definition_ = other.definition_;
        baseAddr_   = other.baseAddr_->addressClone().into_underlying();
        fieldIndex_ = other.fieldIndex_;
        return *this;
    }

    utils::not_null<std::unique_ptr<Structure>> Structure::withFieldValue(
        size_t index,
        utils::not_null<std::unique_ptr<SymbolicExpr>> expr) const {
        if (index >= fields_.size())
            ERROR("Out-of-bounds access");
        auto &factory = ExprFactoryScope::current();
        return cloneStructure(factory.withField(
            factory.importExpr(*this), index, factory.importExpr(*expr)));
    }

    Structure::Structure(Info info, std::vector<ExprHandle> fields)
        : SymbolicExpr(
              ExprKind::K_Structure,
              Type{ScalarKind::Structure, static_cast<unsigned>(info.layout_.getSize().getQuantity()) *
                                              8 /*By default, char is 8-bit.*/}),
          Symbol(Kind::K_Structure), info_(info) {
        if (fields.size() != info_.layout_.getFieldCount())
            ERROR("Structure field count mismatch");
        fields_.reserve(fields.size());
        for (auto field : fields)
            fields_.emplace_back(field);
    }

    Structure::Structure(const clang::RecordDecl *RD,
                         const clang::ASTRecordLayout &layout,
                         utils::not_null<std::unique_ptr<const Address>> from,
                         SourcePoint fromPoint)
        : SymbolicExpr(
              ExprKind::K_Structure,
              Type{ScalarKind::Structure, static_cast<unsigned>(layout.getSize().getQuantity()) *
                                              8 /*By default, char is 8-bit.*/}),
          Symbol(Kind::K_Structure), info_(Info{RD, layout}) {
        auto &factory = ExprFactoryScope::current();
        Addr fromAddr{factory, factory.importAddress(*from)};

        fields_.reserve(info_.layout_.getFieldCount());
        for (auto field : info_.definition_->fields()) {
            auto index          = field->getFieldIndex();
            clang::QualType fty = field->getType();
            auto fieldAddr = fromAddr.field(fty, info_.definition_, index);

            if (fty->isStructureType()) {
                auto nestedRD = fty->getAsRecordDecl();
                if (!nestedRD || !nestedRD->isCompleteDefinition())
                    ERROR("Incomplete nested struct definition");
                nestedRD           = nestedRD->getDefinition();
                auto &nestedLayout = nestedRD->getASTContext().getASTRecordLayout(nestedRD);
                fields_.emplace_back(
                    factory.structure(nestedRD, nestedLayout, fieldAddr.handle(), fromPoint));
            } else if (fty->isPointerType()) {
                fields_.emplace_back(Addr::symbol(fty, fieldAddr, fromPoint).asExpr().handle());
            } else if (fty->isArrayType()) {
                auto arrayType = llvm::cast<clang::ArrayType>(fty);
                auto elemTy    = arrayType->getElementType();
                auto *cat      = llvm::dyn_cast<clang::ConstantArrayType>(fty.getTypePtr());
                auto arrayAddr = Addr::symbol(elemTy, fieldAddr, fromPoint);
                if (cat) {
                    LiteralExpr length{factory, cat->getSize().getZExtValue()};
                    arrayAddr = arrayAddr.withLength(length);
                }
                fields_.emplace_back(arrayAddr.asExpr().handle());
            } else {
                fields_.emplace_back(
                    Expr::symbolValue(deriveType(fty), fieldAddr, fromPoint).handle());
            }
        }
        if (fields_.size() != info_.layout_.getFieldCount())
            UNREACHABLE();
    }

    SymbolValue::SymbolValue(const SymbolValue &other)
        : SymbolicExpr(other), Symbol(Kind::K_SymbolValue),
          fromAddr_(other.fromAddr_->addressClone().into_underlying()),
          fromPoint_(other.fromPoint_) {}

    std::ostream &operator<<(std::ostream &os, SymbolicExpr::ExprKind t) {
        switch (t) {
            using enum SymbolicExpr::ExprKind;
            case K_LiteralExpr: os << "Literal"; break;
            case K_SymbolValue: os << "SymbolValue"; break;
            case K_SymbolAddress: os << "SymbolAddr"; break;
            case K_VariableAddress: os << "VariableAddr"; break;
            case K_FieldAddress: os << "FieldAddr"; break;
            case K_BinaryOpExpr: os << "BinaryOp"; break;
            case K_UnaryOpExpr: os << "UnaryOp"; break;
            case K_Structure: os << "Structure"; break;
            case K_UnknownExpr: os << "Unknown"; break;
            default: UNREACHABLE();
        }
        return os;
    }

    Structure::From Structure::getFrom() const {
        // This structure has a fixed 'from' only if every member is from the same `FieldAddress`
        // **and** same `SourcePoint`.
        std::optional<utils::not_null<std::unique_ptr<const Address>>> commonBaseAddr{};
        std::optional<SourcePoint> commonFromPoint{};
        for (size_t index = 0; index < fields_.size(); ++index) {
            auto &field = fields_.at(index);
            auto symbol = dyn_cast<const Symbol>(field.get().get());
            if (symbol == nullptr)
                return std::nullopt;
            auto fromAddr = symbol->getFromAddr();
            if (fromAddr == std::nullopt)
                return std::nullopt;
            auto fieldAddr = dyn_cast<const FieldAddress>(fromAddr.value().get().get());
            if (fieldAddr == nullptr)
                return std::nullopt;

            auto &base    = fieldAddr->getBaseAddr();
            auto &fieldId = fieldAddr->getFieldIndex();
            if (fieldId != index)
                return std::nullopt;

            if (commonBaseAddr == std::nullopt)
                commonBaseAddr = base->addressClone().into_underlying();

            auto fromPoint = symbol->getFromPoint();
            if (fromPoint == std::nullopt)
                return std::nullopt;
            if (commonFromPoint == std::nullopt)
                commonFromPoint.emplace(fromPoint.value());

            if (*commonBaseAddr.value() != *base || commonFromPoint.value() != fromPoint.value())
                return std::nullopt;
        }
        if (commonBaseAddr == std::nullopt || commonFromPoint == std::nullopt)
            UNREACHABLE();

        return std::pair{std::move(commonBaseAddr).value(), std::move(commonFromPoint).value()};
    }

    std::optional<utils::not_null<std::unique_ptr<const Address>>> Structure::getFromAddr() const {
        if (auto from = getFrom())
            return std::move(from.value().first);
        return std::nullopt;
    }

    std::optional<SourcePoint> Structure::getFromPoint() const {
        if (auto from = getFrom())
            return std::move(from.value().second);
        return std::nullopt;
    }

    SourcePoint &SourcePoint::operator=(const SourcePoint &other) {
        if (this == &other)
            return *this;
        if (&SM_ != &other.SM_)
            ERROR("SourcePoint from different clang::SourceManager.");
        loc_ = other.loc_;
        return *this;
    }

    SourcePoint &SourcePoint::operator=(SourcePoint &&other) {
        if (this == &other)
            return *this;
        if (&SM_ != &other.SM_)
            ERROR("SourcePoint from different clang::SourceManager.");
        loc_ = std::move(other.loc_);
        return *this;
    }

    SourcePoint SourcePoint::fromFuncDecl(const clang::FunctionDecl *FD,
                                          const clang::SourceManager &SM,
                                          const clang::LangOptions &LO) {
        if (FD == nullptr)
            ERROR("FunctionDecl is null.");
        if (!FD->hasBody())
            ERROR("FunctionDecl: " << FD->getNameAsString() << " has no body.");

        auto labelPrefix = "BeginOf_" + FD->getNameAsString();
        SourcePoint p{SM, labelPrefix};

        const clang::Stmt *body = FD->getBody();
        assert(body != nullptr);

        clang::SourceLocation BL;

        if (const auto *CS = llvm::dyn_cast<clang::CompoundStmt>(body)) {
            BL = clang::Lexer::getLocForEndOfToken(CS->getLBracLoc(), 0, SM, LO);
        } else {
            BL = body->getBeginLoc();
        }
        if (BL.isInvalid()) {
            ERROR("Location is invalid: {" +
                  clang::Lexer::getSourceText(
                      clang::CharSourceRange::getTokenRange(FD->getSourceRange()), SM, LO)
                      .str() +
                  "}");
        }

        p.loc_ = SM.getExpansionLoc(BL);
        return p;
    }

    SourcePoint SourcePoint::fromStmtBefore(const clang::Stmt *S,
                                            const clang::SourceManager &SM,
                                            const clang::LangOptions &LO) {
        if (S == nullptr)
            ERROR("S is nullptr.");
        auto labelPrefix = std::string{"Before_"} + S->getStmtClassName();
        SourcePoint p{SM, std::move(labelPrefix)};
        auto BL = S->getBeginLoc();
        if (BL.isInvalid())
            ERROR("Location before clang::Stmt: {" +
                  clang::Lexer::getSourceText(
                      clang::CharSourceRange::getTokenRange(S->getSourceRange()), SM, LO)
                      .str() +
                  "} is invalid.");
        p.loc_ = SM.getExpansionLoc(BL);
        return p;
    }

    SourcePoint SourcePoint::fromStmtAfter(const clang::Stmt *S,
                                           const clang::SourceManager &SM,
                                           const clang::LangOptions &LO) {
        if (S == nullptr)
            ERROR("S is nullptr.");
        auto labelPrefix = std::string{"After_"} + S->getStmtClassName();
        SourcePoint p{SM, std::move(labelPrefix)};
        auto EL = S->getEndLoc();
        if (EL.isInvalid())
            ERROR("EndLoc is invalid for the given Stmt.");

        // Work on spelling loc to avoid macro-ID pitfalls.
        clang::SourceLocation ELSpelling = SM.getSpellingLoc(EL);
        if (ELSpelling.isInvalid())
            ERROR("Spelling EndLoc is invalid for the given Stmt.");

        // Prefer Lexer::getLocForEndOfToken on the spelling loc; fallback to MeasureTokenLength.
        clang::SourceLocation ALSpelling =
            clang::Lexer::getLocForEndOfToken(ELSpelling, /*Offset=*/0, SM, LO);

        if (ALSpelling.isInvalid()) {
            unsigned tokLen = clang::Lexer::MeasureTokenLength(ELSpelling, SM, LO);
            if (tokLen > 0) {
                ALSpelling = ELSpelling.getLocWithOffset(static_cast<int>(tokLen));
            } else {
                // If token length is 0 (rare but possible), treat "after" as the token end itself.
                ALSpelling = ELSpelling;
            }
        }

        if (ALSpelling.isInvalid()) {
            auto text = clang::Lexer::getSourceText(
                clang::CharSourceRange::getTokenRange(S->getSourceRange()), SM, LO);
            ERROR("Location after clang::Stmt: {" + text.str() + "} is invalid (macro/spelling).");
        }

        // Map back to expansion loc so downstream logic stays in expansion coordinates.
        p.loc_ = SM.getExpansionLoc(ALSpelling);
        return p;
    }

    bool SourcePoint::operator<(const SourcePoint &other) const {
        if (&SM_ != &other.SM_)
            ERROR("SourcePoint from different clang::SourceManager.");
        if (loc_.isInvalid() || other.loc_.isInvalid())
            UNREACHABLE();
        return SM_.isBeforeInTranslationUnit(loc_, other.loc_);
    }

    bool SourcePoint::operator==(const SourcePoint &other) const {
        if (&SM_ != &other.SM_) {
            // It's an error now.
            ERROR("SourcePoint from different clang::SourceManager.");
            // WARN("SourcePoint from different clang::SourceManager.");
            // return false;
        }
        if (loc_.isInvalid() || other.loc_.isInvalid())
            UNREACHABLE();
        return !SM_.isBeforeInTranslationUnit(loc_, other.loc_) &&
               !SM_.isBeforeInTranslationUnit(other.loc_, loc_);
    }

    std::unique_ptr<SymbolicExpr> createLNotExpr(
        utils::not_null<std::unique_ptr<SymbolicExpr>> expr) {
        auto &factory = ExprFactoryScope::current();
        return factory.cloneExpr(
                          factory.unary(detail::UnaryOpExprNode::Operator::LogicalNot,
                                        factory.importExpr(*expr)))
            .into_underlying();
    }

    detail::BinaryOpExprNode::Operator getCompoundAssignOp(clang::BinaryOperatorKind compoundAssignOp) {
        switch (compoundAssignOp) {
            using enum clang::BinaryOperatorKind;
            using enum detail::BinaryOpExprNode::Operator;
            case BO_MulAssign: return Multiply;
            case BO_DivAssign: return Divide;
            case BO_RemAssign: return Remainder;
            case BO_AddAssign: return Add;
            case BO_SubAssign: return Subtract;
            case BO_ShlAssign: return ShiftLeft;
            case BO_ShrAssign: return ShiftRight;
            case BO_AndAssign: return BitAnd;
            case BO_XorAssign: return BitXor;
            case BO_OrAssign: return BitOr;
            default: UNREACHABLE();
        }
    }

    // No AssignOp Here.
    detail::BinaryOpExprNode::Operator getBinaryOp(clang::BinaryOperatorKind op) {
        switch (op) {
            using enum clang::BinaryOperatorKind;
            case BO_Mul: return detail::BinaryOpExprNode::Operator::Multiply;
            case BO_Div: return detail::BinaryOpExprNode::Operator::Divide;
            case BO_Rem: return detail::BinaryOpExprNode::Operator::Remainder;
            case BO_Add: return detail::BinaryOpExprNode::Operator::Add;
            case BO_Sub: return detail::BinaryOpExprNode::Operator::Subtract;
            case BO_Shl: return detail::BinaryOpExprNode::Operator::ShiftLeft;
            case BO_Shr: return detail::BinaryOpExprNode::Operator::ShiftRight;
            case BO_LT: return detail::BinaryOpExprNode::Operator::LessThan;
            case BO_GT: return detail::BinaryOpExprNode::Operator::GreaterThan;
            case BO_LE: return detail::BinaryOpExprNode::Operator::LessEqual;
            case BO_GE: return detail::BinaryOpExprNode::Operator::GreaterEqual;
            case BO_EQ: return detail::BinaryOpExprNode::Operator::Equal;
            case BO_NE: return detail::BinaryOpExprNode::Operator::NotEqual;
            case BO_And: return detail::BinaryOpExprNode::Operator::BitAnd;
            case BO_Xor: return detail::BinaryOpExprNode::Operator::BitXor;
            case BO_Or: return detail::BinaryOpExprNode::Operator::BitOr;
            case BO_LAnd: return detail::BinaryOpExprNode::Operator::LogicalAnd;
            case BO_LOr: return detail::BinaryOpExprNode::Operator::LogicalOr;
            case BO_Assign:
            case BO_AddAssign:
            case BO_SubAssign:
            case BO_MulAssign:
            case BO_DivAssign:
            case BO_RemAssign:
            case BO_ShlAssign:
            case BO_ShrAssign:
            case BO_AndAssign:
            case BO_XorAssign:
            case BO_OrAssign: UNREACHABLE();
            default:
                UNIMPLEMENT("Unsupported binary operator: " << op);
                return detail::BinaryOpExprNode::Operator::Add;
        }
    }

    SymbolicExpr::Type deriveType(clang::QualType type) {
        if (auto atomic = type->getAs<clang::AtomicType>())
            return deriveType(atomic->getValueType());
        if (auto ptr = type->getAs<clang::PointerType>())
            return deriveType(ptr->getPointeeType());
        return llvm::TypeSwitch<clang::QualType, SymbolicExpr::Type>(type.getCanonicalType())
            .Case([](const clang::BuiltinType *BT) -> SymbolicExpr::Type {
                using Kind = SymbolicExpr::ScalarKind;
                using enum clang::BuiltinType::Kind;

                switch (BT->getKind()) {
                    case Bool: return {Kind::Bool, 1};
                    case Char_S:
                    case SChar: return {Kind::Int, 8};
                    case Char_U:
                    case UChar: return {Kind::UInt, 8};

                    case Short: return {Kind::Int, 16};
                    case UShort: return {Kind::UInt, 16};

                    case Int: return {Kind::Int, 32};
                    case UInt: return {Kind::UInt, 32};

                    case Long: return {Kind::Int, 64};
                    case ULong: return {Kind::UInt, 64};

                    case LongLong: return {Kind::Int, 64};
                    case ULongLong: return {Kind::UInt, 64};
                    case Void: return {Kind::Void, 0};

                    default:
                        clang::LangOptions langOpts;
                        clang::PrintingPolicy pp(langOpts);
                        UNIMPLEMENT("Unsupported builtin type: " << BT->getName(pp).str());
                }
            })
            .Case([](const clang::EnumType * /*ET*/) -> SymbolicExpr::Type {
                return {SymbolicExpr::ScalarKind::Int, 32};
            })
            .Default([&](clang::QualType QT) -> SymbolicExpr::Type {
                if (QT->isStructureType()) {
                    return {SymbolicExpr::ScalarKind::Structure, 0};
                }
                UNIMPLEMENT("Unsupported non-builtin type: " << QT.getAsString());
            });
    }

    bool isValidOffsetOrLength(const SymbolicExpr &expr) {
        if (expr.isUnknown())
            return true;
        if (expr.tryEvalAsSymbolAddr())
            return false;
        return true;
    }

    bool is_symbol_addr(const Address &a) noexcept { return isa<SymbolAddress>(a); }

    bool isFrom(const SymbolicExpr &expr, const Address &fromAddr, SourcePoint fromPoint) {
        auto symbol = dyn_cast<const Symbol>(&expr);
        if (symbol == nullptr)
            return false;

        if (symbol->getFromPoint() == std::nullopt || symbol->getFromPoint().value() != fromPoint)
            return false;
        auto from = symbol->getFromAddr();
        if (from == std::nullopt)
            return false;
        return fromAddr == *from.value();
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> getSymbol(
        clang::QualType type,
        std::optional<utils::not_null<std::unique_ptr<const Address>>> from,
        SourcePoint fromPoint) {
        auto &factory = ExprFactoryScope::current();
        std::optional<Addr> fromAddr;
        if (from)
            fromAddr.emplace(factory, factory.importAddress(*from.value()));

        if (type->isPointerType()) {
            auto pointerType = llvm::cast<clang::PointerType>(type);
            auto addr = fromAddr ? Addr::symbol(pointerType->getPointeeType(), *fromAddr,
                                                std::move(fromPoint))
                                 : Addr::symbol(pointerType->getPointeeType(),
                                                std::move(fromPoint));
            auto addrExpr = addr.asExpr();
            return importThroughCurrentFactory(*addrExpr);
        }

        if (type->isArrayType()) {
            auto arrayType = llvm::cast<clang::ArrayType>(type);
            auto addr = fromAddr ? Addr::symbol(arrayType->getElementType(), *fromAddr,
                                                std::move(fromPoint))
                                 : Addr::symbol(arrayType->getElementType(),
                                                std::move(fromPoint));
            auto addrExpr = addr.asExpr();
            return importThroughCurrentFactory(*addrExpr);
        }

        if (type->isStructureType()) {
            if (!fromAddr)
                ERROR("Structure should *from* an `Address`.");
            auto *RD = type->getAsRecordDecl();
            if (!RD || !RD->isCompleteDefinition())
                ERROR("Incomplete struct definition");
            RD           = RD->getDefinition();
            auto &layout = RD->getASTContext().getASTRecordLayout(RD);
            return factory.cloneExpr(
                factory.structure(RD, layout, fromAddr->handle(), std::move(fromPoint)));
        }

        if (!fromAddr)
            ERROR("SymbolValue should *from* an `Address`.");
        return factory.cloneExpr(
            Expr::symbolValue(deriveType(type), *fromAddr, std::move(fromPoint)).handle());
    }

    bool Symbol::classof(const SymbolicExpr *e) {
        return dynamic_cast<const Symbol *>(e) != nullptr;
    }

    Symbol *Symbol::toThis(SymbolicExpr *e) {
        return dynamic_cast<Symbol *>(e);
    }

    const Symbol *Symbol::toThis(const SymbolicExpr *e) {
        return toThis(const_cast<SymbolicExpr *>(e));
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> Symbol::callGetACSLOfValueProxy(
        const Address &addr,
        const SymbolicExpr::GetACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned parentPrec,
        bool isRightChild) {
        return Address::callGetACSLOfValue(addr, config, usedPoints, currentPoint, parentPrec,
                                           isRightChild);
    }

    utils::not_null<SymbolicExpr *> Symbol::toSymbolicExpr() {
        auto *result = dynamic_cast<SymbolicExpr *>(this);
        if (result == nullptr)
            UNREACHABLE();
        return result;
    }
    utils::not_null<const SymbolicExpr *> Symbol::toSymbolicExpr() const {
        return const_cast<Symbol *>(this)->toSymbolicExpr();
    }

} // namespace acslg::analyzer::symbolic
