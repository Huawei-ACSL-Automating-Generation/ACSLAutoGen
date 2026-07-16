/**
 * @file expr.cpp
 * @brief Implements symbolic expression hierarchy utilities for comparison, simplification, and
 *        ACSL conversion.
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
#include "detail/aggregateNodes.h"
#include "detail/exprNodes.h"
#include "detail/factoryInternals.h"
#include "macros.h"
#include "utils.h"
#include "Analyzer/state.h"

namespace acslg::analyzer::symbolic {
    using detail::MaxMinOverRangeNode;
    using detail::QuantifierOverRangeNode;
    using detail::FieldAddressNode;
    using detail::StructureNode;
    using detail::SumOverRangeNode;
    using detail::SymbolValueNode;
    using detail::SymbolAddressNode;
    using detail::VariableAddressNode;

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

        auto internTyped = [this](auto node) {
            std::unique_ptr<SymbolicExpr> base = std::move(node);
            return intern(utils::not_null<std::unique_ptr<SymbolicExpr>>{std::move(base)});
        };

        if (auto *literal = expr.dyn_cast<const detail::LiteralExprNode>())
            return internTyped(literal->rebuildNode(newType));

        if (expr.isa<detail::UnknownExprNode>())
            return internTyped(makeNode<detail::UnknownExprNode>(newType));

        if (auto *index = expr.dyn_cast<const detail::RangeIndexNode>())
            return internTyped(
                detail::ExprFactoryInternals::makeNode<detail::RangeIndexNode>(
                    index->getName(), newType));

        if (auto *unaryExpr = expr.dyn_cast<const detail::UnaryOpExprNode>())
            return internTyped(makeNode<detail::UnaryOpExprNode>(
                unaryExpr->getOperator(), importExpr(unaryExpr->getSub()), newType));

        if (auto *binaryExpr = expr.dyn_cast<const detail::BinaryOpExprNode>())
            return internTyped(makeNode<detail::BinaryOpExprNode>(
                importExpr(binaryExpr->getLeft()), binaryExpr->getOperator(),
                importExpr(binaryExpr->getRight()), newType));

        if (auto *variableAddr = expr.dyn_cast<const VariableAddressNode>())
            return internTyped(detail::ExprFactoryInternals::makeNode<VariableAddressNode>(
                variableAddr->getFrom(), newType));

        if (auto *fieldAddr = expr.dyn_cast<const FieldAddressNode>())
            return internTyped(detail::ExprFactoryInternals::makeNode<FieldAddressNode>(
                fieldAddr->getPointeeType(), fieldAddr->getDefinition(),
                importAddress(fieldAddr->getBaseAddr().handle()), fieldAddr->getFieldIndex(),
                newType));

        if (auto *symbolAddr = expr.dyn_cast<const SymbolAddressNode>()) {
            std::optional<AddrHandle> from;
            if (auto existingFrom = symbolAddr->getFromAddrHandle())
                from = importAddress(*existingFrom);

            std::optional<ExprHandle> length;
            if (const auto &existingLength = symbolAddr->getLength(); existingLength)
                length = importExpr(existingLength->handle());

            return internTyped(detail::ExprFactoryInternals::makeNode<SymbolAddressNode>(
                symbolAddr->getPointeeType(), from,
                symbolAddr->getFromPoint().value(),
                importExpr(symbolAddr->getOffset()), length, newType));
        }

        if (auto *symbolVal = expr.dyn_cast<const SymbolValueNode>())
            return internTyped(detail::ExprFactoryInternals::makeNode<SymbolValueNode>(
                newType, importAddress(symbolVal->getFromAddrHandle()),
                symbolVal->getFromPoint().value()));

        if (auto *structure = expr.dyn_cast<const StructureNode>()) {
            std::vector<ExprHandle> fields;
            fields.reserve(structure->getNumFields());
            for (auto field : structure->fieldsValues())
                fields.push_back(importExpr(ExprHandle{field}));
            return internTyped(detail::ExprFactoryInternals::makeNode<StructureNode>(
                structure->getInfo(), std::move(fields), newType));
        }

        if (auto *sum = expr.dyn_cast<const SumOverRangeNode>())
            return internTyped(detail::ExprFactoryInternals::makeNode<SumOverRangeNode>(
                importAddress(sum->getRange().handle()), sum->getIndexName(),
                sum->getFromPoint().value(), newType));

        if (auto *quantifier = expr.dyn_cast<const QuantifierOverRangeNode>())
            return internTyped(detail::ExprFactoryInternals::makeNode<QuantifierOverRangeNode>(
                importAddress(quantifier->getRange().handle()), quantifier->getIndexName(),
                quantifier->getQuantifier(),
                importExpr(ExprHandle{quantifier->getPredicate()}), newType));

        if (auto *maxMin = expr.dyn_cast<const MaxMinOverRangeNode>())
            return internTyped(detail::ExprFactoryInternals::makeNode<MaxMinOverRangeNode>(
                importAddress(maxMin->getRange().handle()), maxMin->getIndexName(),
                maxMin->getExtremum(), importExpr(ExprHandle{maxMin->getExpr()}),
                maxMin->getFromPoint().value(), newType));

        ERROR("Unsupported SymbolicExpr type in ExprFactory::withValType: " + expr.dump());
    }

    ExprHandle getSubstitutedValueHandle(ExprFactory &factory,
                                         ExprHandle expr,
                                         const HashExprHandleMap &hashToExprMap) {
        ExprFactoryScope scope(factory);

        struct Substituter {
            ExprFactory &factory;
            const HashExprHandleMap &substitutions;

            AddrHandle requireAddress(ExprHandle handle) const {
                if (auto *addr = handle.dyn_cast<const Address>())
                    return factory.importAddress(AddrHandle{addr});
                UNREACHABLE();
            }

            AddrHandle requireRange(ExprHandle handle) const {
                if (auto range = SymbolAddressView::tryFrom(handle)) {
                    if (!range->length())
                        ERROR("Substituted expression should be a *range*");
                    return factory.importAddress(range->handle());
                }
                UNREACHABLE();
            }

            ExprHandle run(ExprHandle expr) const {
                if (auto it = substitutions.find(expr.hash()); it != substitutions.end())
                    return it->second;

                if (auto *literal = expr.dyn_cast<const detail::LiteralExprNode>())
                    return literal->importInto(factory);
                if (expr->isUnknown())
                    return factory.unknown();
                if (auto *rangeIndex = expr.dyn_cast<const detail::RangeIndexNode>())
                    return factory.rangeIndex(rangeIndex->getName());
                if (auto *varAddr = expr.dyn_cast<const VariableAddressNode>())
                    return factory.variableAddress(varAddr->getFrom()).asExpr();
                if (auto *fieldAddr = expr.dyn_cast<const FieldAddressNode>()) {
                    auto base = requireAddress(run(fieldAddr->getBaseAddr().handle().asExpr()));
                    return factory
                        .fieldAddress(fieldAddr->getPointeeType(),
                                      fieldAddr->getDefinition(), base,
                                      fieldAddr->getFieldIndex())
                        .asExpr();
                }
                if (auto *symbolValue = expr.dyn_cast<const SymbolValueNode>()) {
                    auto from = requireAddress(run(symbolValue->getFromAddrHandle().asExpr()));
                    return factory.symbolValue(symbolValue->getValType(), from,
                                               symbolValue->getFromPoint().value());
                }
                if (auto *symbolAddr = expr.dyn_cast<const SymbolAddressNode>()) {
                    std::optional<AddrHandle> from;
                    if (auto fromAddr = symbolAddr->getFromAddrHandle())
                        from = requireAddress(run(fromAddr->asExpr()));

                    std::optional<ExprHandle> length;
                    if (symbolAddr->getLength())
                        length = run(symbolAddr->getLength()->handle());

                    return factory
                        .symbolAddress(symbolAddr->getPointeeType(), from,
                                       symbolAddr->getFromPoint().value(),
                                       run(symbolAddr->getOffset()), length)
                        .asExpr();
                }
                if (auto *binary = expr.dyn_cast<const detail::BinaryOpExprNode>()) {
                    return factory.binary(run(binary->getLeft()),
                                          binary->getOperator(),
                                          run(binary->getRight()));
                }
                if (auto *unary = expr.dyn_cast<const detail::UnaryOpExprNode>())
                    return factory.unary(unary->getOperator(),
                                         run(unary->getSub()));
                if (auto *structure = expr.dyn_cast<const StructureNode>()) {
                    auto rebuilt = factory.importExpr(ExprHandle{structure});
                    for (size_t i = 0; i < structure->getNumFields(); ++i)
                        rebuilt = factory.withField(
                            rebuilt, i, run(ExprHandle{structure->getFieldValue(i)}));
                    return rebuilt;
                }
                if (auto *sum = expr.dyn_cast<const SumOverRangeNode>()) {
                    return makeSumOverRangeHandle(
                        factory, requireRange(run(sum->getRange().handle().asExpr())),
                        sum->getIndexName(), sum->getFromPoint().value());
                }
                if (auto *quantifier = expr.dyn_cast<const QuantifierOverRangeNode>()) {
                    return makeQuantifierOverRangeHandle(
                        factory, requireRange(run(quantifier->getRange().handle().asExpr())),
                        quantifier->getIndexName(), quantifier->getQuantifier(),
                        run(ExprHandle{quantifier->getPredicate()}));
                }
                if (auto *maxMin = expr.dyn_cast<const MaxMinOverRangeNode>()) {
                    return makeMaxMinOverRangeHandle(
                        factory, requireRange(run(maxMin->getRange().handle().asExpr())),
                        maxMin->getIndexName(), maxMin->getExtremum(),
                        run(ExprHandle{maxMin->getExpr()}), maxMin->getFromPoint().value());
                }

                ERROR("Unsupported SymbolicExpr node in handle value substitution.");
            }
        };

        return Substituter{factory, hashToExprMap}.run(expr);
    }

    ExprHandle getSubstitutedExprHandle(ExprFactory &factory,
                                        ExprHandle expr,
                                        const Path &pathSubTo,
                                        const SourcePoint &pointToSub) {
        ExprFactoryScope scope(factory);

        struct Substituter {
            ExprFactory &factory;
            const Path &pathSubTo;
            const SourcePoint &pointToSub;

            AddrHandle requireAddress(ExprHandle handle) const {
                if (auto *addr = handle.dyn_cast<const Address>())
                    return factory.importAddress(AddrHandle{addr});
                UNREACHABLE();
            }

            AddrHandle requireRange(ExprHandle handle) const {
                if (auto range = SymbolAddressView::tryFrom(handle)) {
                    if (!range->length())
                        ERROR("Substituted expression should be a *range*");
                    return factory.importAddress(range->handle());
                }
                ERROR("Substituted expression should be a *range*");
            }

            ExprHandle simplified(ExprHandle handle) const {
                return simplifiedExprHandle(factory, handle);
            }

            ExprHandle run(ExprHandle expr) const {
                if (auto *literal = expr.dyn_cast<const detail::LiteralExprNode>())
                    return literal->importInto(factory);
                if (expr->isUnknown())
                    return factory.unknown();
                if (auto *rangeIndex = expr.dyn_cast<const detail::RangeIndexNode>())
                    return factory.rangeIndex(rangeIndex->getName());
                if (auto *varAddr = expr.dyn_cast<const VariableAddressNode>())
                    return factory.variableAddress(varAddr->getFrom()).asExpr();
                if (auto *fieldAddr = expr.dyn_cast<const FieldAddressNode>()) {
                    auto base = requireAddress(run(fieldAddr->getBaseAddr().handle().asExpr()));
                    return factory
                        .fieldAddress(fieldAddr->getPointeeType(),
                                      fieldAddr->getDefinition(), base,
                                      fieldAddr->getFieldIndex())
                        .asExpr();
                }
                if (auto *symbolValue = expr.dyn_cast<const SymbolValueNode>()) {
                    auto fromPoint = symbolValue->getFromPoint();
                    if (fromPoint && fromPoint.value() != pointToSub)
                        return factory.importExpr(expr);

                    auto realFromAddr =
                        requireAddress(run(symbolValue->getFromAddrHandle().asExpr()));
                    if (auto value = pathSubTo.getMemoryState().read(realFromAddr))
                        return factory.importExpr(value.value());

                    return factory.symbolValue(symbolValue->getValType(), realFromAddr,
                                               pathSubTo.getStartPoint());
                }
                if (auto *symbolAddr = expr.dyn_cast<const SymbolAddressNode>()) {
                    auto fromPoint = symbolAddr->getFromPoint();
                    if (fromPoint && fromPoint.value() != pointToSub)
                        return factory.importExpr(expr);

                    std::optional<ExprHandle> length;
                    if (symbolAddr->getLength())
                        length = factory.importExpr(symbolAddr->getLength()->handle());

                    auto fromAddr = symbolAddr->getFromAddrHandle();
                    if (fromAddr == std::nullopt)
                        return factory
                            .symbolAddress(symbolAddr->getPointeeType(), std::nullopt,
                                           pointToSub,
                                           factory.importExpr(
                                               symbolAddr->getOffset()),
                                           length)
                            .asExpr();

                    auto realFromAddr = requireAddress(run(fromAddr->asExpr()));
                    auto offset       = simplified(run(symbolAddr->getOffset()));
                    if (symbolAddr->getLength())
                        length = simplified(run(symbolAddr->getLength()->handle()));

                    if (auto value = pathSubTo.getMemoryState().read(realFromAddr)) {
                        auto realAddr = tryEvalAsSymbolAddrHandle(factory, value.value());
                        if (realAddr == std::nullopt)
                            ERROR("This expr should be a `SymbolAddress");

                        Addr concreteAddr{factory, realAddr.value()};
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
                if (auto *binary = expr.dyn_cast<const detail::BinaryOpExprNode>()) {
                    return factory.binary(run(binary->getLeft()),
                                          binary->getOperator(),
                                          run(binary->getRight()));
                }
                if (auto *unary = expr.dyn_cast<const detail::UnaryOpExprNode>())
                    return factory.unary(unary->getOperator(),
                                         run(unary->getSub()));
                if (auto *structure = expr.dyn_cast<const StructureNode>()) {
                    auto rebuilt = factory.importExpr(ExprHandle{structure});
                    for (size_t i = 0; i < structure->getNumFields(); ++i)
                        rebuilt = factory.withField(
                            rebuilt, i, run(ExprHandle{structure->getFieldValue(i)}));
                    return rebuilt;
                }
                if (auto *sum = expr.dyn_cast<const SumOverRangeNode>()) {
                    if (sum->getFromPoint().value() != pointToSub)
                        return factory.importExpr(expr);
                    return makeSumOverRangeHandle(
                        factory, requireRange(run(sum->getRange().handle().asExpr())),
                        sum->getIndexName(), pathSubTo.getStartPoint());
                }
                if (auto *quantifier = expr.dyn_cast<const QuantifierOverRangeNode>()) {
                    return makeQuantifierOverRangeHandle(
                        factory, requireRange(run(quantifier->getRange().handle().asExpr())),
                        quantifier->getIndexName(), quantifier->getQuantifier(),
                        run(ExprHandle{quantifier->getPredicate()}));
                }
                if (auto *maxMin = expr.dyn_cast<const MaxMinOverRangeNode>()) {
                    auto range = requireRange(run(maxMin->getRange().handle().asExpr()));
                    auto body  = run(ExprHandle{maxMin->getExpr()});
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
                                              ExprHandle expr,
                                              const SymbolAddrBaseInfo &rangeBase,
                                              ExprHandle indexExpr) {
        ExprFactoryScope scope(factory);

        struct Substituter {
            ExprFactory &factory;
            const SymbolAddrBaseInfo &rangeBase;
            ExprHandle indexExpr;

            AddrHandle requireAddress(ExprHandle handle) const {
                if (auto *addr = handle.dyn_cast<const Address>())
                    return factory.importAddress(AddrHandle{addr});
                UNREACHABLE();
            }

            AddrHandle requireRange(ExprHandle handle) const {
                if (auto range = SymbolAddressView::tryFrom(handle)) {
                    if (!range->length())
                        ERROR("Substituted expression should be a *range*");
                    return factory.importAddress(range->handle());
                }
                UNREACHABLE();
            }

            ExprHandle run(ExprHandle expr) const {
                (void)rangeBase;

                if (auto *literal = expr.dyn_cast<const detail::LiteralExprNode>())
                    return literal->importInto(factory);
                if (expr->isUnknown())
                    return factory.unknown();
                if (expr->isRangeIndex())
                    return indexExpr;
                if (auto *varAddr = expr.dyn_cast<const VariableAddressNode>())
                    return factory.variableAddress(varAddr->getFrom()).asExpr();
                if (auto *fieldAddr = expr.dyn_cast<const FieldAddressNode>()) {
                    auto base = requireAddress(run(fieldAddr->getBaseAddr().handle().asExpr()));
                    return factory
                        .fieldAddress(fieldAddr->getPointeeType(),
                                      fieldAddr->getDefinition(), base,
                                      fieldAddr->getFieldIndex())
                        .asExpr();
                }
                if (auto *symbolValue = expr.dyn_cast<const SymbolValueNode>()) {
                    auto from = requireAddress(run(symbolValue->getFromAddrHandle().asExpr()));
                    return factory.symbolValue(symbolValue->getValType(), from,
                                               symbolValue->getFromPoint().value());
                }
                if (auto *symbolAddr = expr.dyn_cast<const SymbolAddressNode>()) {
                    std::optional<AddrHandle> from;
                    if (auto fromAddr = symbolAddr->getFromAddrHandle())
                        from = requireAddress(run(fromAddr->asExpr()));

                    std::optional<ExprHandle> length;
                    if (symbolAddr->getLength())
                        length = run(symbolAddr->getLength()->handle());

                    return factory
                        .symbolAddress(symbolAddr->getPointeeType(), from,
                                       symbolAddr->getFromPoint().value(),
                                       run(symbolAddr->getOffset()), length)
                        .asExpr();
                }
                if (auto *binary = expr.dyn_cast<const detail::BinaryOpExprNode>())
                    return factory.binary(run(binary->getLeft()),
                                          binary->getOperator(),
                                          run(binary->getRight()));
                if (auto *unary = expr.dyn_cast<const detail::UnaryOpExprNode>())
                    return factory.unary(unary->getOperator(),
                                         run(unary->getSub()));
                if (auto *structure = expr.dyn_cast<const StructureNode>()) {
                    auto rebuilt = factory.importExpr(ExprHandle{structure});
                    for (size_t i = 0; i < structure->getNumFields(); ++i)
                        rebuilt = factory.withField(
                            rebuilt, i, run(ExprHandle{structure->getFieldValue(i)}));
                    return rebuilt;
                }
                if (auto *sum = expr.dyn_cast<const SumOverRangeNode>()) {
                    return makeSumOverRangeHandle(
                        factory, requireRange(run(sum->getRange().handle().asExpr())),
                        sum->getIndexName(), sum->getFromPoint().value());
                }
                if (auto *quantifier = expr.dyn_cast<const QuantifierOverRangeNode>()) {
                    return makeQuantifierOverRangeHandle(
                        factory, requireRange(run(quantifier->getRange().handle().asExpr())),
                        quantifier->getIndexName(), quantifier->getQuantifier(),
                        run(ExprHandle{quantifier->getPredicate()}));
                }
                if (auto *maxMin = expr.dyn_cast<const MaxMinOverRangeNode>()) {
                    return makeMaxMinOverRangeHandle(
                        factory, requireRange(run(maxMin->getRange().handle().asExpr())),
                        maxMin->getIndexName(), maxMin->getExtremum(),
                        run(ExprHandle{maxMin->getExpr()}), maxMin->getFromPoint().value());
                }

                ERROR("Unsupported SymbolicExpr node in handle range-index substitution.");
            }
        };

        return Substituter{factory, rangeBase, indexExpr}.run(expr);
    }

    ExprHandle ExprFactory::importExpr(ExprHandle expr) { return importNode(*expr); }

    AddrHandle ExprFactory::importAddress(AddrHandle address) {
        return importAddressNode(*address);
    }

    AddrHandle ExprFactory::importAddressNode(const Address &address) {
        return AddrHandle{cast<const Address>(importNode(address).get().get())};
    }

    ExprHandle ExprFactory::importNode(const SymbolicExpr &expr) {
        auto preserveImportedType = [this, &expr](ExprHandle imported) {
            if (imported->getValType() == expr.getValType())
                return imported;
            return withValType(imported, expr.getValType());
        };

        if (auto *literal = dyn_cast<detail::LiteralExprNode>(&expr))
            return preserveImportedType(literal->importInto(*this));

        if (isa<detail::UnknownExprNode>(&expr))
            return preserveImportedType(unknown());

        if (auto *index = dyn_cast<detail::RangeIndexNode>(&expr))
            return preserveImportedType(rangeIndex(index->getName()));

        if (auto *unaryExpr = dyn_cast<detail::UnaryOpExprNode>(&expr))
            return preserveImportedType(
                unary(unaryExpr->getOperator(), importNode(*unaryExpr->getSub())));

        if (auto *binaryExpr = dyn_cast<detail::BinaryOpExprNode>(&expr)) {
            auto left  = importNode(*binaryExpr->getLeft());
            auto right = importNode(*binaryExpr->getRight());
            return preserveImportedType(binary(left, binaryExpr->getOperator(), right));
        }

        if (auto *variableAddr = dyn_cast<VariableAddressNode>(&expr))
            return preserveImportedType(variableAddress(variableAddr->getFrom()).asExpr());

        if (auto *fieldAddr = dyn_cast<FieldAddressNode>(&expr)) {
            auto base = importAddressNode(*fieldAddr->getBaseAddr());
            return preserveImportedType(
                fieldAddress(fieldAddr->getPointeeType(), fieldAddr->getDefinition(), base,
                             fieldAddr->getFieldIndex())
                    .asExpr());
        }

        if (auto *symbolAddr = dyn_cast<SymbolAddressNode>(&expr)) {
            auto from = symbolAddr->getFromAddrHandle();
            if (from)
                from = importAddressNode(**from);

            std::optional<ExprHandle> length;
            if (const auto &legacyLength = symbolAddr->getLength(); legacyLength)
                length = importNode(*legacyLength.value());

            return preserveImportedType(
                symbolAddress(symbolAddr->getPointeeType(), from,
                              symbolAddr->getFromPoint().value(),
                              importNode(*symbolAddr->getOffset()), length)
                    .asExpr());
        }

        if (auto *symbolVal = dyn_cast<SymbolValueNode>(&expr))
            return preserveImportedType(
                symbolValue(symbolVal->getValType(),
                            importAddressNode(*symbolVal->getFromAddrHandle()),
                            symbolVal->getFromPoint().value()));

        if (auto *structure = dyn_cast<StructureNode>(&expr)) {
            std::vector<ExprHandle> fields;
            fields.reserve(structure->getNumFields());
            for (auto field : structure->fieldsValues())
                fields.push_back(importNode(*field));
            return preserveImportedType(intern(
                detail::ExprFactoryInternals::makeNode<StructureNode>(
                    structure->getInfo(), std::move(fields))));
        }

        if (auto *sum = dyn_cast<SumOverRangeNode>(&expr)) {
            auto fromPoint = sum->getFromPoint();
            if (!fromPoint)
                ERROR("SumOverRange must have a source point.");
            return preserveImportedType(makeSumOverRangeHandle(
                *this, sum->getRange().handle(), sum->getIndexName(), fromPoint.value()));
        }

        if (auto *quantifier = dyn_cast<QuantifierOverRangeNode>(&expr)) {
            return preserveImportedType(makeQuantifierOverRangeHandle(
                *this, quantifier->getRange().handle(), quantifier->getIndexName(),
                quantifier->getQuantifier(), quantifier->getPredicate()));
        }

        if (auto *maxMin = dyn_cast<MaxMinOverRangeNode>(&expr)) {
            auto fromPoint = maxMin->getFromPoint();
            if (!fromPoint)
                ERROR("MaxMinOverRange must have a source point.");
            return preserveImportedType(makeMaxMinOverRangeHandle(
                *this, maxMin->getRange().handle(), maxMin->getIndexName(),
                maxMin->getExtremum(),
                maxMin->getExpr(), fromPoint.value()));
        }

        ERROR("Unsupported SymbolicExpr type in ExprFactory::importExpr: " + expr.dump());
    }

    namespace {
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

        const detail::LiteralExprNode *evaluateToLiteralNode(const SymbolicExpr &expr);

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

            if (auto *bo = dyn_cast<detail::BinaryOpExprNode>(&e)) {
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

            if (auto *uo = dyn_cast<detail::UnaryOpExprNode>(&e)) {
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

    ExprHandle ExprFactory::literal(bool value) {
        return intern(makeNode<detail::LiteralExprNode>(value));
    }

    ExprHandle ExprFactory::literal(int value) {
        return intern(makeNode<detail::LiteralExprNode>(value));
    }

    ExprHandle ExprFactory::literal(unsigned int value) {
        return intern(makeNode<detail::LiteralExprNode>(value));
    }

    ExprHandle ExprFactory::literal(short value) {
        return intern(makeNode<detail::LiteralExprNode>(value));
    }

    ExprHandle ExprFactory::literal(unsigned short value) {
        return intern(makeNode<detail::LiteralExprNode>(value));
    }

    ExprHandle ExprFactory::literal(int64_t value) {
        return intern(makeNode<detail::LiteralExprNode>(value));
    }

    ExprHandle ExprFactory::literal(uint64_t value) {
        return intern(makeNode<detail::LiteralExprNode>(value));
    }

    ExprHandle ExprFactory::unknown() {
        return intern(makeNode<detail::UnknownExprNode>());
    }

    ExprHandle ExprFactory::rangeIndex(std::string_view name) {
        return intern(
            detail::ExprFactoryInternals::makeNode<detail::RangeIndexNode>(name));
    }

    ExprHandle ExprFactory::symbolValue(SymbolicExpr::Type varType,
                                        AddrHandle from,
                                        SourcePoint fromPoint) {
        return intern(detail::ExprFactoryInternals::makeNode<SymbolValueNode>(
            varType, from, std::move(fromPoint)));
    }

    ExprHandle ExprFactory::unary(UnaryOp op, ExprHandle expr) {
        return intern(makeNode<detail::UnaryOpExprNode>(op, expr));
    }

    ExprHandle ExprFactory::binary(ExprHandle left, BinaryOp op, ExprHandle right) {
        return intern(makeNode<detail::BinaryOpExprNode>(left, op, right));
    }

    ExprHandle ExprFactory::simplifiedBinary(ExprHandle left,
                                             BinaryOp op,
                                             ExprHandle right) {
        return simplifiedExprHandle(*this, binary(left, op, right));
    }

    AddrHandle ExprFactory::variableAddress(utils::not_null<const clang::VarDecl *> from) {
        return internAddress(
            detail::ExprFactoryInternals::makeNode<VariableAddressNode>(from));
    }

    AddrHandle ExprFactory::symbolAddress(
        clang::QualType pointeeType,
        std::optional<AddrHandle> from,
        SourcePoint fromPoint,
        std::optional<ExprHandle> offset,
        std::optional<ExprHandle> length) {
        auto resolvedOffset =
            offset.value_or(literal(static_cast<int64_t>(SymbolAddressView::ZERO_OFFSET)));
        return internAddress(detail::ExprFactoryInternals::makeNode<SymbolAddressNode>(
            pointeeType, from, std::move(fromPoint), resolvedOffset, length));
    }

    AddrHandle ExprFactory::withOffset(AddrHandle address, ExprHandle offset) {
        const auto &symbolAddr = address.cast<SymbolAddressNode>();
        auto from              = symbolAddr.getFromAddrHandle();

        std::optional<ExprHandle> length;
        if (const auto &existingLength = symbolAddr.getLength(); existingLength)
            length = importExpr(existingLength->handle());

        return symbolAddress(symbolAddr.getPointeeType(), from,
                             symbolAddr.getFromPoint().value(), offset, length);
    }

    AddrHandle ExprFactory::withAddedOffset(AddrHandle address, ExprHandle extra) {
        const auto &symbolAddr = address.cast<SymbolAddressNode>();
        auto newOffset = simplifiedBinary(importExpr(symbolAddr.getOffset()),
                                          detail::BinaryOpExprNode::Operator::Add, extra);
        return withOffset(address, newOffset);
    }

    AddrHandle ExprFactory::withSubtractedOffset(AddrHandle address, ExprHandle extra) {
        const auto &symbolAddr = address.cast<SymbolAddressNode>();
        auto newOffset = simplifiedBinary(importExpr(symbolAddr.getOffset()),
                                          detail::BinaryOpExprNode::Operator::Subtract, extra);
        return withOffset(address, newOffset);
    }

    AddrHandle ExprFactory::withLength(AddrHandle address, ExprHandle length) {
        const auto &symbolAddr = address.cast<SymbolAddressNode>();
        auto from              = symbolAddr.getFromAddrHandle();

        return symbolAddress(symbolAddr.getPointeeType(), from,
                             symbolAddr.getFromPoint().value(),
                             importExpr(symbolAddr.getOffset()), length);
    }

    AddrHandle ExprFactory::withAddedLength(AddrHandle address, ExprHandle extra) {
        const auto &symbolAddr = address.cast<SymbolAddressNode>();
        auto currentLength =
            symbolAddr.getLength() ? importExpr(symbolAddr.getLength()->handle()) : literal(1);
        auto newLength = simplifiedBinary(currentLength, detail::BinaryOpExprNode::Operator::Add,
                                          extra);
        return withLength(address, newLength);
    }

    AddrHandle ExprFactory::withoutLength(AddrHandle address) {
        const auto &symbolAddr = address.cast<SymbolAddressNode>();
        auto from              = symbolAddr.getFromAddrHandle();

        return symbolAddress(symbolAddr.getPointeeType(), from,
                             symbolAddr.getFromPoint().value(),
                             importExpr(symbolAddr.getOffset()), std::nullopt);
    }

    AddrHandle ExprFactory::fieldAddress(clang::QualType pointeeType,
                                         const clang::RecordDecl *record,
                                         AddrHandle baseAddr,
                                         size_t fieldIndex) {
        return internAddress(detail::ExprFactoryInternals::makeNode<FieldAddressNode>(
            pointeeType, record, baseAddr, fieldIndex));
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
        return intern(detail::ExprFactoryInternals::makeNode<StructureNode>(
            StructureInfo{record, layout}, std::move(fields)));
    }

    ExprHandle ExprFactory::withField(ExprHandle structure, size_t index, ExprHandle value) {
        const auto &structureNode = structure.cast<StructureNode>();
        if (index >= structureNode.getNumFields())
            ERROR("Out-of-bounds access");

        std::vector<ExprHandle> fields;
        fields.reserve(structureNode.getNumFields());
        size_t currentIndex = 0;
        for (auto field : structureNode.fieldsValues()) {
            fields.push_back(currentIndex == index ? value : importExpr(ExprHandle{field}));
            ++currentIndex;
        }

        return intern(detail::ExprFactoryInternals::makeNode<StructureNode>(
            structureNode.getInfo(), std::move(fields)));
    }

    ExprHandle simplifiedExprHandle(ExprFactory &factory, ExprHandle expr) {
        ExprFactoryScope scope(factory);
        if (expr->isUnknown())
            return factory.unknown();

        if (auto *symbolAddr = expr.dyn_cast<const SymbolAddressNode>()) {
            if (symbolAddr->getLength())
                ERROR("Address range is solely for address representation and should not be "
                      "used as an expression.");
            return factory.importExpr(ExprHandle{symbolAddr});
        }

        if (auto *binary = expr.dyn_cast<const detail::BinaryOpExprNode>()) {
            if (binary->isLinear())
                return binary->simplifiedExprIfLinear();

            if (auto c = evaluateToLiteralNode(*binary))
                return factory.importExpr(ExprHandle{c});

            auto lhs = simplifiedExprHandle(factory, binary->getLeft());
            auto rhs = simplifiedExprHandle(factory, binary->getRight());

            using Op = detail::BinaryOpExprNode::Operator;
            if (binary->getOperator() == Op::Equal || binary->getOperator() == Op::NotEqual) {
                auto simplifyBoolCmp = [&](ExprHandle boolExpr,
                                           ExprHandle litExpr) -> std::optional<ExprHandle> {
                    auto lit = litExpr.dyn_cast<const detail::LiteralExprNode>();
                    if (!lit)
                        return std::nullopt;
                    auto value = lit->getLiteralValue();
                    if (value != 0 && value != 1)
                        return std::nullopt;
                    if (!isBooleanExpr(*boolExpr))
                        return std::nullopt;

                    const bool expectTrue =
                        binary->getOperator() == Op::Equal ? value == 1 : value == 0;
                    if (expectTrue)
                        return factory.importExpr(boolExpr);
                    return factory.unary(detail::UnaryOpExprNode::Operator::LogicalNot,
                                         factory.importExpr(boolExpr));
                };

                if (auto simplified = simplifyBoolCmp(lhs, rhs))
                    return *simplified;
                if (auto simplified = simplifyBoolCmp(rhs, lhs))
                    return *simplified;
            }

            if (binary->getOperator() == Op::LogicalAnd) {
                if (auto leftConst = evaluateToLiteralNode(*lhs)) {
                    if (!literalAsBool(*leftConst))
                        return factory.importExpr(ExprHandle{leftConst});
                    return rhs;
                }
                if (auto rightConst = evaluateToLiteralNode(*rhs)) {
                    if (!literalAsBool(*rightConst))
                        return factory.importExpr(ExprHandle{rightConst});
                    return lhs;
                }
            } else if (binary->getOperator() == Op::LogicalOr) {
                if (auto leftConst = evaluateToLiteralNode(*lhs)) {
                    if (literalAsBool(*leftConst))
                        return factory.importExpr(ExprHandle{leftConst});
                    return rhs;
                }
                if (auto rightConst = evaluateToLiteralNode(*rhs)) {
                    if (literalAsBool(*rightConst))
                        return factory.importExpr(ExprHandle{rightConst});
                    return lhs;
                }
            }

            return factory.binary(lhs, binary->getOperator(), rhs);
        }

        if (auto *unary = expr.dyn_cast<const detail::UnaryOpExprNode>()) {
            if (unary->isLinear())
                return unary->simplifiedExprIfLinear();
            return factory.unary(unary->getOperator(),
                                 simplifiedExprHandle(factory, unary->getSub()));
        }

        if (auto *literal = expr.dyn_cast<const detail::LiteralExprNode>())
            return literal->simplifiedExprIfLinear();

        return factory.importExpr(expr);
    }

    std::optional<AddrHandle> tryEvalAsSymbolAddrHandle(ExprFactory &factory,
                                                        ExprHandle expr) {
        ExprFactoryScope scope(factory);
        auto simplified = simplifiedExprHandle(factory, expr);

        if (auto *symbolAddr = simplified.dyn_cast<const SymbolAddressNode>())
            return factory.importAddress(AddrHandle{symbolAddr});

        auto *binary = simplified.dyn_cast<const detail::BinaryOpExprNode>();
        if (!binary)
            return std::nullopt;

        auto left  = binary->getLeft();
        auto right = binary->getRight();
        auto lhs   = tryEvalAsSymbolAddrHandle(factory, left);
        auto rhs   = tryEvalAsSymbolAddrHandle(factory, right);
        if (lhs && rhs)
            return std::nullopt;
        if (!lhs && !rhs)
            return std::nullopt;

        auto isValidOffsetOrLengthHandle = [&](ExprHandle candidate) {
            if (candidate->isUnknown())
                return true;
            return !tryEvalAsSymbolAddrHandle(factory, candidate).has_value();
        };

        using Op = detail::BinaryOpExprNode::Operator;
        if (lhs) {
            if (!isValidOffsetOrLengthHandle(right))
                return std::nullopt;
            auto offset = factory.importExpr(right);
            switch (binary->getOperator()) {
                case Op::Add: return factory.withAddedOffset(*lhs, offset);
                case Op::Subtract: return factory.withSubtractedOffset(*lhs, offset);
                default: return std::nullopt;
            }
        }

        if (!isValidOffsetOrLengthHandle(left))
            return std::nullopt;
        auto offset = factory.importExpr(left);
        switch (binary->getOperator()) {
            case Op::Add: return factory.withAddedOffset(*rhs, offset);
            case Op::Subtract: return std::nullopt;
            default: return std::nullopt;
        }
    }

    ExprHandle SymbolicExpr::simplifiedExpr() const {
        auto &factory = ExprFactoryScope::current();
        return simplifiedExprHandle(factory, ExprHandle{this});
    }

    /**
     * @brief Simplify a linear expression by rebuilding it as a minimal sum of terms.
     * @return Interned simplified expression when linear; otherwise the imported node.
     */
    ExprHandle SymbolicExpr::simplifiedExprIfLinear() const {
        if (!isLinear())
            return ExprFactoryScope::current().importExpr(ExprHandle{this});
        auto [hashPtrMap, hashIdMap] = collectUsedSymbols(ExprHandle{this});

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
                    result = factory.importExpr(ExprHandle{expr});
                else
                    result = factory.binary(factory.literal(static_cast<int64_t>(C)),
                                            Multiply, factory.importExpr(ExprHandle{expr}));
            } else {
                unsigned absC = std::abs(C);
                ExprHandle varExpr = factory.importExpr(ExprHandle{expr});
                if (absC != 1)
                    varExpr = factory.binary(factory.literal(static_cast<int64_t>(absC)),
                                             Multiply, factory.importExpr(ExprHandle{expr}));
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
        return result.value();
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

    std::unique_ptr<detail::LiteralExprNode>
    detail::LiteralExprNode::rebuildNode(std::optional<Type> explicitType) const {
        switch (getLiteralType()) {
            case LiteralType::Boolean:
                return std::unique_ptr<LiteralExprNode>{
                    new LiteralExprNode(data_.boolValue, explicitType)};
            case LiteralType::Int:
                return std::unique_ptr<LiteralExprNode>{
                    new LiteralExprNode(data_.intValue, explicitType)};
            case LiteralType::UnsignedInt:
                return std::unique_ptr<LiteralExprNode>{
                    new LiteralExprNode(data_.uintValue, explicitType)};
            case LiteralType::Short:
                return std::unique_ptr<LiteralExprNode>{
                    new LiteralExprNode(data_.shortValue, explicitType)};
            case LiteralType::UnsignedShort:
                return std::unique_ptr<LiteralExprNode>{
                    new LiteralExprNode(data_.ushortValue, explicitType)};
            case LiteralType::Int64:
                return std::unique_ptr<LiteralExprNode>{
                    new LiteralExprNode(data_.int64Value, explicitType)};
            case LiteralType::UInt64:
                return std::unique_ptr<LiteralExprNode>{
                    new LiteralExprNode(data_.uint64Value, explicitType)};
        }

        UNREACHABLE();
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

    size_t SymbolValueNode::hash() const {
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

    size_t SymbolAddressNode::hash() const {
        size_t seed = utils::hash_val(SymbolicExpr::getKind(), fromPoint_.hash(), offset_->hash(),
                                      length_ ? length_.value()->hash() : 0);

        seed = utils::hash_val(seed, fromAddr_ ? fromAddr_.value()->hash() : 0);
        return seed;
    }

    size_t VariableAddressNode::hash() const { return utils::hash_val(getKind(), from_.get()); }

    size_t FieldAddressNode::hash() const {
        return utils::hash_val(getKind(), baseAddr_->hash(), fieldIndex_);
    }

    size_t StructureNode::hash() const {
        auto seed = utils::hash_val(SymbolicExpr::getKind(), info_.definition_.get());
        for (auto &field : fields_)
            seed = utils::hash_val(seed, field->hash());
        return seed;
    }

    size_t detail::UnknownExprNode::hash() const { return utils::hash_val(getKind()); }

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

    std::string detail::UnknownExprNode::dump() const {
        return utils::dump_fmt::hint("{unknown}");
    }

    LiteralExprView::LiteralExprView(ExprHandle handle) : handle_(handle) {
        if (!handle_->isLiteralExpr())
            ERROR("LiteralExprView requires a literal expression.");
    }

    std::optional<LiteralExprView> LiteralExprView::tryFrom(ExprHandle handle) {
        if (!handle->isLiteralExpr())
            return std::nullopt;
        return LiteralExprView{handle};
    }

    int64_t LiteralExprView::value() const {
        return cast<const detail::LiteralExprNode>(handle_.get().get())->getLiteralValue();
    }

    UnaryExprView::UnaryExprView(ExprHandle handle) : handle_(handle) {
        if (!handle_->isUnaryExpr())
            ERROR("UnaryExprView requires a unary expression.");
    }

    std::optional<UnaryExprView> UnaryExprView::tryFrom(ExprHandle handle) {
        if (!handle->isUnaryExpr())
            return std::nullopt;
        return UnaryExprView{handle};
    }

    UnaryOp UnaryExprView::operation() const {
        return cast<const detail::UnaryOpExprNode>(handle_.get().get())->getOperator();
    }

    ExprHandle UnaryExprView::operand() const {
        return cast<const detail::UnaryOpExprNode>(handle_.get().get())->getSub();
    }

    BinaryExprView::BinaryExprView(ExprHandle handle) : handle_(handle) {
        if (!handle_->isBinaryExpr())
            ERROR("BinaryExprView requires a binary expression.");
    }

    std::optional<BinaryExprView> BinaryExprView::tryFrom(ExprHandle handle) {
        if (!handle->isBinaryExpr())
            return std::nullopt;
        return BinaryExprView{handle};
    }

    BinaryOp BinaryExprView::operation() const {
        return cast<const detail::BinaryOpExprNode>(handle_.get().get())->getOperator();
    }

    ExprHandle BinaryExprView::left() const {
        return cast<const detail::BinaryOpExprNode>(handle_.get().get())->getLeft();
    }

    ExprHandle BinaryExprView::right() const {
        return cast<const detail::BinaryOpExprNode>(handle_.get().get())->getRight();
    }

    std::string SymbolValueNode::dump() const {
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

    SymbolValueView::SymbolValueView(ExprHandle handle) : handle_(handle) {
        if (!handle_->isSymbolValue())
            ERROR("SymbolValueView requires a SymbolValue expression.");
    }

    std::optional<SymbolValueView> SymbolValueView::tryFrom(ExprHandle handle) {
        if (!handle->isSymbolValue())
            return std::nullopt;
        return SymbolValueView{handle};
    }

    AddrHandle SymbolValueView::from() const {
        return cast<const SymbolValueNode>(handle_.get().get())->getFromAddrHandle();
    }

    std::optional<SourcePoint> SymbolValueView::fromPoint() const {
        return cast<const SymbolValueNode>(handle_.get().get())->getFromPoint();
    }

    std::optional<utils::not_null<const clang::VarDecl *>> SymbolValueView::fromRoot() const {
        return cast<const SymbolValueNode>(handle_.get().get())->getFromRoot();
    }

    std::string SymbolAddressNode::dump() const {
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

    std::string VariableAddressNode::dump() const {
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

    std::string FieldAddressNode::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        oss << type("FieldAddress") << " {" << key("from") << "=";
        oss << key("field of") << ":" << baseAddr_.get()->dump() << "["
            << utils::dump_fmt::lit(std::to_string(fieldIndex_)) << "]";
        oss << "}";
        return oss.str();
    }

    VariableAddressView::VariableAddressView(AddrHandle handle) : handle_(handle) {
        if (!handle_->isVariableAddress())
            ERROR("VariableAddressView requires a variable address.");
    }

    std::optional<VariableAddressView> VariableAddressView::tryFrom(AddrHandle handle) {
        if (!handle->isVariableAddress())
            return std::nullopt;
        return VariableAddressView{handle};
    }

    std::optional<VariableAddressView> VariableAddressView::tryFrom(ExprHandle handle) {
        if (!handle->isVariableAddress())
            return std::nullopt;
        return tryFrom(AddrHandle{cast<const Address>(handle.get().get())});
    }

    utils::not_null<const clang::VarDecl *> VariableAddressView::declaration() const {
        return cast<const VariableAddressNode>(handle_.get().get())->getFrom();
    }

    FieldAddressView::FieldAddressView(AddrHandle handle) : handle_(handle) {
        if (!handle_->isFieldAddress())
            ERROR("FieldAddressView requires a field address.");
    }

    std::optional<FieldAddressView> FieldAddressView::tryFrom(AddrHandle handle) {
        if (!handle->isFieldAddress())
            return std::nullopt;
        return FieldAddressView{handle};
    }

    std::optional<FieldAddressView> FieldAddressView::tryFrom(ExprHandle handle) {
        if (!handle->isFieldAddress())
            return std::nullopt;
        return tryFrom(AddrHandle{cast<const Address>(handle.get().get())});
    }

    utils::not_null<const clang::RecordDecl *> FieldAddressView::definition() const {
        return cast<const FieldAddressNode>(handle_.get().get())->getDefinition();
    }

    AddrHandle FieldAddressView::base() const {
        return cast<const FieldAddressNode>(handle_.get().get())->getBaseAddr().handle();
    }

    size_t FieldAddressView::fieldIndex() const {
        return cast<const FieldAddressNode>(handle_.get().get())->getFieldIndex();
    }

    std::optional<utils::not_null<const clang::VarDecl *>> FieldAddressView::fromRoot() const {
        return handle_->getFromRoot();
    }

    SymbolAddressView::SymbolAddressView(AddrHandle handle) : handle_(handle) {
        if (!handle_->isSymbolAddress())
            ERROR("SymbolAddressView requires a symbol address.");
    }

    std::optional<SymbolAddressView> SymbolAddressView::tryFrom(AddrHandle handle) {
        if (!handle->isSymbolAddress())
            return std::nullopt;
        return SymbolAddressView{handle};
    }

    std::optional<SymbolAddressView> SymbolAddressView::tryFrom(ExprHandle handle) {
        if (!handle->isSymbolAddress())
            return std::nullopt;
        return tryFrom(AddrHandle{cast<const Address>(handle.get().get())});
    }

    clang::QualType SymbolAddressView::pointeeType() const {
        return handle_->getPointeeType();
    }

    std::optional<AddrHandle> SymbolAddressView::from() const {
        return cast<const SymbolAddressNode>(handle_.get().get())->getFromAddrHandle();
    }

    std::optional<SourcePoint> SymbolAddressView::fromPoint() const {
        return cast<const SymbolAddressNode>(handle_.get().get())->getFromPoint();
    }

    ExprHandle SymbolAddressView::offset() const {
        return cast<const SymbolAddressNode>(handle_.get().get())->getOffset();
    }

    std::optional<ExprHandle> SymbolAddressView::length() const {
        const auto &length = cast<const SymbolAddressNode>(handle_.get().get())->getLength();
        if (!length)
            return std::nullopt;
        return length->handle();
    }

    std::optional<ExprHandle> SymbolAddressView::rightBound() const {
        return cast<const SymbolAddressNode>(handle_.get().get())->getRightBound();
    }

    SymbolAddrBaseInfo SymbolAddressView::baseInfo() const {
        return cast<const SymbolAddressNode>(handle_.get().get())->getBaseInfo();
    }

    std::optional<utils::not_null<const clang::VarDecl *>> SymbolAddressView::fromRoot() const {
        return handle_->getFromRoot();
    }

    int SymbolAddressView::dimension() const { return handle_->getDimension(); }

    std::string StructureInfo::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        std::string structName = definition_->getNameAsString();
        uint64_t sizeBits      = static_cast<uint64_t>(layout_.getSize().getQuantity()) * 8;

        oss << type("Struct") << "(" << accent(structName) << ", " << key("size") << "="
            << lit(std::to_string(sizeBits)) << " " << hint("bits") << ")";
        return oss.str();
    }

    StructureView::StructureView(ExprHandle handle) : handle_(handle) {
        if (!handle_->isStructure())
            ERROR("StructureView requires a Structure expression.");
    }

    std::optional<StructureView> StructureView::tryFrom(ExprHandle handle) {
        if (!handle->isStructure())
            return std::nullopt;
        return StructureView{handle};
    }

    size_t StructureView::size() const {
        return cast<const StructureNode>(handle_.get().get())->getNumFields();
    }

    ExprHandle StructureView::field(size_t index) const {
        return ExprHandle{cast<const StructureNode>(handle_.get().get())->getFieldValue(index)};
    }

    const StructureInfo &StructureView::info() const {
        return cast<const StructureNode>(handle_.get().get())->getInfo();
    }

    std::optional<SourcePoint> StructureView::fromPoint() const {
        return cast<const StructureNode>(handle_.get().get())->getFromPoint();
    }

    std::string StructureNode::dump() const {
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

    utils::expected<std::string, SymbolicExpr::GetACSLError>
    detail::UnknownExprNode::doGetACSL(
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

    utils::expected<std::string, SymbolicExpr::GetACSLError> SymbolValueNode::doGetACSL(
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

    utils::expected<std::string, SymbolicExpr::GetACSLError> SymbolAddressNode::doGetACSL(
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

    utils::expected<std::string, SymbolicExpr::GetACSLError> VariableAddressNode::doGetACSL(
        const SymbolicExpr::GetACSLConfig &,
        std::unordered_set<SourcePoint> &,
        std::optional<SourcePoint>,
        unsigned parentPrec,
        bool isRightChild) const {
        bool needParens = details::isNeedParens(Operator::AddrOf, parentPrec, isRightChild);
        return (needParens ? "(" : "") + std::string{"&"} + from_->getNameAsString() +
               (needParens ? ")" : "");
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> FieldAddressNode::doGetACSL(
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

    utils::expected<std::string, SymbolicExpr::GetACSLError> SymbolAddressNode::doGetACSLOfValue(
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
            factory.simplifiedBinary(factory.importExpr(getOffset()),
                                     detail::BinaryOpExprNode::Operator::Add,
                                     factory.importExpr(length_->handle()));
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

    utils::expected<std::string, SymbolicExpr::GetACSLError> VariableAddressNode::doGetACSLOfValue(
        const SymbolicExpr::GetACSLConfig &,
        std::unordered_set<SourcePoint> &,
        std::optional<SourcePoint>,
        unsigned,
        bool) const {
        return from_->getNameAsString();
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> FieldAddressNode::doGetACSLOfValue(
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

    namespace {
        struct SymbolOrigin {
            AddrHandle address;
            SourcePoint point;
        };

        std::optional<SymbolOrigin> getBorrowedSymbolOrigin(const Symbol &symbol);

        std::optional<SymbolOrigin> getStructureOrigin(const StructureNode &structure) {
            std::optional<SymbolOrigin> common;
            for (size_t index = 0; index < structure.getNumFields(); ++index) {
                auto *symbol = dyn_cast<const Symbol>(structure.getFieldValue(index).get());
                if (symbol == nullptr)
                    return std::nullopt;

                auto origin = getBorrowedSymbolOrigin(*symbol);
                if (!origin)
                    return std::nullopt;
                auto *fieldAddr = origin->address.dyn_cast<FieldAddressNode>();
                if (fieldAddr == nullptr || fieldAddr->getFieldIndex() != index)
                    return std::nullopt;

                SymbolOrigin fieldOrigin{
                    AddrHandle{fieldAddr->getBaseAddr().get().get()}, origin->point};
                if (!common) {
                    common = fieldOrigin;
                    continue;
                }
                if (*common->address != *fieldOrigin.address ||
                    common->point != fieldOrigin.point)
                    return std::nullopt;
            }
            return common;
        }

        std::optional<SymbolOrigin> getBorrowedSymbolOrigin(const Symbol &symbol) {
            if (auto *value = dyn_cast<const SymbolValueNode>(&symbol))
                return SymbolOrigin{value->getFromAddrHandle(), value->getFromPoint().value()};
            if (auto *address = dyn_cast<const SymbolAddressNode>(&symbol)) {
                auto from = address->getFromAddrHandle();
                if (from)
                    return SymbolOrigin{*from, address->getFromPoint().value()};
                return std::nullopt;
            }
            if (auto *structure = dyn_cast<const StructureNode>(&symbol))
                return getStructureOrigin(*structure);
            return std::nullopt;
        }
    } // namespace

    utils::expected<std::string, SymbolicExpr::GetACSLError> StructureNode::doGetACSL(
        const SymbolicExpr::GetACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned parentPrec,
        bool isRightChild) const {
        auto from = getStructureOrigin(*this);
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
        auto C = evaluateToLiteralNode(*expr_);
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

        auto Lc = evaluateToLiteralNode(*left_);
        if (!Lc)
            return nullptr;

        if (op_ == BO::LogicalAnd) {
            if (!literalAsBool(*Lc))
                return literalNode(ExprFactoryScope::current().literal(false));
            auto Rc = evaluateToLiteralNode(*right_);
            if (!Rc)
                return nullptr;
            return literalNode(ExprFactoryScope::current().literal(literalAsBool(*Rc)));
        }
        if (op_ == BO::LogicalOr) {
            if (literalAsBool(*Lc))
                return literalNode(ExprFactoryScope::current().literal(true));
            auto Rc = evaluateToLiteralNode(*right_);
            if (!Rc)
                return nullptr;
            return literalNode(ExprFactoryScope::current().literal(literalAsBool(*Rc)));
        }

        auto Rc = evaluateToLiteralNode(*right_);
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

    namespace {
        const detail::LiteralExprNode *evaluateToLiteralNode(const SymbolicExpr &expr) {
            if (auto *literal = dyn_cast<const detail::LiteralExprNode>(&expr))
                return literal->evalToConstExpr();
            if (auto *unary = dyn_cast<const detail::UnaryOpExprNode>(&expr))
                return unary->evalToConstExpr();
            if (auto *binary = dyn_cast<const detail::BinaryOpExprNode>(&expr))
                return binary->evalToConstExpr();
            return nullptr;
        }
    } // namespace

    std::optional<int64_t> SymbolicExpr::tryEvalToConstant() const {
        auto *literal = evaluateToLiteralNode(*this);
        if (literal == nullptr)
            return std::nullopt;
        return literal->getLiteralValue();
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
        const auto binary = dyn_cast<const detail::BinaryOpExprNode>(&expr);
        if (!binary)
            return false;
        if (getValType() != expr.getValType())
            return false;

        return *left_ == *(binary->left_) && op_ == binary->op_ && *right_ == *(binary->right_);
    }

    bool detail::UnaryOpExprNode::equal(const SymbolicExpr &expr) const {
        const auto unary = dyn_cast<const detail::UnaryOpExprNode>(&expr);
        if (!unary)
            return false;
        if (getValType() != expr.getValType())
            return false;

        return op_ == unary->op_ && *expr_ == *(unary->expr_);
    }

    bool detail::UnknownExprNode::equal(const SymbolicExpr &expr) const {
        return expr.isUnknown() && getValType() == expr.getValType();
    }

    bool SymbolValueNode::equal(const SymbolicExpr &expr) const {
        const auto symbolValue = dyn_cast<const SymbolValueNode>(&expr);
        if (!symbolValue)
            return false;
        if (getValType() != expr.getValType())
            return false;

        if (fromPoint_ != symbolValue->fromPoint_)
            return false;

        return *fromAddr_ == *symbolValue->fromAddr_;
    }

    bool SymbolAddressNode::equal(const SymbolicExpr &expr) const {
        auto other = dyn_cast<const SymbolAddressNode>(&expr);
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
        auto &factory = ExprFactoryScope::current();
        if (*simplifiedExprHandle(factory, offset_.handle()) !=
            *simplifiedExprHandle(factory, other->getOffset())) {
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

    bool VariableAddressNode::equal(const SymbolicExpr &expr) const {
        auto other = dyn_cast<const VariableAddressNode>(&expr);
        if (!other)
            return false;
        if (getValType() != expr.getValType())
            return false;

        return from_ == other->from_;
    }

    bool FieldAddressNode::equal(const SymbolicExpr &expr) const {
        auto other = dyn_cast<const FieldAddressNode>(&expr);
        if (!other)
            return false;
        if (getValType() != expr.getValType())
            return false;

        return *baseAddr_ == *other->baseAddr_ && fieldIndex_ == other->fieldIndex_;
    }

    std::optional<utils::not_null<const clang::VarDecl *>> SymbolAddressNode::getFromRoot() const {
        if (fromAddr_ == std::nullopt)
            return std::nullopt;
        return fromAddr_.value()->getFromRoot();
    }

    std::optional<utils::not_null<const clang::VarDecl *>> SymbolAddrBaseInfo::getFromRoot() const {
        if (fromAddr_ == std::nullopt)
            return std::nullopt;
        return fromAddr_.value()->getFromRoot();
    }

    std::optional<utils::not_null<const clang::VarDecl *>> VariableAddressNode::getFromRoot() const {
        return from_;
    }

    std::optional<utils::not_null<const clang::VarDecl *>> FieldAddressNode::getFromRoot() const {
        return baseAddr_->getFromRoot();
    }

    std::optional<utils::not_null<const clang::VarDecl *>> SymbolValueNode::getFromRoot() const {
        return fromAddr_->getFromRoot();
    }

    bool StructureInfo::equal(const StructureInfo &other) const {
        if (definition_ != other.definition_)
            return false;
        return true;
    }

    bool StructureInfo::operator==(const StructureInfo &other) const { return equal(other); }

    bool StructureNode::equal(const SymbolicExpr &expr) const {
        const auto st = dyn_cast<const StructureNode>(&expr);
        if (!st)
            return false;
        if (getValType() != expr.getValType())
            return false;
        if (!info_.equal(st->info_))
            return false;
        return std::ranges::equal(fields_, st->fields_,
                                  [](auto &lhs, auto &rhs) { return *lhs == *rhs; });
    }

    SymbolicExpr::UsedMap SymbolValueNode::collectUsedSymbols() const { return {{hash(), this}}; }

    SymbolicExpr::UsedMap detail::BinaryOpExprNode::collectUsedSymbols() const {
        auto lmap = left_->collectUsedSymbols();
        auto rmap = right_->collectUsedSymbols();
        lmap.insert(make_move_iterator(rmap.begin()), make_move_iterator(rmap.end()));
        return lmap;
    }

    SymbolicExpr::UsedMap detail::UnaryOpExprNode::collectUsedSymbols() const {
        return expr_->collectUsedSymbols();
    }

    SymbolicExpr::UsedMap SymbolAddressNode::collectUsedSymbols() const {
        if (length_)
            ERROR("Address range is solely for address representation and should not be "
                  "used as an expression.");
        return {{hash(), this}};
    }

    SymbolAddrBaseInfo::SymbolAddrBaseInfo(const SymbolAddrBaseInfo &other)
        : fromPoint_(other.fromPoint_), pointeeType_(other.pointeeType_) {
        if (other.fromAddr_ == std::nullopt)
            fromAddr_ = std::nullopt;
        else
            fromAddr_.emplace(other.fromAddr_.value());
    }

    SymbolAddressNode::SymbolAddressNode(const clang::QualType pointeeType,
                                 std::optional<AddrHandle> from,
                                 SourcePoint fromPoint,
                                 ExprHandle offset,
                                 std::optional<ExprHandle> length,
                                 std::optional<Type> explicitType)
        : Address(SymbolicExpr::ExprKind::K_SymbolAddress,
                  SymbolicExpr::Type{SymbolicExpr::ScalarKind::UInt, 64},
                  pointeeType, explicitType),
          Symbol(Kind::K_SymbolAddress), offset_(offset), fromAddr_(std::nullopt),
          fromPoint_(std::move(fromPoint)), length_(std::nullopt) {
        if (from)
            fromAddr_.emplace(*from);
        if (length)
            length_.emplace(*length);
    }

    size_t SymbolAddrBaseInfo::hash() const {
        return utils::hash_val(fromPoint_.hash(), fromAddr_ ? fromAddr_.value()->hash() : 0);
    }

    std::optional<ExprHandle> SymbolAddressNode::getRightBound() const {
        // Not sure return which one is better, offset_+1 or nullopt.
        if (length_ == std::nullopt)
            return std::nullopt;
        auto &factory = ExprFactoryScope::current();
        return factory.binary(factory.importExpr(offset_.handle()),
                              detail::BinaryOpExprNode::Operator::Add,
                              factory.importExpr(length_->handle()));
    }

    SymbolAddrBaseInfo SymbolAddressNode::getBaseInfo() const {
        if (fromAddr_ == std::nullopt)
            return SymbolAddrBaseInfo{std::nullopt, fromPoint_, pointeeType_};
        return SymbolAddrBaseInfo{getFromAddrHandle().value(), fromPoint_, pointeeType_};
    }

    int SymbolAddressNode::getDimension() const {
        if (fromAddr_ == std::nullopt)
            return -1;
        if (auto dim = fromAddr_.value()->getDimension(); dim >= 0)
            return dim + 1;
        return -1;
    }

    int VariableAddressNode::getDimension() const { return 0; }

    int FieldAddressNode::getDimension() const { return baseAddr_->getDimension(); }

    StructureNode::StructureNode(StructureInfo info,
                                 std::vector<ExprHandle> fields,
                                 std::optional<Type> explicitType)
        : SymbolicExpr(
              ExprKind::K_Structure,
              Type{ScalarKind::Structure, static_cast<unsigned>(info.layout_.getSize().getQuantity()) *
                                              8 /*By default, char is 8-bit.*/},
              explicitType),
          Symbol(Kind::K_Structure), info_(info) {
        if (fields.size() != info_.layout_.getFieldCount())
            ERROR("Structure field count mismatch");
        fields_.reserve(fields.size());
        for (auto field : fields)
            fields_.emplace_back(field);
    }

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

    bool isValidOffsetOrLength(ExprHandle expr) {
        if (expr->isUnknown())
            return true;
        auto &factory = ExprFactoryScope::current();
        if (tryEvalAsSymbolAddrHandle(factory, expr))
            return false;
        return true;
    }

    bool is_symbol_addr(AddrHandle address) noexcept { return address->isSymbolAddress(); }

    std::optional<SourcePoint> StructureNode::getFromPoint() const {
        auto origin = getStructureOrigin(*this);
        if (!origin)
            return std::nullopt;
        return origin->point;
    }

    std::optional<AddrHandle> getFromAddrHandle(ExprFactory &factory, const Symbol &symbol) {
        auto origin = getBorrowedSymbolOrigin(symbol);
        if (!origin)
            return std::nullopt;
        return factory.importAddress(AddrHandle{origin->address});
    }

    std::optional<AddrHandle> getFromAddrHandle(ExprFactory &factory, ExprHandle symbol) {
        auto *symbolNode = dyn_cast<const Symbol>(symbol.get().get());
        if (symbolNode == nullptr)
            return std::nullopt;
        return getFromAddrHandle(factory, *symbolNode);
    }

    bool isFrom(ExprHandle expr, AddrHandle fromAddr, SourcePoint fromPoint) {
        auto *symbol = dyn_cast<const Symbol>(expr.get().get());
        if (symbol == nullptr)
            return false;

        auto point = symbol->getFromPoint();
        if (!point || *point != fromPoint)
            return false;

        auto origin = getBorrowedSymbolOrigin(*symbol);
        return origin && *fromAddr == *origin->address;
    }

    namespace {
        ExprHandle getSymbolFromAddr(clang::QualType type,
                                     std::optional<Addr> fromAddr,
                                     SourcePoint fromPoint) {
            auto &factory = ExprFactoryScope::current();

            if (type->isPointerType()) {
                auto pointerType = llvm::cast<clang::PointerType>(type);
                auto addr = fromAddr ? Addr::symbol(pointerType->getPointeeType(), *fromAddr,
                                                    std::move(fromPoint))
                                     : Addr::symbol(pointerType->getPointeeType(),
                                                    std::move(fromPoint));
                return addr.asExpr().handle();
            }

            if (type->isArrayType()) {
                auto arrayType = llvm::cast<clang::ArrayType>(type);
                auto addr = fromAddr ? Addr::symbol(arrayType->getElementType(), *fromAddr,
                                                    std::move(fromPoint))
                                     : Addr::symbol(arrayType->getElementType(),
                                                    std::move(fromPoint));
                return addr.asExpr().handle();
            }

            if (type->isStructureType()) {
                if (!fromAddr)
                    ERROR("Structure should *from* an `Address`.");
                auto *RD = type->getAsRecordDecl();
                if (!RD || !RD->isCompleteDefinition())
                    ERROR("Incomplete struct definition");
                RD           = RD->getDefinition();
                auto &layout = RD->getASTContext().getASTRecordLayout(RD);
                return factory.structure(RD, layout, fromAddr->handle(), std::move(fromPoint));
            }

            if (!fromAddr)
                ERROR("SymbolValue should *from* an `Address`.");
            return Expr::symbolValue(deriveType(type), *fromAddr, std::move(fromPoint)).handle();
        }
    } // namespace

    ExprHandle getSymbol(clang::QualType type,
                         std::optional<AddrHandle> from,
                         SourcePoint fromPoint) {
        auto &factory = ExprFactoryScope::current();
        std::optional<Addr> fromAddr;
        if (from)
            fromAddr.emplace(factory, *from);
        return getSymbolFromAddr(type, std::move(fromAddr), std::move(fromPoint));
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
