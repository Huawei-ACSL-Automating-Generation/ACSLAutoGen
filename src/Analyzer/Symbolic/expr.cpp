/**
 * @file expr.cpp
 * @brief Implements symbolic facades and the factory-owned immutable expression DAG.
 */
#include "expr.h"

#include <clang/AST/Type.h>
#include <memory>
#include <optional>
#include <sstream>
#include <cstring>
#include <string_view>
#include <ranges>
#include <llvm/ADT/APInt.h>
#include <llvm/ADT/TypeSwitch.h>
#include <llvm/Support/Casting.h>

#include "aggregateExpr.h" // IWYU pragma: keep
#include "detail/aggregateNodes.h"
#include "detail/exprNodes.h"
#include "detail/exprViews.h"
#include "detail/facadeAccess.h"
#include "detail/factoryInternals.h"
#include "detail/handles.h"
#include "detail/handleInternals.h"
#include "detail/nativeRtti.h"
#include "macros.h"
#include "utils.h"
#include "Analyzer/state.h"

namespace acslg::analyzer::symbolic {
    using detail::AddrHandle;
    using detail::dyn_cast;
    using detail::ExprHandle;
    using detail::ExprHandleIndexMap;
    using detail::ExprHandleSet;
    struct ExprHandleIdentityHash {
        size_t operator()(ExprHandle expression) const { return expression.hash(); }
    };
    using ExprHandleMap = std::unordered_map<ExprHandle, ExprHandle, ExprHandleIdentityHash>;
    using detail::FacadeAccess;
    using detail::FieldAddressNode;
    using detail::isa;
    using detail::MaxMinOverRangeNode;
    using detail::QuantifierOverRangeNode;
    using detail::StructureNode;
    using detail::SumOverRangeNode;
    using detail::Symbol;
    using detail::SymbolAddressNode;
    using detail::SymbolValueNode;
    using detail::VariableAddressNode;

    Addr::Addr(ExprFactory &factory, AddrHandle handle) : factory_(&factory) {
        auto imported = detail::ExprFactoryInternals::importAddress(factory, handle);
        node_         = &detail::HandleAccess::node(imported);
    }

    Addr::Addr(AddrHandle handle) : Addr(ExprFactoryScope::current(), handle) {}

    AddrHandle Addr::handle() const { return detail::HandleAccess::handle(node_); }

    Expr::Expr(ExprFactory &factory, ExprHandle handle) : factory_(&factory) {
        auto imported = detail::ExprFactoryInternals::importExpr(factory, handle);
        node_         = &detail::HandleAccess::node(imported);
    }

    Expr::Expr(ExprHandle handle) : Expr(ExprFactoryScope::current(), handle) {}

    ExprHandle Expr::handle() const { return detail::HandleAccess::handle(node_); }

    const detail::SymbolicExprNode *detail::rawNode(ExprHandle handle) {
        return &HandleAccess::node(handle);
    }

    const detail::AddressNode *detail::rawNode(AddrHandle handle) {
        return &HandleAccess::node(handle);
    }

    Addr Addr::variable(utils::not_null<const clang::VarDecl *> from) {
        return variable(ExprFactoryScope::current(), from);
    }

    Addr Addr::variable(ExprFactory &factory, utils::not_null<const clang::VarDecl *> from) {
        return Addr{factory, detail::ExprFactoryInternals::variableAddress(factory, from)};
    }

    Addr Addr::symbol(ExprFactory &factory, clang::QualType pointeeType, SourcePoint fromPoint) {
        return Addr{factory, detail::ExprFactoryInternals::symbolAddress(factory, pointeeType,
                                                                         std::nullopt, fromPoint)};
    }

    Addr Addr::symbol(clang::QualType pointeeType, SourcePoint fromPoint) {
        return symbol(ExprFactoryScope::current(), pointeeType, fromPoint);
    }

    Addr Addr::symbol(clang::QualType pointeeType, SourcePoint fromPoint, const Expr &offset) {
        auto &factory = offset.factory();
        return Addr{factory, detail::ExprFactoryInternals::symbolAddress(
                                 factory, pointeeType, std::nullopt, fromPoint, offset.handle())};
    }

    Addr Addr::symbol(clang::QualType pointeeType,
                      SourcePoint fromPoint,
                      const Expr &offset,
                      const Expr &length) {
        if (&offset.factory() != &length.factory())
            ERROR("Cannot build address with range expressions from different factories.");
        auto &factory = offset.factory();
        return Addr{factory, detail::ExprFactoryInternals::symbolAddress(
                                 factory, pointeeType, std::nullopt, fromPoint, offset.handle(),
                                 length.handle())};
    }

    Addr Addr::symbol(clang::QualType pointeeType, const Addr &from, SourcePoint fromPoint) {
        auto &factory = from.factory();
        return Addr{factory, detail::ExprFactoryInternals::symbolAddress(factory, pointeeType,
                                                                         from.handle(), fromPoint)};
    }

    Addr Addr::symbol(clang::QualType pointeeType,
                      const Addr &from,
                      SourcePoint fromPoint,
                      const Expr &offset) {
        from.ensureSameFactory(offset);
        auto &factory = from.factory();
        return Addr{factory, detail::ExprFactoryInternals::symbolAddress(
                                 factory, pointeeType, from.handle(), fromPoint, offset.handle())};
    }

    Addr Addr::symbol(clang::QualType pointeeType,
                      const Addr &from,
                      SourcePoint fromPoint,
                      const Expr &offset,
                      const Expr &length) {
        from.ensureSameFactory(offset);
        from.ensureSameFactory(length);
        auto &factory = from.factory();
        return Addr{factory, detail::ExprFactoryInternals::symbolAddress(
                                 factory, pointeeType, from.handle(), fromPoint, offset.handle(),
                                 length.handle())};
    }

    Addr Addr::importedInto(ExprFactory &target) const { return Addr{target, handle()}; }

    std::size_t Addr::hash() const { return handle().hash(); }

    std::string Addr::dump() const { return handle().dump(); }

    ExprType Addr::getValType() const { return handle().getValType(); }

    bool Addr::isSymbolAddress() const { return handle().isSymbolAddress(); }

    bool Addr::isVariableAddress() const { return handle().isVariableAddress(); }

    bool Addr::isFieldAddress() const { return handle().isFieldAddress(); }

    bool Addr::structurallyEqual(const Addr &other) const {
        return handle().structurallyEqual(other.handle());
    }

    std::optional<utils::not_null<const clang::VarDecl *>> Addr::getFromRoot() const {
        return handle().getFromRoot();
    }

    int Addr::getDimension() const { return handle().getDimension(); }

    const clang::QualType &Addr::pointeeType() const { return handle().getPointeeType(); }

    utils::expected<std::pair<std::string, std::unordered_set<SourcePoint>>, ACSLError> Addr::getACSL(
        const ACSLConfig &config,
        std::optional<SourcePoint> currentPoint) const {
        return handle().asExpr().getACSL(config, currentPoint);
    }

    utils::expected<std::pair<std::string, std::unordered_set<SourcePoint>>, ACSLError> Addr::
        getACSLOfValue(const ACSLConfig &config, std::optional<SourcePoint> currentPoint) const {
        return handle().getACSLOfValue(config, currentPoint);
    }

    Expr Addr::asExpr() const { return Expr{factory(), handle().asExpr()}; }

    Addr Addr::withOffset(const Expr &offset) const {
        ensureSameFactory(offset);
        return Addr{factory(),
                    detail::ExprFactoryInternals::withOffset(factory(), handle(), offset.handle())};
    }

    Addr Addr::withAddedOffset(const Expr &extra) const {
        ensureSameFactory(extra);
        return Addr{factory(), detail::ExprFactoryInternals::withAddedOffset(factory(), handle(),
                                                                             extra.handle())};
    }

    Addr Addr::withSubtractedOffset(const Expr &extra) const {
        ensureSameFactory(extra);
        return Addr{factory(), detail::ExprFactoryInternals::withSubtractedOffset(
                                   factory(), handle(), extra.handle())};
    }

    Addr Addr::withLength(const Expr &length) const {
        ensureSameFactory(length);
        return Addr{factory(),
                    detail::ExprFactoryInternals::withLength(factory(), handle(), length.handle())};
    }

    Addr Addr::withAddedLength(const Expr &extra) const {
        ensureSameFactory(extra);
        return Addr{factory(), detail::ExprFactoryInternals::withAddedLength(factory(), handle(),
                                                                             extra.handle())};
    }

    Addr Addr::withoutLength() const {
        return Addr{factory(), detail::ExprFactoryInternals::withoutLength(factory(), handle())};
    }

    Addr Addr::field(clang::QualType pointeeType,
                     const clang::RecordDecl *record,
                     size_t fieldIndex) const {
        return Addr{factory(), detail::ExprFactoryInternals::fieldAddress(
                                   factory(), pointeeType, record, handle(), fieldIndex)};
    }

    Expr Expr::unknown() { return unknown(ExprFactoryScope::current()); }

    Expr Expr::unknown(ExprFactory &factory) {
        return Expr{factory, detail::ExprFactoryInternals::unknown(factory)};
    }

    Expr Expr::rangeIndex(std::string_view name) {
        return rangeIndex(ExprFactoryScope::current(), name);
    }

    Expr Expr::rangeIndex(ExprFactory &factory, std::string_view name) {
        return Expr{factory, detail::ExprFactoryInternals::rangeIndex(factory, name)};
    }

    Expr Expr::symbolValue(ExprType varType, const Addr &from, SourcePoint fromPoint) {
        auto &factory = from.factory();
        return Expr{factory, detail::ExprFactoryInternals::symbolValue(factory, varType,
                                                                       from.handle(), fromPoint)};
    }

    Expr Expr::importedInto(ExprFactory &target) const { return Expr{target, handle()}; }

    std::size_t Expr::hash() const { return handle().hash(); }

    std::string Expr::dump() const { return handle().dump(); }

    ExprType Expr::getValType() const { return handle().getValType(); }

    bool Expr::isUnknown() const { return handle().isUnknown(); }

    bool Expr::isCast() const { return handle().isCastExpr(); }

    bool Expr::isRangeIndex() const { return handle().isRangeIndex(); }

    bool Expr::isSymbolValue() const { return handle().isSymbolValue(); }

    bool Expr::isStructure() const { return handle().isStructure(); }

    bool Expr::isOverRange() const { return handle().isOverRange(); }

    int Expr::getMaxDegree() const { return handle().getMaxDegree(); }

    std::size_t ExprIdentityHash::operator()(const Expr &expression) const {
        return expression.hash();
    }

    ExprSet Expr::collectUsedSymbols() const {
        ExprSet symbols;
        for (const auto &expression : handle().collectUsedSymbols())
            symbols.emplace(Expr{factory(), expression});
        return symbols;
    }

    std::optional<int64_t> Expr::tryEvalAsConstant() const { return handle().tryEvalAsConstant(); }

    std::optional<Parma_Polyhedra_Library::Linear_Expression> Expr::toLinearExpr(
        const std::unordered_map<std::string, size_t> &varIndexMap) const {
        return handle().toLinearExpr(varIndexMap);
    }

    Parma_Polyhedra_Library::Linear_Expression Expr::toLinearExpr(
        const ExprIndexMap &expressionIndexMap) const {
        ExprHandleIndexMap handleIndexMap;
        handleIndexMap.reserve(expressionIndexMap.size());
        for (const auto &[expression, index] : expressionIndexMap)
            handleIndexMap.emplace(FacadeAccess::exprHandle(expression), index);
        return handle().toLinearExpr(handleIndexMap);
    }

    bool Expr::structurallyEqual(const Expr &other) const {
        return handle().structurallyEqual(other.handle());
    }

    utils::expected<std::pair<std::string, std::unordered_set<SourcePoint>>, ACSLError> Expr::getACSL(
        const ACSLConfig &config,
        std::optional<SourcePoint> currentPoint) const {
        return handle().getACSL(config, currentPoint);
    }

    Expr Expr::withType(ExprType newType) const {
        return Expr{factory(),
                    detail::ExprFactoryInternals::withValType(factory(), handle(), newType)};
    }

    Expr Expr::castTo(ExprType targetType) const {
        return Expr{factory(), detail::ExprFactoryInternals::cast(factory(), handle(), targetType)};
    }

    Expr Expr::withField(size_t index, const Expr &value) const {
        ensureSameFactory(value);
        return Expr{factory(), detail::ExprFactoryInternals::withField(factory(), handle(), index,
                                                                       value.handle())};
    }

    Expr Expr::binary(BinaryOp op, const Expr &rhs) const {
        ensureSameFactory(rhs);
        return Expr{factory(),
                    detail::ExprFactoryInternals::binary(factory(), handle(), op, rhs.handle())};
    }

    Expr Expr::unary(UnaryOp op) const {
        return Expr{factory(), detail::ExprFactoryInternals::unary(factory(), op, handle())};
    }

    namespace {
        template <typename... Exprs>
        auto collectUsedSymbolHandles(ExprHandle first, Exprs... rest) {
            ExprHandleSet merged;

            auto mergeIntoOne = [&](const ExprHandleSet &symbols) {
                merged.insert(symbols.begin(), symbols.end());
            };

            mergeIntoOne(first.collectUsedSymbols());
            (mergeIntoOne(rest.collectUsedSymbols()), ...);

            size_t index = 0;
            ExprHandleIndexMap expressionIndexMap;
            for (const auto &expression : merged)
                expressionIndexMap.emplace(expression, index++);
            return std::pair{std::move(merged), std::move(expressionIndexMap)};
        }

        ExprHandle simplifyHandle(ExprFactory &factory, ExprHandle expr);
        std::optional<AddrHandle> evaluateAddressHandle(ExprFactory &factory, ExprHandle expr);
        ExprHandle stripSizeofFactorHandle(ExprFactory &factory,
                                           ExprHandle expression,
                                           std::uint64_t sizeofBytes);

        const detail::AddressNode *nodePointer(const std::optional<AddrHandle> &handle) {
            return handle ? &detail::HandleAccess::node(*handle) : nullptr;
        }

        const detail::SymbolicExprNode *nodePointer(const std::optional<ExprHandle> &handle) {
            return handle ? &detail::HandleAccess::node(*handle) : nullptr;
        }
    } // namespace

    static ExprHandle substituteValuesHandle(ExprFactory &factory,
                                             ExprHandle expr,
                                             const ExprHandleMap &substitutions);
    static ExprHandle substitutePathHandle(ExprFactory &factory,
                                           ExprHandle expr,
                                           const Path &path,
                                           const SourcePoint &point);
    static ExprHandle substituteRangeIndexHandle(ExprFactory &factory,
                                                 ExprHandle expr,
                                                 const SymbolAddrBaseInfo &rangeBase,
                                                 ExprHandle index);

    struct detail::ExprFactoryBackend {
        explicit ExprFactoryBackend(ExprFactory &factory) : owner(factory) {}

        size_t size() const { return owned.size(); }
        ExprHandle importExpr(ExprHandle expr);
        AddrHandle importAddress(AddrHandle address);
        ExprHandle literal(bool value);
        ExprHandle literal(int value);
        ExprHandle literal(unsigned int value);
        ExprHandle literal(short value);
        ExprHandle literal(unsigned short value);
        ExprHandle literal(int64_t value);
        ExprHandle literal(uint64_t value);
        ExprHandle unknown();
        ExprHandle rangeIndex(std::string_view name);
        AddrHandle variableAddress(utils::not_null<const clang::VarDecl *> from);
        AddrHandle fieldAddress(clang::QualType pointeeType,
                                const clang::RecordDecl *record,
                                AddrHandle baseAddr,
                                size_t fieldIndex);
        AddrHandle symbolAddress(clang::QualType pointeeType,
                                 const AddressNode *from,
                                 SourcePoint fromPoint,
                                 const SymbolicExprNode *offset,
                                 const SymbolicExprNode *length);
        AddrHandle withOffset(AddrHandle address, ExprHandle offset);
        AddrHandle withAddedOffset(AddrHandle address, ExprHandle extra);
        AddrHandle withSubtractedOffset(AddrHandle address, ExprHandle extra);
        AddrHandle withLength(AddrHandle address, ExprHandle length);
        AddrHandle withAddedLength(AddrHandle address, ExprHandle extra);
        AddrHandle withoutLength(AddrHandle address);
        ExprHandle symbolValue(ExprType varType, AddrHandle from, SourcePoint fromPoint);
        ExprHandle structure(const clang::RecordDecl *record,
                             const clang::ASTRecordLayout &layout,
                             AddrHandle from,
                             SourcePoint fromPoint);
        ExprHandle withField(ExprHandle structure, size_t index, ExprHandle value);
        ExprHandle unary(UnaryOp op, ExprHandle expr);
        ExprHandle cast(ExprHandle expr, ExprType targetType);
        ExprHandle binary(ExprHandle left, BinaryOp op, ExprHandle right);
        ExprHandle simplifiedBinary(ExprHandle left, BinaryOp op, ExprHandle right);
        ExprHandle withValType(ExprHandle expr, ExprType newType);
        ExprHandle intern(utils::not_null<std::unique_ptr<SymbolicExprNode>> node);
        AddrHandle internAddress(utils::not_null<std::unique_ptr<AddressNode>> node);
        ExprHandle importNode(const SymbolicExprNode &expr);
        AddrHandle importAddressNode(const AddressNode &address);

        template <typename Node, typename... Args>
        static std::unique_ptr<Node> makeNode(Args &&...args) {
            return ExprFactoryInternals::makeNode<Node>(std::forward<Args>(args)...);
        }

        ExprFactory &owner;
        std::vector<std::unique_ptr<const SymbolicExprNode>> owned;
        std::unordered_map<size_t, std::vector<const SymbolicExprNode *>> interned;
    };

    ExprFactory::ExprFactory() : backend_(std::make_unique<detail::ExprFactoryBackend>(*this)) {}
    ExprFactory::~ExprFactory() = default;

    size_t detail::ExprFactoryInternals::size(const ExprFactory &factory) {
        return factory.backend_->size();
    }

    ExprHandle detail::ExprFactoryInternals::importExpr(ExprFactory &factory,
                                                        ExprHandle expression) {
        return factory.backend_->importExpr(expression);
    }

    AddrHandle detail::ExprFactoryInternals::importAddress(ExprFactory &factory,
                                                           AddrHandle address) {
        return factory.backend_->importAddress(address);
    }

    ExprHandle detail::ExprFactoryInternals::literal(ExprFactory &factory, bool value) {
        return factory.backend_->literal(value);
    }

    ExprHandle detail::ExprFactoryInternals::literal(ExprFactory &factory, int value) {
        return factory.backend_->literal(value);
    }

    ExprHandle detail::ExprFactoryInternals::literal(ExprFactory &factory, unsigned int value) {
        return factory.backend_->literal(value);
    }

    ExprHandle detail::ExprFactoryInternals::literal(ExprFactory &factory, short value) {
        return factory.backend_->literal(value);
    }

    ExprHandle detail::ExprFactoryInternals::literal(ExprFactory &factory, unsigned short value) {
        return factory.backend_->literal(value);
    }

    ExprHandle detail::ExprFactoryInternals::literal(ExprFactory &factory, int64_t value) {
        return factory.backend_->literal(value);
    }

    ExprHandle detail::ExprFactoryInternals::literal(ExprFactory &factory, uint64_t value) {
        return factory.backend_->literal(value);
    }

    ExprHandle detail::ExprFactoryInternals::unknown(ExprFactory &factory) {
        return factory.backend_->unknown();
    }

    ExprHandle detail::ExprFactoryInternals::rangeIndex(ExprFactory &factory,
                                                        std::string_view name) {
        return factory.backend_->rangeIndex(name);
    }

    AddrHandle detail::ExprFactoryInternals::variableAddress(
        ExprFactory &factory,
        utils::not_null<const clang::VarDecl *> variable) {
        return factory.backend_->variableAddress(variable);
    }

    AddrHandle detail::ExprFactoryInternals::fieldAddress(ExprFactory &factory,
                                                          clang::QualType pointeeType,
                                                          const clang::RecordDecl *record,
                                                          AddrHandle base,
                                                          size_t fieldIndex) {
        return factory.backend_->fieldAddress(pointeeType, record, base, fieldIndex);
    }

    AddrHandle detail::ExprFactoryInternals::symbolAddress(ExprFactory &factory,
                                                           clang::QualType pointeeType,
                                                           std::optional<AddrHandle> from,
                                                           SourcePoint fromPoint,
                                                           std::optional<ExprHandle> offset,
                                                           std::optional<ExprHandle> length) {
        return factory.backend_->symbolAddress(
            pointeeType, from ? rawNode(*from) : nullptr, std::move(fromPoint),
            offset ? rawNode(*offset) : nullptr, length ? rawNode(*length) : nullptr);
    }

    AddrHandle detail::ExprFactoryInternals::withOffset(ExprFactory &factory,
                                                        AddrHandle address,
                                                        ExprHandle offset) {
        return factory.backend_->withOffset(address, offset);
    }

    AddrHandle detail::ExprFactoryInternals::withAddedOffset(ExprFactory &factory,
                                                             AddrHandle address,
                                                             ExprHandle extra) {
        return factory.backend_->withAddedOffset(address, extra);
    }

    AddrHandle detail::ExprFactoryInternals::withSubtractedOffset(ExprFactory &factory,
                                                                  AddrHandle address,
                                                                  ExprHandle extra) {
        return factory.backend_->withSubtractedOffset(address, extra);
    }

    AddrHandle detail::ExprFactoryInternals::withLength(ExprFactory &factory,
                                                        AddrHandle address,
                                                        ExprHandle length) {
        return factory.backend_->withLength(address, length);
    }

    AddrHandle detail::ExprFactoryInternals::withAddedLength(ExprFactory &factory,
                                                             AddrHandle address,
                                                             ExprHandle extra) {
        return factory.backend_->withAddedLength(address, extra);
    }

    AddrHandle detail::ExprFactoryInternals::withoutLength(ExprFactory &factory,
                                                           AddrHandle address) {
        return factory.backend_->withoutLength(address);
    }

    ExprHandle detail::ExprFactoryInternals::symbolValue(ExprFactory &factory,
                                                         ExprType type,
                                                         AddrHandle from,
                                                         SourcePoint fromPoint) {
        return factory.backend_->symbolValue(type, from, std::move(fromPoint));
    }

    ExprHandle detail::ExprFactoryInternals::structure(ExprFactory &factory,
                                                       const clang::RecordDecl *record,
                                                       const clang::ASTRecordLayout &layout,
                                                       AddrHandle from,
                                                       SourcePoint fromPoint) {
        return factory.backend_->structure(record, layout, from, std::move(fromPoint));
    }

    ExprHandle detail::ExprFactoryInternals::withField(ExprFactory &factory,
                                                       ExprHandle structure,
                                                       size_t index,
                                                       ExprHandle value) {
        return factory.backend_->withField(structure, index, value);
    }

    ExprHandle detail::ExprFactoryInternals::unary(ExprFactory &factory,
                                                   UnaryOp op,
                                                   ExprHandle expression) {
        return factory.backend_->unary(op, expression);
    }

    ExprHandle detail::ExprFactoryInternals::cast(ExprFactory &factory,
                                                  ExprHandle expression,
                                                  ExprType targetType) {
        return factory.backend_->cast(expression, targetType);
    }

    ExprHandle detail::ExprFactoryInternals::binary(ExprFactory &factory,
                                                    ExprHandle left,
                                                    BinaryOp op,
                                                    ExprHandle right) {
        return factory.backend_->binary(left, op, right);
    }

    ExprHandle detail::ExprFactoryInternals::simplifiedBinary(ExprFactory &factory,
                                                              ExprHandle left,
                                                              BinaryOp op,
                                                              ExprHandle right) {
        return factory.backend_->simplifiedBinary(left, op, right);
    }

    ExprHandle detail::ExprFactoryInternals::withValType(ExprFactory &factory,
                                                         ExprHandle expression,
                                                         ExprType type) {
        return factory.backend_->withValType(expression, type);
    }

    ExprHandle detail::ExprFactoryInternals::intern(
        ExprFactory &factory,
        utils::not_null<std::unique_ptr<SymbolicExprNode>> node) {
        return factory.backend_->intern(std::move(node));
    }

    namespace {
        ExprHandle stripSizeofFactorHandle(ExprFactory &factory,
                                           ExprHandle expression,
                                           std::uint64_t sizeofBytes) {
            if (auto literal = detail::LiteralExprView::tryFrom(expression)) {
                const auto value = static_cast<std::uint64_t>(literal->value());
                return value == sizeofBytes
                           ? detail::ExprFactoryInternals::literal(factory, std::uint64_t{1})
                           : expression;
            }

            if (auto binary = detail::BinaryExprView::tryFrom(expression);
                binary && binary->operation() == BinaryOp::Multiply) {
                auto left  = binary->left();
                auto right = binary->right();
                if (auto literal = detail::LiteralExprView::tryFrom(left);
                    literal && static_cast<std::uint64_t>(literal->value()) == sizeofBytes)
                    return detail::ExprFactoryInternals::importExpr(factory, right);
                if (auto literal = detail::LiteralExprView::tryFrom(right);
                    literal && static_cast<std::uint64_t>(literal->value()) == sizeofBytes)
                    return detail::ExprFactoryInternals::importExpr(factory, left);
            }
            return expression;
        }
    } // namespace

    Expr strip_sizeof_factor(const Expr &expression, std::uint64_t sizeofBytes) {
        auto &factory = expression.factory();
        return FacadeAccess::makeExpr(
            factory,
            stripSizeofFactorHandle(factory, FacadeAccess::exprHandle(expression), sizeofBytes));
    }

    ExprHandle detail::SymbolicExprNode::selfHandle() const { return ExprHandle{this}; }

    Expr Expr::simplified() const { return Expr{factory(), simplifyHandle(factory(), handle())}; }

    std::optional<Addr> Expr::tryAsAddress() const {
        auto address = detail::AddrHandle::tryFrom(handle());
        if (!address)
            return std::nullopt;
        return Addr{factory(), *address};
    }

    std::optional<Addr> Expr::evaluatedAddress() const {
        auto address = evaluateAddressHandle(factory(), handle());
        if (!address)
            return std::nullopt;
        return Addr{factory(), *address};
    }

    Expr Expr::structure(const clang::RecordDecl *record, const Addr &base, SourcePoint fromPoint) {
        if (!record || !record->isCompleteDefinition())
            ERROR("Expected complete structure type.");
        record        = record->getDefinition();
        auto &factory = base.factory();
        return Expr{factory,
                    detail::ExprFactoryInternals::structure(
                        factory, record, record->getASTContext().getASTRecordLayout(record),
                        FacadeAccess::addressHandle(base), fromPoint)};
    }

    Expr Expr::substituteValues(const ExprSubstitutions &substitutions) const {
        ExprHandleMap handles;
        handles.reserve(substitutions.substitutions_.size());
        for (const auto &[source, replacement] : substitutions.substitutions_) {
            auto importedSource = detail::ExprFactoryInternals::importExpr(
                factory(), FacadeAccess::exprHandle(source));
            handles.insert_or_assign(importedSource, FacadeAccess::exprHandle(replacement));
        }
        return Expr{factory(), substituteValuesHandle(factory(), handle(), handles)};
    }

    Expr Expr::substitutePath(const Path &path, const SourcePoint &point) const {
        return Expr{factory(), substitutePathHandle(factory(), handle(), path, point)};
    }

    Expr Expr::substituteRangeIndex(const SymbolAddrBaseInfo &rangeBase, const Expr &index) const {
        ensureSameFactory(index);
        return Expr{factory(),
                    substituteRangeIndexHandle(factory(), handle(), rangeBase, index.handle())};
    }

    std::size_t detail::ExprHandle::hash() const { return expr_->hash(); }

    std::string detail::ExprHandle::dump() const { return expr_->dump(); }

    ExprType detail::ExprHandle::getValType() const { return expr_->getValType(); }

    bool detail::ExprHandle::structurallyEqual(ExprHandle other) const {
        return expr_->equal(*other.expr_);
    }

    bool detail::ExprHandle::isUnknown() const { return expr_->isUnknown(); }
    bool detail::ExprHandle::isLiteralExpr() const { return expr_->isLiteralExpr(); }
    bool detail::ExprHandle::isUnaryExpr() const { return expr_->isUnaryExpr(); }
    bool detail::ExprHandle::isBinaryExpr() const { return expr_->isBinaryExpr(); }
    bool detail::ExprHandle::isCastExpr() const { return expr_->isCastExpr(); }
    bool detail::ExprHandle::isStructure() const { return expr_->isStructure(); }
    bool detail::ExprHandle::isSymbolValue() const { return expr_->isSymbolValue(); }
    bool detail::ExprHandle::isSymbolAddress() const { return expr_->isSymbolAddress(); }
    bool detail::ExprHandle::isVariableAddress() const { return expr_->isVariableAddress(); }
    bool detail::ExprHandle::isFieldAddress() const { return expr_->isFieldAddress(); }
    bool detail::ExprHandle::isRangeIndex() const { return expr_->isRangeIndex(); }
    bool detail::ExprHandle::isSumOverRange() const { return expr_->isSumOverRange(); }
    bool detail::ExprHandle::isQuantifierOverRange() const {
        return expr_->isQuantifierOverRange();
    }
    bool detail::ExprHandle::isMaxMinOverRange() const { return expr_->isMaxMinOverRange(); }
    bool detail::ExprHandle::isOverRange() const { return expr_->isOverRange(); }
    bool detail::ExprHandle::isLinear() const { return expr_->isLinear(); }

    int detail::ExprHandle::getMaxDegree() const { return expr_->getMaxDegree(); }

    ExprHandleSet detail::ExprHandle::collectUsedSymbols() const {
        return expr_->collectUsedSymbols();
    }

    std::optional<int64_t> detail::ExprHandle::tryEvalAsConstant() const {
        return expr_->tryEvalAsConstant();
    }

    std::optional<int64_t> detail::SymbolicExprNode::tryEvalAsConstant() const {
        // TODO: cache the result.
        if (!isLinear())
            return std::nullopt;
        auto [usedSymbols, expressionIndexMap] = collectUsedSymbolHandles(selfHandle());
        (void)usedSymbols;
        auto linearExpr = toLinearExpr(expressionIndexMap);
        if (linearExpr.all_homogeneous_terms_are_zero())
            return linearExpr.inhomogeneous_term().get_si();
        return std::nullopt;
    }

    std::optional<int64_t> detail::ExprHandle::tryEvalToConstant() const {
        return expr_->tryEvalToConstant();
    }

    std::optional<Parma_Polyhedra_Library::Linear_Expression> detail::ExprHandle::toLinearExpr(
        const std::unordered_map<std::string, size_t> &varIndexMap) const {
        return expr_->toLinearExpr(varIndexMap);
    }

    Parma_Polyhedra_Library::Linear_Expression detail::ExprHandle::toLinearExpr(
        const ExprHandleIndexMap &expressionIndexMap) const {
        return expr_->toLinearExpr(expressionIndexMap);
    }

    utils::expected<std::pair<std::string, std::unordered_set<SourcePoint>>, ACSLError> detail::
        ExprHandle::getACSL(const ACSLConfig &config,
                            std::optional<SourcePoint> currentPoint) const {
        return expr_->getACSL(config, currentPoint);
    }

    AddressBox::AddressBox(AddrHandle handle) noexcept : ptr_(handle.ptr_) {}

    AddressBox::AddressBox(const Addr &address) noexcept
        : AddressBox(FacadeAccess::addressHandle(address)) {}

    AddrHandle AddressBox::handle() const { return AddrHandle{ptr_}; }

    Addr AddressBox::importedInto(ExprFactory &target) const {
        return FacadeAccess::makeAddress(target, handle());
    }

    std::string AddressBox::dump() const { return handle().dump(); }
    bool AddressBox::isSymbolAddress() const { return handle().isSymbolAddress(); }
    bool AddressBox::isVariableAddress() const { return handle().isVariableAddress(); }
    bool AddressBox::isFieldAddress() const { return handle().isFieldAddress(); }

    std::optional<utils::not_null<const clang::VarDecl *>> AddressBox::getFromRoot() const {
        return handle().getFromRoot();
    }

    utils::expected<std::pair<std::string, std::unordered_set<SourcePoint>>, ACSLError> AddressBox::
        getACSLOfValue(const ACSLConfig &config, std::optional<SourcePoint> currentPoint) const {
        return handle().getACSLOfValue(config, currentPoint);
    }

    bool operator==(const AddressBox &a, const AddressBox &b) {
        return a.handle().structurallyEqual(b.handle());
    }

    std::size_t AddressBox::hash() const noexcept { return handle().hash(); }

    std::optional<AddrHandle> detail::AddrHandle::tryFrom(ExprHandle handle) {
        if (auto *address = detail::HandleAccess::dynCast<const detail::AddressNode>(handle))
            return AddrHandle{address};
        return std::nullopt;
    }

    ExprHandle detail::AddrHandle::asExpr() const { return ExprHandle{ptr_}; }

    std::size_t detail::AddrHandle::hash() const { return ptr_->hash(); }
    std::string detail::AddrHandle::dump() const { return ptr_->dump(); }
    ExprType detail::AddrHandle::getValType() const { return ptr_->getValType(); }

    bool detail::AddrHandle::structurallyEqual(AddrHandle other) const {
        return ptr_->equal(*other.ptr_);
    }

    bool detail::AddrHandle::isSymbolAddress() const { return ptr_->isSymbolAddress(); }
    bool detail::AddrHandle::isVariableAddress() const { return ptr_->isVariableAddress(); }
    bool detail::AddrHandle::isFieldAddress() const { return ptr_->isFieldAddress(); }

    std::optional<utils::not_null<const clang::VarDecl *>> detail::AddrHandle::getFromRoot() const {
        return ptr_->getFromRoot();
    }

    int detail::AddrHandle::getDimension() const { return ptr_->getDimension(); }

    const clang::QualType &detail::AddrHandle::getPointeeType() const {
        return ptr_->getPointeeType();
    }

    utils::expected<std::pair<std::string, std::unordered_set<SourcePoint>>, ACSLError> detail::
        AddrHandle::getACSLOfValue(const ACSLConfig &config,
                                   std::optional<SourcePoint> currentPoint) const {
        return ptr_->getACSLOfValue(config, currentPoint);
    }

    thread_local ExprFactory *ExprFactoryScope::current_ = nullptr;

    ExprFactoryScope::ExprFactoryScope(ExprFactory &factory) : previous_(current_) {
        current_ = &factory;
    }

    ExprFactoryScope::~ExprFactoryScope() { current_ = previous_; }

    ExprFactory &ExprFactoryScope::current() {
        if (current_ == nullptr)
            ERROR("No active ExprFactory.");
        return *current_;
    }

    bool ExprFactoryScope::hasCurrent() { return current_ != nullptr; }

    ExprHandle detail::ExprFactoryBackend::intern(
        utils::not_null<std::unique_ptr<SymbolicExprNode>> node) {
        const auto hash = node->hash();
        auto &bucket    = interned[hash];

        ExprFactoryScope scope(owner);
        for (const auto *existing : bucket) {
            if (*existing == *node)
                return HandleAccess::handle(existing);
        }

        auto *raw = node.get().get();
        owned.push_back(std::move(node).into_underlying());
        bucket.push_back(raw);
        return HandleAccess::handle(raw);
    }

    AddrHandle detail::ExprFactoryBackend::internAddress(
        utils::not_null<std::unique_ptr<AddressNode>> node) {
        std::unique_ptr<SymbolicExprNode> exprNode = std::move(node).into_underlying();
        auto handle =
            intern(utils::not_null<std::unique_ptr<detail::SymbolicExprNode>>{std::move(exprNode)});
        auto address = detail::AddrHandle::tryFrom(handle);
        if (!address)
            ERROR("ExprFactory::internAddress produced a non-address node.");
        return *address;
    }

    ExprHandle detail::ExprFactoryBackend::withValType(ExprHandle expr, ExprType newType) {
        if (expr.getValType() == newType)
            return expr;

        auto internTyped = [this](auto node) {
            std::unique_ptr<detail::SymbolicExprNode> base = std::move(node);
            return intern(
                utils::not_null<std::unique_ptr<detail::SymbolicExprNode>>{std::move(base)});
        };

        if (auto *literal = detail::HandleAccess::dynCast<const detail::LiteralExprNode>(expr))
            return internTyped(literal->rebuildNode(newType));

        if (detail::HandleAccess::isa<detail::UnknownExprNode>(expr))
            return internTyped(makeNode<detail::UnknownExprNode>(newType));

        if (auto *index = detail::HandleAccess::dynCast<const detail::RangeIndexNode>(expr))
            return internTyped(detail::ExprFactoryInternals::makeNode<detail::RangeIndexNode>(
                index->getName(), newType));

        if (auto *unaryExpr = detail::HandleAccess::dynCast<const detail::UnaryOpExprNode>(expr))
            return internTyped(makeNode<detail::UnaryOpExprNode>(
                unaryExpr->getOperator(), importExpr(unaryExpr->getSub()), newType));

        if (auto *castExpr = detail::HandleAccess::dynCast<const detail::CastExprNode>(expr))
            return cast(importExpr(castExpr->getOperand()), newType);

        if (auto *binaryExpr = detail::HandleAccess::dynCast<const detail::BinaryOpExprNode>(expr))
            return internTyped(makeNode<detail::BinaryOpExprNode>(
                importExpr(binaryExpr->getLeft()), binaryExpr->getOperator(),
                importExpr(binaryExpr->getRight()), newType));

        if (auto *variableAddr = detail::HandleAccess::dynCast<const VariableAddressNode>(expr))
            return internTyped(detail::ExprFactoryInternals::makeNode<VariableAddressNode>(
                variableAddr->getFrom(), newType));

        if (auto *fieldAddr = detail::HandleAccess::dynCast<const FieldAddressNode>(expr))
            return internTyped(detail::ExprFactoryInternals::makeNode<FieldAddressNode>(
                fieldAddr->getPointeeType(), fieldAddr->getDefinition(),
                importAddress(fieldAddr->getBaseAddr()), fieldAddr->getFieldIndex(), newType));

        if (auto *symbolAddr = detail::HandleAccess::dynCast<const SymbolAddressNode>(expr)) {
            std::optional<AddrHandle> from;
            if (auto existingFrom = symbolAddr->getFromAddrHandle())
                from = importAddress(*existingFrom);

            std::optional<ExprHandle> length;
            if (const auto &existingLength = symbolAddr->getLength(); existingLength)
                length = importExpr(*existingLength);

            return internTyped(detail::ExprFactoryInternals::makeNode<SymbolAddressNode>(
                symbolAddr->getPointeeType(), from, symbolAddr->getFromPoint().value(),
                importExpr(symbolAddr->getOffset()), length, newType));
        }

        if (auto *symbolVal = detail::HandleAccess::dynCast<const SymbolValueNode>(expr))
            return internTyped(detail::ExprFactoryInternals::makeNode<SymbolValueNode>(
                newType, importAddress(symbolVal->getFromAddrHandle()),
                symbolVal->getFromPoint().value()));

        if (auto *structure = detail::HandleAccess::dynCast<const StructureNode>(expr)) {
            std::vector<ExprHandle> fields;
            fields.reserve(structure->getNumFields());
            for (auto field : structure->fieldsValues())
                fields.push_back(importExpr(field));
            return internTyped(detail::ExprFactoryInternals::makeNode<StructureNode>(
                structure->getInfo(), std::move(fields), newType));
        }

        if (expr.isSumOverRange()) {
            SumOverRangeExpr sum{FacadeAccess::makeExpr(owner, expr)};
            return internTyped(detail::ExprFactoryInternals::makeNode<SumOverRangeNode>(
                FacadeAccess::addressHandle(sum.range()), sum.indexName(), sum.fromPoint(),
                newType));
        }

        if (expr.isQuantifierOverRange()) {
            QuantifierOverRangeExpr quantifier{FacadeAccess::makeExpr(owner, expr)};
            return internTyped(detail::ExprFactoryInternals::makeNode<QuantifierOverRangeNode>(
                FacadeAccess::addressHandle(quantifier.range()), quantifier.indexName(),
                quantifier.quantifier(), FacadeAccess::exprHandle(quantifier.predicate()),
                newType));
        }

        if (expr.isMaxMinOverRange()) {
            MaxMinOverRangeExpr maxMin{FacadeAccess::makeExpr(owner, expr)};
            return internTyped(detail::ExprFactoryInternals::makeNode<MaxMinOverRangeNode>(
                FacadeAccess::addressHandle(maxMin.range()), maxMin.indexName(), maxMin.extremum(),
                FacadeAccess::exprHandle(maxMin.body()), maxMin.fromPoint(), newType));
        }

        ERROR("Unsupported symbolic expression type in ExprFactory::withValType: " + expr.dump());
    }

    static ExprHandle substituteValuesHandle(ExprFactory &factory,
                                             ExprHandle expr,
                                             const ExprHandleMap &substitutions) {
        ExprFactoryScope scope(factory);

        struct Substituter {
            ExprFactory &factory;
            const ExprHandleMap &substitutions;

            AddrHandle requireAddress(ExprHandle handle) const {
                if (auto address = detail::AddrHandle::tryFrom(handle))
                    return detail::ExprFactoryInternals::importAddress(factory, *address);
                UNREACHABLE();
            }

            SymbolAddress requireRange(ExprHandle handle) const {
                auto address = FacadeAccess::makeExpr(factory, handle).tryAsAddress();
                if (address) {
                    auto range = SymbolAddress::tryFrom(*address);
                    if (range && range->length())
                        return *range;
                }
                ERROR("Substituted expression should be a *range*");
            }

            ExprHandle run(ExprHandle expr) const {
                if (auto it = substitutions.find(expr); it != substitutions.end())
                    return detail::ExprFactoryInternals::importExpr(factory, it->second);

                if (auto *literal =
                        detail::HandleAccess::dynCast<const detail::LiteralExprNode>(expr))
                    return literal->importInto(factory);
                if (expr.isUnknown())
                    return detail::ExprFactoryInternals::unknown(factory);
                if (auto *rangeIndex =
                        detail::HandleAccess::dynCast<const detail::RangeIndexNode>(expr))
                    return detail::ExprFactoryInternals::rangeIndex(factory, rangeIndex->getName());
                if (auto *varAddr = detail::HandleAccess::dynCast<const VariableAddressNode>(expr))
                    return detail::ExprFactoryInternals::variableAddress(factory,
                                                                         varAddr->getFrom())
                        .asExpr();
                if (auto *fieldAddr = detail::HandleAccess::dynCast<const FieldAddressNode>(expr)) {
                    auto base = requireAddress(run(fieldAddr->getBaseAddr().asExpr()));
                    return detail::ExprFactoryInternals::fieldAddress(
                               factory, fieldAddr->getPointeeType(), fieldAddr->getDefinition(),
                               base, fieldAddr->getFieldIndex())
                        .asExpr();
                }
                if (auto *symbolValue =
                        detail::HandleAccess::dynCast<const SymbolValueNode>(expr)) {
                    auto from = requireAddress(run(symbolValue->getFromAddrHandle().asExpr()));
                    return detail::ExprFactoryInternals::symbolValue(
                        factory, symbolValue->getValType(), from,
                        symbolValue->getFromPoint().value());
                }
                if (auto *symbolAddr =
                        detail::HandleAccess::dynCast<const SymbolAddressNode>(expr)) {
                    std::optional<AddrHandle> from;
                    if (auto fromAddr = symbolAddr->getFromAddrHandle())
                        from = requireAddress(run(fromAddr->asExpr()));

                    std::optional<ExprHandle> length;
                    if (symbolAddr->getLength())
                        length = run(*symbolAddr->getLength());

                    return detail::ExprFactoryInternals::symbolAddress(
                               factory, symbolAddr->getPointeeType(), from,
                               symbolAddr->getFromPoint().value(), run(symbolAddr->getOffset()),
                               length)
                        .asExpr();
                }
                if (auto *binary =
                        detail::HandleAccess::dynCast<const detail::BinaryOpExprNode>(expr)) {
                    return detail::ExprFactoryInternals::binary(factory, run(binary->getLeft()),
                                                                binary->getOperator(),
                                                                run(binary->getRight()));
                }
                if (auto *unary =
                        detail::HandleAccess::dynCast<const detail::UnaryOpExprNode>(expr))
                    return detail::ExprFactoryInternals::unary(factory, unary->getOperator(),
                                                               run(unary->getSub()));
                if (auto *cast = detail::HandleAccess::dynCast<const detail::CastExprNode>(expr))
                    return detail::ExprFactoryInternals::cast(factory, run(cast->getOperand()),
                                                              cast->getValType());
                if (auto *structure = detail::HandleAccess::dynCast<const StructureNode>(expr)) {
                    auto rebuilt = detail::ExprFactoryInternals::importExpr(factory, expr);
                    for (size_t i = 0; i < structure->getNumFields(); ++i)
                        rebuilt = detail::ExprFactoryInternals::withField(
                            factory, rebuilt, i, run(structure->getFieldValue(i)));
                    return rebuilt;
                }
                if (expr.isSumOverRange()) {
                    SumOverRangeExpr sum{FacadeAccess::makeExpr(factory, expr)};
                    auto range = requireRange(run(FacadeAccess::exprHandle(sum.range().asExpr())));
                    return FacadeAccess::exprHandle(
                        SumOverRangeExpr{range, sum.indexName(), sum.fromPoint()});
                }
                if (expr.isQuantifierOverRange()) {
                    QuantifierOverRangeExpr quantifier{FacadeAccess::makeExpr(factory, expr)};
                    auto range =
                        requireRange(run(FacadeAccess::exprHandle(quantifier.range().asExpr())));
                    auto predicate = FacadeAccess::makeExpr(
                        factory, run(FacadeAccess::exprHandle(quantifier.predicate())));
                    return FacadeAccess::exprHandle(QuantifierOverRangeExpr{
                        range, quantifier.indexName(), quantifier.quantifier(), predicate});
                }
                if (expr.isMaxMinOverRange()) {
                    MaxMinOverRangeExpr maxMin{FacadeAccess::makeExpr(factory, expr)};
                    auto range =
                        requireRange(run(FacadeAccess::exprHandle(maxMin.range().asExpr())));
                    auto body = FacadeAccess::makeExpr(
                        factory, run(FacadeAccess::exprHandle(maxMin.body())));
                    return FacadeAccess::exprHandle(MaxMinOverRangeExpr{
                        range, maxMin.indexName(), maxMin.extremum(), body, maxMin.fromPoint()});
                }

                ERROR("Unsupported symbolic expression node in handle value substitution.");
            }
        };

        return Substituter{factory, substitutions}.run(expr);
    }

    static ExprHandle substitutePathHandle(ExprFactory &factory,
                                           ExprHandle expr,
                                           const Path &pathSubTo,
                                           const SourcePoint &pointToSub) {
        ExprFactoryScope scope(factory);

        struct Substituter {
            ExprFactory &factory;
            const Path &pathSubTo;
            const SourcePoint &pointToSub;

            AddrHandle requireAddress(ExprHandle handle) const {
                if (auto address = detail::AddrHandle::tryFrom(handle))
                    return detail::ExprFactoryInternals::importAddress(factory, *address);
                UNREACHABLE();
            }

            SymbolAddress requireRange(ExprHandle handle) const {
                auto address = FacadeAccess::makeExpr(factory, handle).tryAsAddress();
                if (address) {
                    auto range = SymbolAddress::tryFrom(*address);
                    if (range && range->length())
                        return *range;
                }
                ERROR("Substituted expression should be a *range*");
            }

            ExprHandle simplified(ExprHandle handle) const {
                return simplifyHandle(factory, handle);
            }

            ExprHandle run(ExprHandle expr) const {
                if (auto *literal =
                        detail::HandleAccess::dynCast<const detail::LiteralExprNode>(expr))
                    return literal->importInto(factory);
                if (expr.isUnknown())
                    return detail::ExprFactoryInternals::unknown(factory);
                if (auto *rangeIndex =
                        detail::HandleAccess::dynCast<const detail::RangeIndexNode>(expr))
                    return detail::ExprFactoryInternals::rangeIndex(factory, rangeIndex->getName());
                if (auto *varAddr = detail::HandleAccess::dynCast<const VariableAddressNode>(expr))
                    return detail::ExprFactoryInternals::variableAddress(factory,
                                                                         varAddr->getFrom())
                        .asExpr();
                if (auto *fieldAddr = detail::HandleAccess::dynCast<const FieldAddressNode>(expr)) {
                    auto base = requireAddress(run(fieldAddr->getBaseAddr().asExpr()));
                    return detail::ExprFactoryInternals::fieldAddress(
                               factory, fieldAddr->getPointeeType(), fieldAddr->getDefinition(),
                               base, fieldAddr->getFieldIndex())
                        .asExpr();
                }
                if (auto *symbolValue =
                        detail::HandleAccess::dynCast<const SymbolValueNode>(expr)) {
                    auto fromPoint = symbolValue->getFromPoint();
                    if (fromPoint && fromPoint.value() != pointToSub)
                        return detail::ExprFactoryInternals::importExpr(factory, expr);

                    auto realFromAddr =
                        requireAddress(run(symbolValue->getFromAddrHandle().asExpr()));
                    if (auto value = pathSubTo.getMemoryState().read(
                            FacadeAccess::makeAddress(pathSubTo.getExprFactory(), realFromAddr)))
                        return detail::ExprFactoryInternals::importExpr(
                            factory, FacadeAccess::exprHandle(*value));

                    return detail::ExprFactoryInternals::symbolValue(
                        factory, symbolValue->getValType(), realFromAddr,
                        pathSubTo.getStartPoint());
                }
                if (auto *symbolAddr =
                        detail::HandleAccess::dynCast<const SymbolAddressNode>(expr)) {
                    auto fromPoint = symbolAddr->getFromPoint();
                    if (fromPoint && fromPoint.value() != pointToSub)
                        return detail::ExprFactoryInternals::importExpr(factory, expr);

                    std::optional<ExprHandle> length;
                    if (symbolAddr->getLength())
                        length = detail::ExprFactoryInternals::importExpr(factory,
                                                                          *symbolAddr->getLength());

                    auto fromAddr = symbolAddr->getFromAddrHandle();
                    if (fromAddr == std::nullopt)
                        return detail::ExprFactoryInternals::symbolAddress(
                                   factory, symbolAddr->getPointeeType(), std::nullopt, pointToSub,
                                   detail::ExprFactoryInternals::importExpr(
                                       factory, symbolAddr->getOffset()),
                                   length)
                            .asExpr();

                    auto realFromAddr = requireAddress(run(fromAddr->asExpr()));
                    auto offset       = simplified(run(symbolAddr->getOffset()));
                    if (symbolAddr->getLength())
                        length = simplified(run(*symbolAddr->getLength()));

                    if (auto value = pathSubTo.getMemoryState().read(
                            FacadeAccess::makeAddress(pathSubTo.getExprFactory(), realFromAddr))) {
                        auto realAddr =
                            evaluateAddressHandle(factory, FacadeAccess::exprHandle(*value));
                        if (realAddr == std::nullopt)
                            ERROR("This expr should be a `SymbolAddress");

                        auto concreteAddr = FacadeAccess::makeAddress(factory, realAddr.value());
                        concreteAddr =
                            concreteAddr.withAddedOffset(FacadeAccess::makeExpr(factory, offset));
                        if (length)
                            concreteAddr = concreteAddr.withLength(
                                FacadeAccess::makeExpr(factory, length.value()));
                        return FacadeAccess::exprHandle(concreteAddr.asExpr());
                    }

                    return detail::ExprFactoryInternals::symbolAddress(
                               factory, symbolAddr->getPointeeType(), realFromAddr,
                               pathSubTo.getStartPoint(), offset, length)
                        .asExpr();
                }
                if (auto *binary =
                        detail::HandleAccess::dynCast<const detail::BinaryOpExprNode>(expr)) {
                    return detail::ExprFactoryInternals::binary(factory, run(binary->getLeft()),
                                                                binary->getOperator(),
                                                                run(binary->getRight()));
                }
                if (auto *unary =
                        detail::HandleAccess::dynCast<const detail::UnaryOpExprNode>(expr))
                    return detail::ExprFactoryInternals::unary(factory, unary->getOperator(),
                                                               run(unary->getSub()));
                if (auto *cast = detail::HandleAccess::dynCast<const detail::CastExprNode>(expr))
                    return detail::ExprFactoryInternals::cast(factory, run(cast->getOperand()),
                                                              cast->getValType());
                if (auto *structure = detail::HandleAccess::dynCast<const StructureNode>(expr)) {
                    auto rebuilt = detail::ExprFactoryInternals::importExpr(factory, expr);
                    for (size_t i = 0; i < structure->getNumFields(); ++i)
                        rebuilt = detail::ExprFactoryInternals::withField(
                            factory, rebuilt, i, run(structure->getFieldValue(i)));
                    return rebuilt;
                }
                if (expr.isSumOverRange()) {
                    SumOverRangeExpr sum{FacadeAccess::makeExpr(factory, expr)};
                    if (sum.fromPoint() != pointToSub)
                        return detail::ExprFactoryInternals::importExpr(factory, expr);
                    auto range = requireRange(run(FacadeAccess::exprHandle(sum.range().asExpr())));
                    return FacadeAccess::exprHandle(
                        SumOverRangeExpr{range, sum.indexName(), pathSubTo.getStartPoint()});
                }
                if (expr.isQuantifierOverRange()) {
                    QuantifierOverRangeExpr quantifier{FacadeAccess::makeExpr(factory, expr)};
                    auto range =
                        requireRange(run(FacadeAccess::exprHandle(quantifier.range().asExpr())));
                    auto predicate = FacadeAccess::makeExpr(
                        factory, run(FacadeAccess::exprHandle(quantifier.predicate())));
                    return FacadeAccess::exprHandle(QuantifierOverRangeExpr{
                        range, quantifier.indexName(), quantifier.quantifier(), predicate});
                }
                if (expr.isMaxMinOverRange()) {
                    MaxMinOverRangeExpr maxMin{FacadeAccess::makeExpr(factory, expr)};
                    auto range =
                        requireRange(run(FacadeAccess::exprHandle(maxMin.range().asExpr())));
                    auto body = FacadeAccess::makeExpr(
                        factory, run(FacadeAccess::exprHandle(maxMin.body())));
                    if (maxMin.fromPoint() == pointToSub)
                        TODO();
                    return FacadeAccess::exprHandle(MaxMinOverRangeExpr{
                        range, maxMin.indexName(), maxMin.extremum(), body, maxMin.fromPoint()});
                }

                ERROR("Unsupported symbolic expression node in handle path substitution.");
            }
        };

        return Substituter{factory, pathSubTo, pointToSub}.run(expr);
    }

    static ExprHandle substituteRangeIndexHandle(ExprFactory &factory,
                                                 ExprHandle expr,
                                                 const SymbolAddrBaseInfo &rangeBase,
                                                 ExprHandle indexExpr) {
        ExprFactoryScope scope(factory);

        struct Substituter {
            ExprFactory &factory;
            const SymbolAddrBaseInfo &rangeBase;
            ExprHandle indexExpr;

            AddrHandle requireAddress(ExprHandle handle) const {
                if (auto address = detail::AddrHandle::tryFrom(handle))
                    return detail::ExprFactoryInternals::importAddress(factory, *address);
                UNREACHABLE();
            }

            SymbolAddress requireRange(ExprHandle handle) const {
                auto address = FacadeAccess::makeExpr(factory, handle).tryAsAddress();
                if (address) {
                    auto range = SymbolAddress::tryFrom(*address);
                    if (range && range->length())
                        return *range;
                }
                ERROR("Substituted expression should be a *range*");
            }

            ExprHandle run(ExprHandle expr) const {
                (void)rangeBase;

                if (auto *literal =
                        detail::HandleAccess::dynCast<const detail::LiteralExprNode>(expr))
                    return literal->importInto(factory);
                if (expr.isUnknown())
                    return detail::ExprFactoryInternals::unknown(factory);
                if (expr.isRangeIndex())
                    return indexExpr;
                if (auto *varAddr = detail::HandleAccess::dynCast<const VariableAddressNode>(expr))
                    return detail::ExprFactoryInternals::variableAddress(factory,
                                                                         varAddr->getFrom())
                        .asExpr();
                if (auto *fieldAddr = detail::HandleAccess::dynCast<const FieldAddressNode>(expr)) {
                    auto base = requireAddress(run(fieldAddr->getBaseAddr().asExpr()));
                    return detail::ExprFactoryInternals::fieldAddress(
                               factory, fieldAddr->getPointeeType(), fieldAddr->getDefinition(),
                               base, fieldAddr->getFieldIndex())
                        .asExpr();
                }
                if (auto *symbolValue =
                        detail::HandleAccess::dynCast<const SymbolValueNode>(expr)) {
                    auto from = requireAddress(run(symbolValue->getFromAddrHandle().asExpr()));
                    return detail::ExprFactoryInternals::symbolValue(
                        factory, symbolValue->getValType(), from,
                        symbolValue->getFromPoint().value());
                }
                if (auto *symbolAddr =
                        detail::HandleAccess::dynCast<const SymbolAddressNode>(expr)) {
                    std::optional<AddrHandle> from;
                    if (auto fromAddr = symbolAddr->getFromAddrHandle())
                        from = requireAddress(run(fromAddr->asExpr()));

                    std::optional<ExprHandle> length;
                    if (symbolAddr->getLength())
                        length = run(*symbolAddr->getLength());

                    return detail::ExprFactoryInternals::symbolAddress(
                               factory, symbolAddr->getPointeeType(), from,
                               symbolAddr->getFromPoint().value(), run(symbolAddr->getOffset()),
                               length)
                        .asExpr();
                }
                if (auto *binary =
                        detail::HandleAccess::dynCast<const detail::BinaryOpExprNode>(expr))
                    return detail::ExprFactoryInternals::binary(factory, run(binary->getLeft()),
                                                                binary->getOperator(),
                                                                run(binary->getRight()));
                if (auto *unary =
                        detail::HandleAccess::dynCast<const detail::UnaryOpExprNode>(expr))
                    return detail::ExprFactoryInternals::unary(factory, unary->getOperator(),
                                                               run(unary->getSub()));
                if (auto *cast = detail::HandleAccess::dynCast<const detail::CastExprNode>(expr))
                    return detail::ExprFactoryInternals::cast(factory, run(cast->getOperand()),
                                                              cast->getValType());
                if (auto *structure = detail::HandleAccess::dynCast<const StructureNode>(expr)) {
                    auto rebuilt = detail::ExprFactoryInternals::importExpr(factory, expr);
                    for (size_t i = 0; i < structure->getNumFields(); ++i)
                        rebuilt = detail::ExprFactoryInternals::withField(
                            factory, rebuilt, i, run(structure->getFieldValue(i)));
                    return rebuilt;
                }
                if (expr.isSumOverRange()) {
                    SumOverRangeExpr sum{FacadeAccess::makeExpr(factory, expr)};
                    auto range = requireRange(run(FacadeAccess::exprHandle(sum.range().asExpr())));
                    return FacadeAccess::exprHandle(
                        SumOverRangeExpr{range, sum.indexName(), sum.fromPoint()});
                }
                if (expr.isQuantifierOverRange()) {
                    QuantifierOverRangeExpr quantifier{FacadeAccess::makeExpr(factory, expr)};
                    auto range =
                        requireRange(run(FacadeAccess::exprHandle(quantifier.range().asExpr())));
                    auto predicate = FacadeAccess::makeExpr(
                        factory, run(FacadeAccess::exprHandle(quantifier.predicate())));
                    return FacadeAccess::exprHandle(QuantifierOverRangeExpr{
                        range, quantifier.indexName(), quantifier.quantifier(), predicate});
                }
                if (expr.isMaxMinOverRange()) {
                    MaxMinOverRangeExpr maxMin{FacadeAccess::makeExpr(factory, expr)};
                    auto range =
                        requireRange(run(FacadeAccess::exprHandle(maxMin.range().asExpr())));
                    auto body = FacadeAccess::makeExpr(
                        factory, run(FacadeAccess::exprHandle(maxMin.body())));
                    return FacadeAccess::exprHandle(MaxMinOverRangeExpr{
                        range, maxMin.indexName(), maxMin.extremum(), body, maxMin.fromPoint()});
                }

                ERROR("Unsupported symbolic expression node in handle range-index substitution.");
            }
        };

        return Substituter{factory, rangeBase, indexExpr}.run(expr);
    }

    ExprHandle detail::ExprFactoryBackend::importExpr(ExprHandle expr) {
        const auto &node = detail::HandleAccess::node(expr);
        auto bucket      = interned.find(node.hash());
        if (bucket != interned.end() &&
            std::ranges::find(bucket->second, &node) != bucket->second.end())
            return expr;
        return importNode(node);
    }

    AddrHandle detail::ExprFactoryBackend::importAddress(AddrHandle address) {
        auto imported = detail::AddrHandle::tryFrom(importExpr(address.asExpr()));
        if (!imported)
            ERROR("ExprFactory::importAddress produced a non-address node.");
        return *imported;
    }

    AddrHandle detail::ExprFactoryBackend::importAddressNode(const AddressNode &address) {
        auto imported = detail::AddrHandle::tryFrom(importNode(address));
        if (!imported)
            ERROR("ExprFactory::importAddressNode produced a non-address node.");
        return *imported;
    }

    ExprHandle detail::ExprFactoryBackend::importNode(const SymbolicExprNode &expr) {
        auto preserveImportedType = [this, &expr](ExprHandle imported) {
            if (imported.getValType() == expr.getValType())
                return imported;
            return withValType(imported, expr.getValType());
        };

        if (auto *literal = dyn_cast<detail::LiteralExprNode>(&expr))
            return preserveImportedType(literal->importInto(owner));

        if (isa<detail::UnknownExprNode>(&expr))
            return preserveImportedType(unknown());

        if (auto *index = dyn_cast<detail::RangeIndexNode>(&expr))
            return preserveImportedType(rangeIndex(index->getName()));

        if (auto *unaryExpr = dyn_cast<detail::UnaryOpExprNode>(&expr))
            return preserveImportedType(
                unary(unaryExpr->getOperator(),
                      importNode(detail::HandleAccess::node(unaryExpr->getSub()))));

        if (auto *castExpr = dyn_cast<detail::CastExprNode>(&expr))
            return cast(importNode(detail::HandleAccess::node(castExpr->getOperand())),
                        castExpr->getValType());

        if (auto *binaryExpr = dyn_cast<detail::BinaryOpExprNode>(&expr)) {
            auto left  = importNode(detail::HandleAccess::node(binaryExpr->getLeft()));
            auto right = importNode(detail::HandleAccess::node(binaryExpr->getRight()));
            return preserveImportedType(binary(left, binaryExpr->getOperator(), right));
        }

        if (auto *variableAddr = dyn_cast<VariableAddressNode>(&expr))
            return preserveImportedType(variableAddress(variableAddr->getFrom()).asExpr());

        if (auto *fieldAddr = dyn_cast<FieldAddressNode>(&expr)) {
            auto base = importAddressNode(detail::HandleAccess::node(fieldAddr->getBaseAddr()));
            return preserveImportedType(fieldAddress(fieldAddr->getPointeeType(),
                                                     fieldAddr->getDefinition(), base,
                                                     fieldAddr->getFieldIndex())
                                            .asExpr());
        }

        if (auto *symbolAddr = dyn_cast<SymbolAddressNode>(&expr)) {
            auto from = symbolAddr->getFromAddrHandle();
            if (from)
                from = importAddressNode(detail::HandleAccess::node(*from));

            std::optional<ExprHandle> length;
            if (const auto &legacyLength = symbolAddr->getLength(); legacyLength)
                length = importNode(detail::HandleAccess::node(legacyLength.value()));

            auto offset = importNode(detail::HandleAccess::node(symbolAddr->getOffset()));

            return preserveImportedType(
                symbolAddress(symbolAddr->getPointeeType(), nodePointer(from),
                              symbolAddr->getFromPoint().value(),
                              &detail::HandleAccess::node(offset), nodePointer(length))
                    .asExpr());
        }

        if (auto *symbolVal = dyn_cast<SymbolValueNode>(&expr))
            return preserveImportedType(symbolValue(
                symbolVal->getValType(),
                importAddressNode(detail::HandleAccess::node(symbolVal->getFromAddrHandle())),
                symbolVal->getFromPoint().value()));

        if (auto *structure = dyn_cast<StructureNode>(&expr)) {
            std::vector<ExprHandle> fields;
            fields.reserve(structure->getNumFields());
            for (auto field : structure->fieldsValues())
                fields.push_back(importNode(detail::HandleAccess::node(field)));
            return preserveImportedType(
                intern(detail::ExprFactoryInternals::makeNode<StructureNode>(structure->getInfo(),
                                                                             std::move(fields))));
        }

        if (auto *sum = dyn_cast<SumOverRangeNode>(&expr)) {
            auto fromPoint = sum->getFromPoint();
            if (!fromPoint)
                ERROR("SumOverRange must have a source point.");
            auto range = FacadeAccess::makeAddress(owner, sum->getRange().handle());
            return preserveImportedType(FacadeAccess::exprHandle(
                SumOverRangeExpr{range, sum->getIndexName(), fromPoint.value()}));
        }

        if (auto *quantifier = dyn_cast<QuantifierOverRangeNode>(&expr)) {
            auto range     = FacadeAccess::makeAddress(owner, quantifier->getRange().handle());
            auto predicate = FacadeAccess::makeExpr(owner, quantifier->getPredicate());
            return preserveImportedType(FacadeAccess::exprHandle(QuantifierOverRangeExpr{
                range, quantifier->getIndexName(), quantifier->getQuantifier(), predicate}));
        }

        if (auto *maxMin = dyn_cast<MaxMinOverRangeNode>(&expr)) {
            auto fromPoint = maxMin->getFromPoint();
            if (!fromPoint)
                ERROR("MaxMinOverRange must have a source point.");
            auto range = FacadeAccess::makeAddress(owner, maxMin->getRange().handle());
            auto body  = FacadeAccess::makeExpr(owner, maxMin->getExpr());
            return preserveImportedType(FacadeAccess::exprHandle(MaxMinOverRangeExpr{
                range, maxMin->getIndexName(), maxMin->getExtremum(), body, fromPoint.value()}));
        }

        ERROR("Unsupported symbolic expression type in ExprFactory::importExpr: " + expr.dump());
    }

    namespace {
        template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };

        template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

        using Type       = ExprType;
        using ScalarKind = ExprScalarKind;

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
            return &detail::HandleAccess::cast<detail::LiteralExprNode>(handle);
        }

        const detail::LiteralExprNode *evaluateToLiteralNode(const detail::SymbolicExprNode &expr);

        inline const detail::LiteralExprNode *makeLiteralFromUnifiedType(Type t,
                                                                         bool asBool,
                                                                         uint64_t raw) {
            auto &factory = ExprFactoryScope::current();
            if (asBool)
                return literalNode(detail::ExprFactoryInternals::literal(factory, asBool));

            unsigned bw = t.bitWidth ? t.bitWidth : 64;
            if (t.kind == ScalarKind::UInt) {
                uint64_t u = coerceU(bw, raw);
                if (bw <= 16)
                    return literalNode(
                        detail::ExprFactoryInternals::literal(factory, (unsigned short)u));
                if (bw <= 32)
                    return literalNode(
                        detail::ExprFactoryInternals::literal(factory, (unsigned int)u));
                return literalNode(detail::ExprFactoryInternals::literal(factory, (uint64_t)u));
            } else { // Int
                int64_t s = coerceS(bw, raw);
                if (bw <= 16)
                    return literalNode(detail::ExprFactoryInternals::literal(factory, (short)s));
                if (bw <= 32)
                    return literalNode(detail::ExprFactoryInternals::literal(factory, (int)s));
                return literalNode(detail::ExprFactoryInternals::literal(factory, (int64_t)s));
            }
        }

        inline bool isBooleanExpr(const detail::SymbolicExprNode &e) {
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
        inline bool literalAsBool(const detail::LiteralExprNode &L) {
            return L.getLiteralValue() != 0;
        }
    } // namespace

    ExprHandle detail::ExprFactoryBackend::literal(bool value) {
        return intern(makeNode<detail::LiteralExprNode>(value));
    }

    ExprHandle detail::ExprFactoryBackend::literal(int value) {
        return intern(makeNode<detail::LiteralExprNode>(value));
    }

    ExprHandle detail::ExprFactoryBackend::literal(unsigned int value) {
        return intern(makeNode<detail::LiteralExprNode>(value));
    }

    ExprHandle detail::ExprFactoryBackend::literal(short value) {
        return intern(makeNode<detail::LiteralExprNode>(value));
    }

    ExprHandle detail::ExprFactoryBackend::literal(unsigned short value) {
        return intern(makeNode<detail::LiteralExprNode>(value));
    }

    ExprHandle detail::ExprFactoryBackend::literal(int64_t value) {
        return intern(makeNode<detail::LiteralExprNode>(value));
    }

    ExprHandle detail::ExprFactoryBackend::literal(uint64_t value) {
        return intern(makeNode<detail::LiteralExprNode>(value));
    }

    ExprHandle detail::ExprFactoryBackend::unknown() { return intern(makeNode<UnknownExprNode>()); }

    ExprHandle detail::ExprFactoryBackend::rangeIndex(std::string_view name) {
        return intern(detail::ExprFactoryInternals::makeNode<detail::RangeIndexNode>(name));
    }

    ExprHandle detail::ExprFactoryBackend::symbolValue(ExprType varType,
                                                       AddrHandle from,
                                                       SourcePoint fromPoint) {
        return intern(detail::ExprFactoryInternals::makeNode<SymbolValueNode>(
            varType, from, std::move(fromPoint)));
    }

    ExprHandle detail::ExprFactoryBackend::unary(UnaryOp op, ExprHandle expr) {
        return intern(makeNode<detail::UnaryOpExprNode>(op, expr));
    }

    ExprHandle detail::ExprFactoryBackend::cast(ExprHandle expr, ExprType targetType) {
        if (expr.getValType() == targetType)
            return importExpr(expr);
        if (targetType.kind == ExprScalarKind::Void ||
            targetType.kind == ExprScalarKind::Structure || targetType.bitWidth == 0)
            ERROR("CastExpr requires a scalar target type.");
        return intern(makeNode<detail::CastExprNode>(importExpr(expr), targetType));
    }

    ExprHandle detail::ExprFactoryBackend::binary(ExprHandle left, BinaryOp op, ExprHandle right) {
        return intern(makeNode<detail::BinaryOpExprNode>(left, op, right));
    }

    ExprHandle detail::ExprFactoryBackend::simplifiedBinary(ExprHandle left,
                                                            BinaryOp op,
                                                            ExprHandle right) {
        return simplifyHandle(owner, binary(left, op, right));
    }

    AddrHandle detail::ExprFactoryBackend::variableAddress(
        utils::not_null<const clang::VarDecl *> from) {
        return internAddress(detail::ExprFactoryInternals::makeNode<VariableAddressNode>(from));
    }

    AddrHandle detail::ExprFactoryBackend::symbolAddress(clang::QualType pointeeType,
                                                         const AddressNode *from,
                                                         SourcePoint fromPoint,
                                                         const SymbolicExprNode *offset,
                                                         const SymbolicExprNode *length) {
        auto fromHandle = from ? std::optional{detail::HandleAccess::handle(from)} : std::nullopt;
        auto resolvedOffset =
            offset ? detail::HandleAccess::handle(offset)
                   : literal(static_cast<int64_t>(detail::SymbolAddressView::ZERO_OFFSET));
        auto lengthHandle =
            length ? std::optional{detail::HandleAccess::handle(length)} : std::nullopt;
        return internAddress(detail::ExprFactoryInternals::makeNode<SymbolAddressNode>(
            pointeeType, fromHandle, std::move(fromPoint), resolvedOffset, lengthHandle));
    }

    AddrHandle detail::ExprFactoryBackend::withOffset(AddrHandle address, ExprHandle offset) {
        const auto &symbolAddr = detail::HandleAccess::cast<SymbolAddressNode>(address);
        auto from              = symbolAddr.getFromAddrHandle();

        std::optional<ExprHandle> length;
        if (const auto &existingLength = symbolAddr.getLength(); existingLength)
            length = importExpr(*existingLength);

        return symbolAddress(symbolAddr.getPointeeType(), nodePointer(from),
                             symbolAddr.getFromPoint().value(), &detail::HandleAccess::node(offset),
                             nodePointer(length));
    }

    AddrHandle detail::ExprFactoryBackend::withAddedOffset(AddrHandle address, ExprHandle extra) {
        const auto &symbolAddr = detail::HandleAccess::cast<SymbolAddressNode>(address);
        auto newOffset         = simplifiedBinary(importExpr(symbolAddr.getOffset()),
                                                  detail::BinaryOpExprNode::Operator::Add, extra);
        return withOffset(address, newOffset);
    }

    AddrHandle detail::ExprFactoryBackend::withSubtractedOffset(AddrHandle address,
                                                                ExprHandle extra) {
        const auto &symbolAddr = detail::HandleAccess::cast<SymbolAddressNode>(address);
        auto newOffset         = simplifiedBinary(importExpr(symbolAddr.getOffset()),
                                                  detail::BinaryOpExprNode::Operator::Subtract, extra);
        return withOffset(address, newOffset);
    }

    AddrHandle detail::ExprFactoryBackend::withLength(AddrHandle address, ExprHandle length) {
        const auto &symbolAddr = detail::HandleAccess::cast<SymbolAddressNode>(address);
        auto from              = symbolAddr.getFromAddrHandle();

        auto offset = importExpr(symbolAddr.getOffset());
        return symbolAddress(symbolAddr.getPointeeType(), nodePointer(from),
                             symbolAddr.getFromPoint().value(), &detail::HandleAccess::node(offset),
                             &detail::HandleAccess::node(length));
    }

    AddrHandle detail::ExprFactoryBackend::withAddedLength(AddrHandle address, ExprHandle extra) {
        const auto &symbolAddr = detail::HandleAccess::cast<SymbolAddressNode>(address);
        auto currentLength =
            symbolAddr.getLength() ? importExpr(*symbolAddr.getLength()) : literal(1);
        auto newLength =
            simplifiedBinary(currentLength, detail::BinaryOpExprNode::Operator::Add, extra);
        return withLength(address, newLength);
    }

    AddrHandle detail::ExprFactoryBackend::withoutLength(AddrHandle address) {
        const auto &symbolAddr = detail::HandleAccess::cast<SymbolAddressNode>(address);
        auto from              = symbolAddr.getFromAddrHandle();

        auto offset = importExpr(symbolAddr.getOffset());
        return symbolAddress(symbolAddr.getPointeeType(), nodePointer(from),
                             symbolAddr.getFromPoint().value(), &detail::HandleAccess::node(offset),
                             nullptr);
    }

    AddrHandle detail::ExprFactoryBackend::fieldAddress(clang::QualType pointeeType,
                                                        const clang::RecordDecl *record,
                                                        AddrHandle baseAddr,
                                                        size_t fieldIndex) {
        return internAddress(detail::ExprFactoryInternals::makeNode<FieldAddressNode>(
            pointeeType, record, baseAddr, fieldIndex));
    }

    ExprHandle detail::ExprFactoryBackend::structure(const clang::RecordDecl *record,
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
                fields.push_back(symbolAddress(fty, &detail::HandleAccess::node(fieldAddr),
                                               fromPoint, nullptr, nullptr)
                                     .asExpr());
            } else if (fty->isArrayType()) {
                auto arrayType = llvm::cast<clang::ArrayType>(fty);
                auto elemTy    = arrayType->getElementType();
                std::optional<ExprHandle> length;
                if (auto *cat = llvm::dyn_cast<clang::ConstantArrayType>(fty.getTypePtr()))
                    length = literal(cat->getSize().getZExtValue());
                fields.push_back(symbolAddress(elemTy, &detail::HandleAccess::node(fieldAddr),
                                               fromPoint, nullptr, nodePointer(length))
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

    ExprHandle detail::ExprFactoryBackend::withField(ExprHandle structure,
                                                     size_t index,
                                                     ExprHandle value) {
        const auto &structureNode = detail::HandleAccess::cast<StructureNode>(structure);
        if (index >= structureNode.getNumFields())
            ERROR("Out-of-bounds access");

        std::vector<ExprHandle> fields;
        fields.reserve(structureNode.getNumFields());
        size_t currentIndex = 0;
        for (auto field : structureNode.fieldsValues()) {
            fields.push_back(currentIndex == index ? value : importExpr(field));
            ++currentIndex;
        }

        return intern(detail::ExprFactoryInternals::makeNode<StructureNode>(structureNode.getInfo(),
                                                                            std::move(fields)));
    }

    namespace {
        bool containsCast(ExprHandle expr) {
            if (expr.isCastExpr())
                return true;
            if (auto *binary =
                    detail::HandleAccess::dynCast<const detail::BinaryOpExprNode>(expr)) {
                return containsCast(binary->getLeft()) || containsCast(binary->getRight());
            }
            if (auto *unary =
                    detail::HandleAccess::dynCast<const detail::UnaryOpExprNode>(expr))
                return containsCast(unary->getSub());
            return false;
        }

        ExprHandle simplifyHandle(ExprFactory &factory, ExprHandle expr) {
            ExprFactoryScope scope(factory);
            if (expr.isUnknown())
                return detail::ExprFactoryInternals::unknown(factory);

            if (auto *symbolAddr = detail::HandleAccess::dynCast<const SymbolAddressNode>(expr)) {
                if (symbolAddr->getLength())
                    ERROR("Address range is solely for address representation and should not be "
                          "used as an expression.");
                return detail::ExprFactoryInternals::importExpr(factory, expr);
            }

            if (auto *binary =
                    detail::HandleAccess::dynCast<const detail::BinaryOpExprNode>(expr)) {
                if (binary->isLinear() && !containsCast(expr))
                    return binary->simplifiedExprIfLinear();

                if (auto c = evaluateToLiteralNode(*binary))
                    return c->importInto(factory);

                auto lhs = simplifyHandle(factory, binary->getLeft());
                auto rhs = simplifyHandle(factory, binary->getRight());

                using Op = detail::BinaryOpExprNode::Operator;
                if (binary->getOperator() == Op::Equal || binary->getOperator() == Op::NotEqual) {
                    auto simplifyBoolCmp = [&](ExprHandle boolExpr,
                                               ExprHandle litExpr) -> std::optional<ExprHandle> {
                        auto lit =
                            detail::HandleAccess::dynCast<const detail::LiteralExprNode>(litExpr);
                        if (!lit)
                            return std::nullopt;
                        auto value = lit->getLiteralValue();
                        if (value != 0 && value != 1)
                            return std::nullopt;
                        if (!isBooleanExpr(detail::HandleAccess::node(boolExpr)))
                            return std::nullopt;

                        const bool expectTrue =
                            binary->getOperator() == Op::Equal ? value == 1 : value == 0;
                        if (expectTrue)
                            return detail::ExprFactoryInternals::importExpr(factory, boolExpr);
                        return detail::ExprFactoryInternals::unary(
                            factory, detail::UnaryOpExprNode::Operator::LogicalNot,
                            detail::ExprFactoryInternals::importExpr(factory, boolExpr));
                    };

                    if (auto simplified = simplifyBoolCmp(lhs, rhs))
                        return *simplified;
                    if (auto simplified = simplifyBoolCmp(rhs, lhs))
                        return *simplified;
                }

                if (binary->getOperator() == Op::LogicalAnd) {
                    if (auto leftConst = evaluateToLiteralNode(detail::HandleAccess::node(lhs))) {
                        if (!literalAsBool(*leftConst))
                            return leftConst->importInto(factory);
                        return rhs;
                    }
                    if (auto rightConst = evaluateToLiteralNode(detail::HandleAccess::node(rhs))) {
                        if (!literalAsBool(*rightConst))
                            return rightConst->importInto(factory);
                        return lhs;
                    }
                } else if (binary->getOperator() == Op::LogicalOr) {
                    if (auto leftConst = evaluateToLiteralNode(detail::HandleAccess::node(lhs))) {
                        if (literalAsBool(*leftConst))
                            return leftConst->importInto(factory);
                        return rhs;
                    }
                    if (auto rightConst = evaluateToLiteralNode(detail::HandleAccess::node(rhs))) {
                        if (literalAsBool(*rightConst))
                            return rightConst->importInto(factory);
                        return lhs;
                    }
                }

                return detail::ExprFactoryInternals::binary(factory, lhs, binary->getOperator(),
                                                            rhs);
            }

            if (auto *unary = detail::HandleAccess::dynCast<const detail::UnaryOpExprNode>(expr)) {
                if (unary->isLinear() && !containsCast(expr))
                    return unary->simplifiedExprIfLinear();
                return detail::ExprFactoryInternals::unary(
                    factory, unary->getOperator(), simplifyHandle(factory, unary->getSub()));
            }

            if (auto *literal = detail::HandleAccess::dynCast<const detail::LiteralExprNode>(expr))
                return literal->simplifiedExprIfLinear();

            return detail::ExprFactoryInternals::importExpr(factory, expr);
        }
    } // namespace

    namespace {
        std::optional<AddrHandle> evaluateAddressHandle(ExprFactory &factory, ExprHandle expr) {
            ExprFactoryScope scope(factory);
            auto simplified = simplifyHandle(factory, expr);

            if (auto symbolAddr = detail::AddrHandle::tryFrom(simplified))
                return detail::ExprFactoryInternals::importAddress(factory, *symbolAddr);

            auto *binary =
                detail::HandleAccess::dynCast<const detail::BinaryOpExprNode>(simplified);
            if (!binary)
                return std::nullopt;

            auto left  = binary->getLeft();
            auto right = binary->getRight();
            auto lhs   = evaluateAddressHandle(factory, left);
            auto rhs   = evaluateAddressHandle(factory, right);
            if (lhs && rhs)
                return std::nullopt;
            if (!lhs && !rhs)
                return std::nullopt;

            auto isValidOffsetOrLengthHandle = [&](ExprHandle candidate) {
                if (candidate.isUnknown())
                    return true;
                return !evaluateAddressHandle(factory, candidate).has_value();
            };

            using Op = detail::BinaryOpExprNode::Operator;
            if (lhs) {
                if (!isValidOffsetOrLengthHandle(right))
                    return std::nullopt;
                auto offset = detail::ExprFactoryInternals::importExpr(factory, right);
                switch (binary->getOperator()) {
                    case Op::Add:
                        return detail::ExprFactoryInternals::withAddedOffset(factory, *lhs, offset);
                    case Op::Subtract:
                        return detail::ExprFactoryInternals::withSubtractedOffset(factory, *lhs,
                                                                                  offset);
                    default: return std::nullopt;
                }
            }

            if (!isValidOffsetOrLengthHandle(left))
                return std::nullopt;
            auto offset = detail::ExprFactoryInternals::importExpr(factory, left);
            switch (binary->getOperator()) {
                case Op::Add:
                    return detail::ExprFactoryInternals::withAddedOffset(factory, *rhs, offset);
                case Op::Subtract: return std::nullopt;
                default: return std::nullopt;
            }
        }
    } // namespace

    ExprHandle detail::SymbolicExprNode::simplifiedExpr() const {
        auto &factory = ExprFactoryScope::current();
        return simplifyHandle(factory, selfHandle());
    }

    /**
     * @brief Simplify a linear expression by rebuilding it as a minimal sum of terms.
     * @return Interned simplified expression when linear; otherwise the imported node.
     */
    ExprHandle detail::SymbolicExprNode::simplifiedExprIfLinear() const {
        if (!isLinear())
            return detail::ExprFactoryInternals::importExpr(ExprFactoryScope::current(),
                                                            selfHandle());
        auto [usedSymbols, expressionIndexMap] = collectUsedSymbolHandles(selfHandle());

        auto &factory   = ExprFactoryScope::current();
        auto linearExpr = toLinearExpr(expressionIndexMap);
        std::optional<ExprHandle> result{};

        using enum detail::BinaryOpExprNode::Operator;
        for (auto symbol : usedSymbols) {
            auto C =
                linearExpr
                    .coefficient(Parma_Polyhedra_Library::Variable{expressionIndexMap.at(symbol)})
                    .get_si();
            if (C == 0)
                continue;

            if (result == std::nullopt) {
                // Seed the accumulator with the first non-zero term.
                if (C == 1)
                    result = detail::ExprFactoryInternals::importExpr(factory, symbol);
                else
                    result = detail::ExprFactoryInternals::binary(
                        factory,
                        detail::ExprFactoryInternals::literal(factory, static_cast<int64_t>(C)),
                        Multiply, detail::ExprFactoryInternals::importExpr(factory, symbol));
            } else {
                unsigned absC      = std::abs(C);
                ExprHandle varExpr = detail::ExprFactoryInternals::importExpr(factory, symbol);
                if (absC != 1)
                    varExpr = detail::ExprFactoryInternals::binary(
                        factory,
                        detail::ExprFactoryInternals::literal(factory, static_cast<int64_t>(absC)),
                        Multiply, detail::ExprFactoryInternals::importExpr(factory, symbol));
                // Combine the current polynomial with the new term using the sign of the
                // coefficient.
                result = detail::ExprFactoryInternals::binary(factory, result.value(),
                                                              (C > 0 ? Add : Subtract), varExpr);
            }
        }
        if (auto inhomo = linearExpr.inhomogeneous_term().get_si();
            inhomo || result == std::nullopt) {
            if (result != std::nullopt) {
                // Append the constant term to the linear combination.
                result = detail::ExprFactoryInternals::binary(
                    factory, result.value(), (inhomo > 0 ? Add : Subtract),
                    detail::ExprFactoryInternals::literal(factory,
                                                          static_cast<int64_t>(std::abs(inhomo))));
            } else
                result =
                    detail::ExprFactoryInternals::literal(factory, static_cast<int64_t>(inhomo));
        }
        if (result == std::nullopt) {
            ERROR("Simplified expr is null! Something goes wrong.");
        }
        return result.value();
    }

    ExprHandle detail::LiteralExprNode::importInto(ExprFactory &factory) const {
        switch (getLiteralType()) {
            case LiteralType::Boolean:
                return detail::ExprFactoryInternals::literal(factory, data_.boolValue);
            case LiteralType::Int:
                return detail::ExprFactoryInternals::literal(factory, data_.intValue);
            case LiteralType::UnsignedInt:
                return detail::ExprFactoryInternals::literal(factory, data_.uintValue);
            case LiteralType::Short:
                return detail::ExprFactoryInternals::literal(factory, data_.shortValue);
            case LiteralType::UnsignedShort:
                return detail::ExprFactoryInternals::literal(factory, data_.ushortValue);
            case LiteralType::Int64:
                return detail::ExprFactoryInternals::literal(factory, data_.int64Value);
            case LiteralType::UInt64:
                return detail::ExprFactoryInternals::literal(factory, data_.uint64Value);
        }

        UNREACHABLE();
    }

    std::unique_ptr<detail::LiteralExprNode> detail::LiteralExprNode::rebuildNode(
        std::optional<Type> explicitType) const {
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

    std::variant<std::int64_t, std::uint64_t>
    detail::LiteralExprNode::getIntegerValue() const {
        switch (getLiteralType()) {
            case LiteralType::Boolean: return static_cast<std::int64_t>(data_.boolValue);
            case LiteralType::Int: return static_cast<std::int64_t>(data_.intValue);
            case LiteralType::UnsignedInt:
                return static_cast<std::uint64_t>(data_.uintValue);
            case LiteralType::Short: return static_cast<std::int64_t>(data_.shortValue);
            case LiteralType::UnsignedShort:
                return static_cast<std::uint64_t>(data_.ushortValue);
            case LiteralType::Int64: return data_.int64Value;
            case LiteralType::UInt64: return data_.uint64Value;
        }

        UNREACHABLE();
        return std::int64_t{0};
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
        size_t seed = utils::hash_val(detail::SymbolicExprNode::getKind(), fromAddr_.hash(),
                                      fromPoint_.hash());
        return seed;
    }

    size_t detail::UnaryOpExprNode::hash() const {
        return utils::hash_val(getKind(), static_cast<size_t>(op_), expr_.hash());
    }

    size_t detail::CastExprNode::hash() const {
        const auto target = getValType();
        return utils::hash_val(getKind(), static_cast<size_t>(target.kind), target.bitWidth,
                               operand_.hash());
    }

    size_t detail::BinaryOpExprNode::hash() const {
        return utils::hash_val(getKind(), static_cast<size_t>(op_), left_.hash(), right_.hash());
    }

    size_t SymbolAddressNode::hash() const {
        size_t seed = utils::hash_val(detail::SymbolicExprNode::getKind(), fromPoint_.hash(),
                                      offset_.hash(), length_ ? length_.value().hash() : 0);

        seed = utils::hash_val(seed, fromAddr_ ? fromAddr_.value().hash() : 0);
        return seed;
    }

    size_t VariableAddressNode::hash() const { return utils::hash_val(getKind(), from_.get()); }

    size_t FieldAddressNode::hash() const {
        return utils::hash_val(getKind(), baseAddr_.hash(), fieldIndex_);
    }

    size_t StructureNode::hash() const {
        auto seed = utils::hash_val(detail::SymbolicExprNode::getKind(), info_.definition_.get());
        for (auto &field : fields_)
            seed = utils::hash_val(seed, field.hash());
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
        oss << "(" << left_.dump() << " " << op(opStr) << " " << right_.dump() << ")";
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
        oss << op(opStr) << "(" << expr_.dump() << ")";
        return oss.str();
    }

    std::string detail::CastExprNode::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        const auto target = getValType();
        oss << type("Cast") << "(" << static_cast<unsigned>(target.kind) << ":" << target.bitWidth
            << ", " << operand_.dump() << ")";
        return oss.str();
    }

    std::string detail::UnknownExprNode::dump() const { return utils::dump_fmt::hint("{unknown}"); }

    detail::LiteralExprView::LiteralExprView(ExprHandle handle) : handle_(handle) {
        if (!handle_.isLiteralExpr())
            ERROR("LiteralExprView requires a literal expression.");
    }

    std::optional<detail::LiteralExprView> detail::LiteralExprView::tryFrom(ExprHandle handle) {
        if (!handle.isLiteralExpr())
            return std::nullopt;
        return LiteralExprView{handle};
    }

    int64_t detail::LiteralExprView::value() const {
        return detail::HandleAccess::cast<detail::LiteralExprNode>(handle_).getLiteralValue();
    }

    std::variant<std::int64_t, std::uint64_t>
    detail::LiteralExprView::integerValue() const {
        return detail::HandleAccess::cast<detail::LiteralExprNode>(handle_).getIntegerValue();
    }

    detail::UnaryExprView::UnaryExprView(ExprHandle handle) : handle_(handle) {
        if (!handle_.isUnaryExpr())
            ERROR("UnaryExprView requires a unary expression.");
    }

    std::optional<detail::UnaryExprView> detail::UnaryExprView::tryFrom(ExprHandle handle) {
        if (!handle.isUnaryExpr())
            return std::nullopt;
        return UnaryExprView{handle};
    }

    UnaryOp detail::UnaryExprView::operation() const {
        return detail::HandleAccess::cast<detail::UnaryOpExprNode>(handle_).getOperator();
    }

    ExprHandle detail::UnaryExprView::operand() const {
        return detail::HandleAccess::cast<detail::UnaryOpExprNode>(handle_).getSub();
    }

    detail::CastExprView::CastExprView(ExprHandle handle) : handle_(handle) {
        if (!handle_.isCastExpr())
            ERROR("CastExprView requires a cast expression.");
    }

    std::optional<detail::CastExprView> detail::CastExprView::tryFrom(ExprHandle handle) {
        if (!handle.isCastExpr())
            return std::nullopt;
        return CastExprView{handle};
    }

    ExprHandle detail::CastExprView::operand() const {
        return detail::HandleAccess::cast<detail::CastExprNode>(handle_).getOperand();
    }

    ExprType detail::CastExprView::targetType() const { return handle_.getValType(); }

    detail::BinaryExprView::BinaryExprView(ExprHandle handle) : handle_(handle) {
        if (!handle_.isBinaryExpr())
            ERROR("BinaryExprView requires a binary expression.");
    }

    std::optional<detail::BinaryExprView> detail::BinaryExprView::tryFrom(ExprHandle handle) {
        if (!handle.isBinaryExpr())
            return std::nullopt;
        return BinaryExprView{handle};
    }

    BinaryOp detail::BinaryExprView::operation() const {
        return detail::HandleAccess::cast<detail::BinaryOpExprNode>(handle_).getOperator();
    }

    ExprHandle detail::BinaryExprView::left() const {
        return detail::HandleAccess::cast<detail::BinaryOpExprNode>(handle_).getLeft();
    }

    ExprHandle detail::BinaryExprView::right() const {
        return detail::HandleAccess::cast<detail::BinaryOpExprNode>(handle_).getRight();
    }

    LiteralExpr::LiteralExpr(bool value) : LiteralExpr(ExprFactoryScope::current(), value) {}

    LiteralExpr::LiteralExpr(int value) : LiteralExpr(ExprFactoryScope::current(), value) {}

    LiteralExpr::LiteralExpr(unsigned int value)
        : LiteralExpr(ExprFactoryScope::current(), value) {}

    LiteralExpr::LiteralExpr(short value) : LiteralExpr(ExprFactoryScope::current(), value) {}

    LiteralExpr::LiteralExpr(unsigned short value)
        : LiteralExpr(ExprFactoryScope::current(), value) {}

    LiteralExpr::LiteralExpr(int64_t value) : LiteralExpr(ExprFactoryScope::current(), value) {}

    LiteralExpr::LiteralExpr(uint64_t value) : LiteralExpr(ExprFactoryScope::current(), value) {}

    LiteralExpr::LiteralExpr(ExprFactory &factory, bool value)
        : Expr(factory, detail::ExprFactoryInternals::literal(factory, value)) {}

    LiteralExpr::LiteralExpr(ExprFactory &factory, int value)
        : Expr(factory, detail::ExprFactoryInternals::literal(factory, value)) {}

    LiteralExpr::LiteralExpr(ExprFactory &factory, unsigned int value)
        : Expr(factory, detail::ExprFactoryInternals::literal(factory, value)) {}

    LiteralExpr::LiteralExpr(ExprFactory &factory, short value)
        : Expr(factory, detail::ExprFactoryInternals::literal(factory, value)) {}

    LiteralExpr::LiteralExpr(ExprFactory &factory, unsigned short value)
        : Expr(factory, detail::ExprFactoryInternals::literal(factory, value)) {}

    LiteralExpr::LiteralExpr(ExprFactory &factory, int64_t value)
        : Expr(factory, detail::ExprFactoryInternals::literal(factory, value)) {}

    LiteralExpr::LiteralExpr(ExprFactory &factory, uint64_t value)
        : Expr(factory, detail::ExprFactoryInternals::literal(factory, value)) {}

    LiteralExpr::LiteralExpr(const Expr &expression) : Expr(expression) {
        if (!FacadeAccess::exprHandle(*this).isLiteralExpr())
            ERROR("LiteralExpr requires a literal expression.");
    }

    std::optional<LiteralExpr> LiteralExpr::tryFrom(const Expr &expression) {
        if (!FacadeAccess::exprHandle(expression).isLiteralExpr())
            return std::nullopt;
        return LiteralExpr{expression};
    }

    int64_t LiteralExpr::value() const {
        return detail::LiteralExprView{FacadeAccess::exprHandle(*this)}.value();
    }

    std::variant<std::int64_t, std::uint64_t> LiteralExpr::integerValue() const {
        return detail::LiteralExprView{FacadeAccess::exprHandle(*this)}.integerValue();
    }

    UnaryExpr::UnaryExpr(const Expr &expression) : Expr(expression) {
        if (!FacadeAccess::exprHandle(*this).isUnaryExpr())
            ERROR("UnaryExpr requires a unary expression.");
    }

    std::optional<UnaryExpr> UnaryExpr::tryFrom(const Expr &expression) {
        if (!FacadeAccess::exprHandle(expression).isUnaryExpr())
            return std::nullopt;
        return UnaryExpr{expression};
    }

    UnaryOp UnaryExpr::operation() const {
        return detail::UnaryExprView{FacadeAccess::exprHandle(*this)}.operation();
    }

    Expr UnaryExpr::operand() const {
        auto handle = detail::UnaryExprView{FacadeAccess::exprHandle(*this)}.operand();
        return FacadeAccess::makeExpr(factory(), handle);
    }

    CastExpr::CastExpr(const Expr &expression) : Expr(expression) {
        if (!FacadeAccess::exprHandle(*this).isCastExpr())
            ERROR("CastExpr requires a cast expression.");
    }

    std::optional<CastExpr> CastExpr::tryFrom(const Expr &expression) {
        if (!FacadeAccess::exprHandle(expression).isCastExpr())
            return std::nullopt;
        return CastExpr{expression};
    }

    Expr CastExpr::operand() const {
        auto handle = detail::CastExprView{FacadeAccess::exprHandle(*this)}.operand();
        return FacadeAccess::makeExpr(factory(), handle);
    }

    ExprType CastExpr::targetType() const {
        return detail::CastExprView{FacadeAccess::exprHandle(*this)}.targetType();
    }

    BinaryExpr::BinaryExpr(const Expr &expression) : Expr(expression) {
        if (!FacadeAccess::exprHandle(*this).isBinaryExpr())
            ERROR("BinaryExpr requires a binary expression.");
    }

    std::optional<BinaryExpr> BinaryExpr::tryFrom(const Expr &expression) {
        if (!FacadeAccess::exprHandle(expression).isBinaryExpr())
            return std::nullopt;
        return BinaryExpr{expression};
    }

    BinaryOp BinaryExpr::operation() const {
        return detail::BinaryExprView{FacadeAccess::exprHandle(*this)}.operation();
    }

    Expr BinaryExpr::left() const {
        auto handle = detail::BinaryExprView{FacadeAccess::exprHandle(*this)}.left();
        return FacadeAccess::makeExpr(factory(), handle);
    }

    Expr BinaryExpr::right() const {
        auto handle = detail::BinaryExprView{FacadeAccess::exprHandle(*this)}.right();
        return FacadeAccess::makeExpr(factory(), handle);
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
        oss << key("addr") << ":" << fromAddr_.dump();
        oss << "}, ";

        oss << "{" << key("from point") << "=" << path(fromPoint_.dump()) << "}";
        return oss.str();
    }

    detail::SymbolValueView::SymbolValueView(ExprHandle handle) : handle_(handle) {
        if (!handle_.isSymbolValue())
            ERROR("SymbolValueView requires a SymbolValue expression.");
    }

    std::optional<detail::SymbolValueView> detail::SymbolValueView::tryFrom(ExprHandle handle) {
        if (!handle.isSymbolValue())
            return std::nullopt;
        return SymbolValueView{handle};
    }

    AddrHandle detail::SymbolValueView::from() const {
        return detail::HandleAccess::cast<SymbolValueNode>(handle_).getFromAddrHandle();
    }

    std::optional<SourcePoint> detail::SymbolValueView::fromPoint() const {
        return detail::HandleAccess::cast<SymbolValueNode>(handle_).getFromPoint();
    }

    std::optional<utils::not_null<const clang::VarDecl *>> detail::SymbolValueView::fromRoot()
        const {
        return detail::HandleAccess::cast<SymbolValueNode>(handle_).getFromRoot();
    }

    SymbolValueExpr::SymbolValueExpr(const Expr &expression) : Expr(expression) {
        if (!isSymbolValue())
            ERROR("SymbolValueExpr requires a symbol value expression.");
    }

    std::optional<SymbolValueExpr> SymbolValueExpr::tryFrom(const Expr &expression) {
        if (!expression.isSymbolValue())
            return std::nullopt;
        return SymbolValueExpr{expression};
    }

    Addr SymbolValueExpr::from() const {
        auto handle = detail::SymbolValueView{FacadeAccess::exprHandle(*this)}.from();
        return FacadeAccess::makeAddress(factory(), handle);
    }

    std::optional<SourcePoint> SymbolValueExpr::fromPoint() const {
        return detail::SymbolValueView{FacadeAccess::exprHandle(*this)}.fromPoint();
    }

    std::optional<utils::not_null<const clang::VarDecl *>> SymbolValueExpr::fromRoot() const {
        return detail::SymbolValueView{FacadeAccess::exprHandle(*this)}.fromRoot();
    }

    std::string SymbolAddressNode::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        oss << type("SymbolAddress");

        auto off = getOffset();
        if (length_ == std::nullopt)
            oss << "[" << off.dump() << "]";
        else
            oss << "[" << off.dump() << " " << hint("... +") << length_.value().dump() << "]";

        oss << " {" << key("from") << "=";
        if (fromAddr_)
            oss << key("addr") << ":" << fromAddr_.value().dump();
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
        oss << key("field of") << ":" << baseAddr_.dump() << "["
            << utils::dump_fmt::lit(std::to_string(fieldIndex_)) << "]";
        oss << "}";
        return oss.str();
    }

    namespace detail {
        VariableAddressView::VariableAddressView(AddrHandle handle) : handle_(handle) {
            if (!handle_.isVariableAddress())
                ERROR("VariableAddressView requires a variable address.");
        }

        std::optional<VariableAddressView> VariableAddressView::tryFrom(AddrHandle handle) {
            if (!handle.isVariableAddress())
                return std::nullopt;
            return VariableAddressView{handle};
        }

        std::optional<VariableAddressView> VariableAddressView::tryFrom(ExprHandle handle) {
            auto address = detail::AddrHandle::tryFrom(handle);
            return address ? tryFrom(*address) : std::nullopt;
        }

        utils::not_null<const clang::VarDecl *> VariableAddressView::declaration() const {
            return detail::HandleAccess::cast<VariableAddressNode>(handle_).getFrom();
        }

        FieldAddressView::FieldAddressView(AddrHandle handle) : handle_(handle) {
            if (!handle_.isFieldAddress())
                ERROR("FieldAddressView requires a field address.");
        }

        std::optional<FieldAddressView> FieldAddressView::tryFrom(AddrHandle handle) {
            if (!handle.isFieldAddress())
                return std::nullopt;
            return FieldAddressView{handle};
        }

        std::optional<FieldAddressView> FieldAddressView::tryFrom(ExprHandle handle) {
            auto address = detail::AddrHandle::tryFrom(handle);
            return address ? tryFrom(*address) : std::nullopt;
        }

        utils::not_null<const clang::RecordDecl *> FieldAddressView::definition() const {
            return detail::HandleAccess::cast<FieldAddressNode>(handle_).getDefinition();
        }

        AddrHandle FieldAddressView::base() const {
            return detail::HandleAccess::cast<FieldAddressNode>(handle_).getBaseAddr();
        }

        size_t FieldAddressView::fieldIndex() const {
            return detail::HandleAccess::cast<FieldAddressNode>(handle_).getFieldIndex();
        }

        std::optional<utils::not_null<const clang::VarDecl *>> FieldAddressView::fromRoot() const {
            return handle_.getFromRoot();
        }
    } // namespace detail

    VariableAddress::VariableAddress(const Addr &address) : Addr(address) {
        if (!isVariableAddress())
            ERROR("VariableAddress requires a variable address.");
    }

    std::optional<VariableAddress> VariableAddress::tryFrom(const Addr &address) {
        if (!address.isVariableAddress())
            return std::nullopt;
        return VariableAddress{address};
    }

    utils::not_null<const clang::VarDecl *> VariableAddress::declaration() const {
        return detail::VariableAddressView{FacadeAccess::addressHandle(*this)}.declaration();
    }

    FieldAddress::FieldAddress(const Addr &address) : Addr(address) {
        if (!isFieldAddress())
            ERROR("FieldAddress requires a field address.");
    }

    std::optional<FieldAddress> FieldAddress::tryFrom(const Addr &address) {
        if (!address.isFieldAddress())
            return std::nullopt;
        return FieldAddress{address};
    }

    utils::not_null<const clang::RecordDecl *> FieldAddress::definition() const {
        return detail::FieldAddressView{FacadeAccess::addressHandle(*this)}.definition();
    }

    Addr FieldAddress::base() const {
        auto handle = detail::FieldAddressView{FacadeAccess::addressHandle(*this)}.base();
        return FacadeAccess::makeAddress(factory(), handle);
    }

    size_t FieldAddress::fieldIndex() const {
        return detail::FieldAddressView{FacadeAccess::addressHandle(*this)}.fieldIndex();
    }

    namespace detail {
        SymbolAddressView::SymbolAddressView(AddrHandle handle) : handle_(handle) {
            if (!handle_.isSymbolAddress())
                ERROR("SymbolAddressView requires a symbol address.");
        }

        std::optional<SymbolAddressView> SymbolAddressView::tryFrom(AddrHandle handle) {
            if (!handle.isSymbolAddress())
                return std::nullopt;
            return SymbolAddressView{handle};
        }

        std::optional<SymbolAddressView> SymbolAddressView::tryFrom(ExprHandle handle) {
            auto address = detail::AddrHandle::tryFrom(handle);
            return address ? tryFrom(*address) : std::nullopt;
        }

        clang::QualType SymbolAddressView::pointeeType() const { return handle_.getPointeeType(); }

        std::optional<AddrHandle> SymbolAddressView::from() const {
            return detail::HandleAccess::cast<SymbolAddressNode>(handle_).getFromAddrHandle();
        }

        std::optional<SourcePoint> SymbolAddressView::fromPoint() const {
            return detail::HandleAccess::cast<SymbolAddressNode>(handle_).getFromPoint();
        }

        ExprHandle SymbolAddressView::offset() const {
            return detail::HandleAccess::cast<SymbolAddressNode>(handle_).getOffset();
        }

        std::optional<ExprHandle> SymbolAddressView::length() const {
            const auto &length = detail::HandleAccess::cast<SymbolAddressNode>(handle_).getLength();
            if (!length)
                return std::nullopt;
            return *length;
        }

        std::optional<ExprHandle> SymbolAddressView::rightBound() const {
            return detail::HandleAccess::cast<SymbolAddressNode>(handle_).getRightBound();
        }

        SymbolAddrBaseInfo SymbolAddressView::baseInfo() const {
            return detail::HandleAccess::cast<SymbolAddressNode>(handle_).getBaseInfo();
        }

        std::optional<utils::not_null<const clang::VarDecl *>> SymbolAddressView::fromRoot() const {
            return handle_.getFromRoot();
        }

        int SymbolAddressView::dimension() const { return handle_.getDimension(); }
    } // namespace detail

    SymbolAddress::SymbolAddress(const Addr &address) : Addr(address) {
        if (!isSymbolAddress())
            ERROR("SymbolAddress requires a symbol address.");
    }

    std::optional<SymbolAddress> SymbolAddress::tryFrom(const Addr &address) {
        if (!address.isSymbolAddress())
            return std::nullopt;
        return SymbolAddress{address};
    }

    clang::QualType SymbolAddress::pointeeType() const {
        return detail::SymbolAddressView{FacadeAccess::addressHandle(*this)}.pointeeType();
    }

    std::optional<Addr> SymbolAddress::from() const {
        auto origin = detail::SymbolAddressView{FacadeAccess::addressHandle(*this)}.from();
        if (!origin)
            return std::nullopt;
        return FacadeAccess::makeAddress(factory(), *origin);
    }

    std::optional<SourcePoint> SymbolAddress::fromPoint() const {
        return detail::SymbolAddressView{FacadeAccess::addressHandle(*this)}.fromPoint();
    }

    Expr SymbolAddress::offset() const {
        auto handle = detail::SymbolAddressView{FacadeAccess::addressHandle(*this)}.offset();
        return FacadeAccess::makeExpr(factory(), handle);
    }

    std::optional<Expr> SymbolAddress::length() const {
        auto rangeLength = detail::SymbolAddressView{FacadeAccess::addressHandle(*this)}.length();
        if (!rangeLength)
            return std::nullopt;
        return FacadeAccess::makeExpr(factory(), *rangeLength);
    }

    std::optional<Expr> SymbolAddress::rightBound() const {
        auto bound = detail::SymbolAddressView{FacadeAccess::addressHandle(*this)}.rightBound();
        if (!bound)
            return std::nullopt;
        return FacadeAccess::makeExpr(factory(), *bound);
    }

    SymbolAddrBaseInfo SymbolAddress::baseInfo() const {
        return detail::SymbolAddressView{FacadeAccess::addressHandle(*this)}.baseInfo();
    }

    std::string StructureInfo::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        std::string structName = definition_->getNameAsString();
        uint64_t sizeBits      = static_cast<uint64_t>(layout_.getSize().getQuantity()) * 8;

        oss << type("Struct") << "(" << accent(structName) << ", " << key("size") << "="
            << lit(std::to_string(sizeBits)) << " " << hint("bits") << ")";
        return oss.str();
    }

    detail::StructureView::StructureView(ExprHandle handle) : handle_(handle) {
        if (!handle_.isStructure())
            ERROR("StructureView requires a Structure expression.");
    }

    std::optional<detail::StructureView> detail::StructureView::tryFrom(ExprHandle handle) {
        if (!handle.isStructure())
            return std::nullopt;
        return StructureView{handle};
    }

    size_t detail::StructureView::size() const {
        return detail::HandleAccess::cast<StructureNode>(handle_).getNumFields();
    }

    ExprHandle detail::StructureView::field(size_t index) const {
        return detail::HandleAccess::cast<StructureNode>(handle_).getFieldValue(index);
    }

    const StructureInfo &detail::StructureView::info() const {
        return detail::HandleAccess::cast<StructureNode>(handle_).getInfo();
    }

    std::optional<SourcePoint> detail::StructureView::fromPoint() const {
        return detail::HandleAccess::cast<StructureNode>(handle_).getFromPoint();
    }

    StructureExpr::StructureExpr(const Expr &expression) : Expr(expression) {
        if (!isStructure())
            ERROR("StructureExpr requires a structure expression.");
    }

    std::optional<StructureExpr> StructureExpr::tryFrom(const Expr &expression) {
        if (!expression.isStructure())
            return std::nullopt;
        return StructureExpr{expression};
    }

    size_t StructureExpr::size() const {
        return detail::StructureView{FacadeAccess::exprHandle(*this)}.size();
    }

    Expr StructureExpr::field(size_t index) const {
        auto handle = detail::StructureView{FacadeAccess::exprHandle(*this)}.field(index);
        return FacadeAccess::makeExpr(factory(), handle);
    }

    const StructureInfo &StructureExpr::info() const {
        return detail::StructureView{FacadeAccess::exprHandle(*this)}.info();
    }

    std::optional<SourcePoint> StructureExpr::fromPoint() const {
        return detail::StructureView{FacadeAccess::exprHandle(*this)}.fromPoint();
    }

    std::string StructureNode::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;

        oss << info_.dump();
        oss << ", " << key("fields") << "=[";
        for (size_t i = 0; i < fields_.size(); ++i) {
            oss << fields_[i].dump();
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

    utils::expected<std::string, ACSLError> detail::LiteralExprNode::doGetACSL(
        const ACSLConfig &,
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

    utils::expected<std::string, ACSLError> detail::BinaryOpExprNode::doGetACSL(
        const ACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned parentPrec,
        bool isRightChild) const {
        using Op = detail::BinaryOpExprNode::Operator;

        auto emitBoolCmp = [&](const detail::SymbolicExprNode &boolExpr,
                               bool expectTrue) -> utils::expected<std::string, ACSLError> {
            auto boolStr =
                callGetACSL(boolExpr, config, usedPoints, currentPoint, getPrecedence(op_), false);
            if (!boolStr)
                return boolStr.error();
            if (expectTrue)
                return boolStr.value();
            return std::string("!(") + boolStr.value() + ")";
        };

        if (op_ == Op::Equal || op_ == Op::NotEqual) {
            auto trySimplify = [&](const detail::SymbolicExprNode &lhs,
                                   const detail::SymbolicExprNode &rhs)
                -> std::optional<utils::expected<std::string, ACSLError>> {
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

            if (auto r = trySimplify(detail::HandleAccess::node(left_),
                                     detail::HandleAccess::node(right_)))
                return *r;
            if (auto r = trySimplify(detail::HandleAccess::node(right_),
                                     detail::HandleAccess::node(left_)))
                return *r;
        }

        std::ostringstream oss;
        std::string opStr;
        switch (op_) {
#define BIN_OP(name, tok, prec, isRight)                                                           \
    case Operator::name:                                                                           \
        opStr = tok;                                                                               \
        break;
#include "operators.def"
            default: UNREACHABLE();
        }

        auto myPrec     = getPrecedence(op_);
        bool needParens = (myPrec < parentPrec) ||
                          (myPrec == parentPrec && isRightChild && !isRightAssociative(op_));

        auto leftStr = callGetACSL(detail::HandleAccess::node(left_), config, usedPoints,
                                   currentPoint, myPrec, false);
        if (!leftStr)
            return leftStr.error();
        auto rightStr = callGetACSL(detail::HandleAccess::node(right_), config, usedPoints,
                                    currentPoint, myPrec, true);
        if (!rightStr)
            return rightStr.error();
        oss << (needParens ? "(" : "") << leftStr.value() << " " << opStr << " " << rightStr.value()
            << (needParens ? ")" : "");
        return oss.str();
    }

    utils::expected<std::string, ACSLError> detail::UnaryOpExprNode::doGetACSL(
        const ACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned parentPrec,
        bool isRightChild) const {
        std::ostringstream oss;
        std::string opStr;
        switch (op_) {
#define UN_OP(name, tok, prec, isRight)                                                            \
    case Operator::name:                                                                           \
        opStr = tok;                                                                               \
        break;
#include "operators.def"
            default: UNREACHABLE();
        }

        auto myPrec     = getPrecedence(op_);
        bool needParens = (myPrec < parentPrec) ||
                          (myPrec == parentPrec && isRightChild && !isRightAssociative(op_));

        if (op_ == Operator::PostInc || op_ == Operator::PostDec) {
            auto subStr = callGetACSL(detail::HandleAccess::node(expr_), config, usedPoints,
                                      currentPoint, myPrec, false);
            if (!subStr)
                return subStr.error();
            oss << (needParens ? "(" : "") << subStr.value() << opStr << (needParens ? ")" : "");
        } else {
            auto subStr = callGetACSL(detail::HandleAccess::node(expr_), config, usedPoints,
                                      currentPoint, myPrec, true);
            if (!subStr)
                return subStr.error();
            oss << (needParens ? "(" : "") << opStr << subStr.value() << (needParens ? ")" : "");
        }
        return oss.str();
    }

    utils::expected<std::string, ACSLError> detail::CastExprNode::doGetACSL(
        const ACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned,
        bool) const {
        const auto target = getValType();
        std::string typeName;
        switch (target.kind) {
            case ExprScalarKind::Bool: typeName = "_Bool"; break;
            case ExprScalarKind::Int:
                if (target.bitWidth == 8)
                    typeName = "signed char";
                else if (target.bitWidth == 16)
                    typeName = "short";
                else if (target.bitWidth == 32)
                    typeName = "int";
                else if (target.bitWidth == 64)
                    typeName = "long long";
                break;
            case ExprScalarKind::UInt:
                if (target.bitWidth == 8)
                    typeName = "unsigned char";
                else if (target.bitWidth == 16)
                    typeName = "unsigned short";
                else if (target.bitWidth == 32)
                    typeName = "unsigned int";
                else if (target.bitWidth == 64)
                    typeName = "unsigned long long";
                break;
            case ExprScalarKind::Void:
            case ExprScalarKind::Structure: break;
        }
        if (typeName.empty())
            ERROR("CastExpr has an unsupported target type.");

        auto operand =
            callGetACSL(detail::HandleAccess::node(operand_), config, usedPoints, currentPoint);
        if (!operand)
            return operand.error();
        return "(" + typeName + ")(" + operand.value() + ")";
    }

    utils::expected<std::string, ACSLError> detail::UnknownExprNode::doGetACSL(
        const ACSLConfig &config,
        std::unordered_set<SourcePoint> &,
        std::optional<SourcePoint>,
        unsigned,
        bool) const {
        if (!config.UnknownExprAsError) {
            WARN("Output UnknownExpr's ACSL, something may go wrong.");
            return std::string{"{Unknown}"};
        }
        return ACSLError::UnknownExpr;
    }

    utils::expected<std::string, ACSLError> SymbolValueNode::doGetACSL(
        const ACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned parentPrec,
        bool isRightChild) const {
        auto [prefix, suffix] =
            details::getPrefixSuffixAndUpdateMap(config, usedPoints, currentPoint, fromPoint_);
        bool hasAt    = !(prefix.empty() || suffix.empty());
        auto valueStr = callGetACSLOfValueProxy(
            detail::HandleAccess::node(fromAddr_), config, usedPoints, fromPoint_,
            hasAt ? /* enclosed in \\at() */ 0 : parentPrec, hasAt ? false : isRightChild);
        if (!valueStr)
            return valueStr.error();

        return prefix + std::move(valueStr.value()) + suffix;
    }

    utils::expected<std::string, ACSLError> SymbolAddressNode::doGetACSL(
        const ACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned parentPrec,
        bool isRightChild) const {
        if (length_)
            ERROR("Address range has no regularForm but regularFormOfValue.");
        if (fromAddr_ == std::nullopt)
            return ACSLError::HeapAddress;

        auto offsetStr = callGetACSL(detail::HandleAccess::node(offset_), config, usedPoints,
                                     currentPoint, getPrecedence(Operator::Add), true);
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
                return callGetACSLOfValue(detail::HandleAccess::node(fromAddr_.value()), config,
                                          usedPoints, fromPoint_,
                                          /* enclosed in \\at() */ 0, false);
            if (hasOffset)
                return callGetACSLOfValue(detail::HandleAccess::node(fromAddr_.value()), config,
                                          usedPoints, fromPoint_, getPrecedence(Operator::Add),
                                          false);
            return callGetACSLOfValue(detail::HandleAccess::node(fromAddr_.value()), config,
                                      usedPoints, fromPoint_, parentPrec, isRightChild);
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

    utils::expected<std::string, ACSLError> VariableAddressNode::doGetACSL(
        const ACSLConfig &,
        std::unordered_set<SourcePoint> &,
        std::optional<SourcePoint>,
        unsigned parentPrec,
        bool isRightChild) const {
        bool needParens = details::isNeedParens(Operator::AddrOf, parentPrec, isRightChild);
        return (needParens ? "(" : "") + std::string{"&"} + from_->getNameAsString() +
               (needParens ? ")" : "");
    }

    utils::expected<std::string, ACSLError> FieldAddressNode::doGetACSL(
        const ACSLConfig &config,
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

    utils::expected<std::string, ACSLError> SymbolAddressNode::doGetACSLOfValue(
        const ACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned parentPrec,
        bool isRightChild) const {
        if (fromAddr_ == std::nullopt)
            return ACSLError::HeapAddress;

        auto [prefix, suffix] =
            details::getPrefixSuffixAndUpdateMap(config, usedPoints, currentPoint, fromPoint_);
        bool hasAt = !(prefix.empty() || suffix.empty());

        auto nameStrExpected =
            callGetACSLOfValue(detail::HandleAccess::node(fromAddr_.value()), config, usedPoints,
                               fromPoint_, hasAt ? 0 : getPrecedence(Operator::Subscript), false);
        if (!nameStrExpected)
            return nameStrExpected.error();

        auto nameStr = prefix + std::move(nameStrExpected.value()) + suffix;

        if (length_ == std::nullopt) {
            auto offsetStr = callGetACSL(detail::HandleAccess::node(offset_), config, usedPoints,
                                         currentPoint, /* enclosed in [] */ 0, false);
            if (!offsetStr)
                return offsetStr.error();

            auto useDeref = offsetStr.value() == "0" && config.useDerefWithZeroOffset;
            std::string subedAddrStr;

            if (useDeref) {
                auto recalcNameStrExpected = callGetACSLOfValue(
                    detail::HandleAccess::node(fromAddr_.value()), config, usedPoints, fromPoint_,
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
        auto offsetStr = callGetACSL(detail::HandleAccess::node(offset_), config, usedPoints,
                                     currentPoint, rangePrec, false);
        if (!offsetStr)
            return offsetStr.error();

        auto &factory = ExprFactoryScope::current();

        // offset + length - 1
        auto offsetPlusLength = detail::ExprFactoryInternals::simplifiedBinary(
            factory, detail::ExprFactoryInternals::importExpr(factory, getOffset()),
            detail::BinaryOpExprNode::Operator::Add,
            detail::ExprFactoryInternals::importExpr(factory, *length_));
        auto rightBound = detail::ExprFactoryInternals::simplifiedBinary(
            factory, offsetPlusLength, detail::BinaryOpExprNode::Operator::Subtract,
            detail::ExprFactoryInternals::literal(factory, int64_t{1}));
        auto rightBoundStr = callGetACSL(detail::HandleAccess::node(rightBound), config, usedPoints,
                                         currentPoint, rangePrec, true);
        if (!rightBoundStr)
            return rightBoundStr.error();

        auto subedAddrStr = std::move(nameStr) + "[" + std::move(offsetStr.value()) + " .. " +
                            std::move(rightBoundStr.value()) + "]";

        bool needParens = details::isNeedParens(Operator::Subscript, parentPrec, isRightChild);
        return (needParens ? "(" : "") + std::move(subedAddrStr) + (needParens ? ")" : "");
    }

    utils::expected<std::string, ACSLError> VariableAddressNode::doGetACSLOfValue(
        const ACSLConfig &,
        std::unordered_set<SourcePoint> &,
        std::optional<SourcePoint>,
        unsigned,
        bool) const {
        return from_->getNameAsString();
    }

    utils::expected<std::string, ACSLError> FieldAddressNode::doGetACSLOfValue(
        const ACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned parentPrec,
        bool isRightChild) const {
        auto baseStr =
            callGetACSLOfValue(detail::HandleAccess::node(baseAddr_), config, usedPoints,
                               currentPoint, getPrecedence(Operator::MemberAccess), false);
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
                auto *symbol =
                    detail::HandleAccess::dynCast<const Symbol>(structure.getFieldValue(index));
                if (symbol == nullptr)
                    return std::nullopt;

                auto origin = getBorrowedSymbolOrigin(*symbol);
                if (!origin)
                    return std::nullopt;
                auto *fieldAddr = detail::HandleAccess::dynCast<FieldAddressNode>(origin->address);
                if (fieldAddr == nullptr || fieldAddr->getFieldIndex() != index)
                    return std::nullopt;

                SymbolOrigin fieldOrigin{fieldAddr->getBaseAddr(), origin->point};
                if (!common) {
                    common = fieldOrigin;
                    continue;
                }
                if (!common->address.structurallyEqual(fieldOrigin.address) ||
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

    std::optional<Addr> Expr::sourceAddress() const {
        auto *symbol = detail::HandleAccess::dynCast<Symbol>(handle());
        if (symbol == nullptr)
            return std::nullopt;
        auto origin = getBorrowedSymbolOrigin(*symbol);
        if (!origin)
            return std::nullopt;
        return Addr{factory(),
                    detail::ExprFactoryInternals::importAddress(factory(), origin->address)};
    }

    bool Expr::isFrom(const Addr &address, const SourcePoint &point) const {
        if (&factory() != &address.factory())
            ERROR("Cannot compare symbol origin across different factories.");

        auto *symbol = detail::HandleAccess::dynCast<Symbol>(handle());
        if (symbol == nullptr)
            return false;
        auto symbolPoint = symbol->getFromPoint();
        if (!symbolPoint || *symbolPoint != point)
            return false;
        auto origin = getBorrowedSymbolOrigin(*symbol);
        return origin && FacadeAccess::addressHandle(address).structurallyEqual(origin->address);
    }

    utils::expected<std::string, ACSLError> StructureNode::doGetACSL(
        const ACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned parentPrec,
        bool isRightChild) const {
        auto from = getStructureOrigin(*this);
        if (from == std::nullopt)
            return ACSLError::PartiallyModifiedStruct;

        auto &[fromAddr, fromPoint] = from.value();
        auto [prefix, suffix] =
            details::getPrefixSuffixAndUpdateMap(config, usedPoints, currentPoint, fromPoint);
        bool hasAt    = !(prefix.empty() || suffix.empty());
        auto valueStr = callGetACSLOfValueProxy(
            detail::HandleAccess::node(fromAddr), config, usedPoints, fromPoint,
            hasAt ? /* enclosed in \\at() */ 0 : parentPrec, hasAt ? false : isRightChild);
        if (!valueStr)
            return valueStr.error();

        return prefix + std::move(valueStr.value()) + suffix;
    }

    const detail::LiteralExprNode *detail::LiteralExprNode::evalToConstExpr() const {
        auto &factory = ExprFactoryScope::current();
        switch (type_) {
            case LiteralType::Boolean:
                return literalNode(detail::ExprFactoryInternals::literal(factory, data_.boolValue));
            case LiteralType::Int:
                return literalNode(detail::ExprFactoryInternals::literal(factory, data_.intValue));
            case LiteralType::UnsignedInt:
                return literalNode(detail::ExprFactoryInternals::literal(factory, data_.uintValue));
            case LiteralType::Short:
                return literalNode(
                    detail::ExprFactoryInternals::literal(factory, data_.shortValue));
            case LiteralType::UnsignedShort:
                return literalNode(
                    detail::ExprFactoryInternals::literal(factory, data_.ushortValue));
            case LiteralType::Int64:
                return literalNode(
                    detail::ExprFactoryInternals::literal(factory, data_.int64Value));
            case LiteralType::UInt64:
                return literalNode(
                    detail::ExprFactoryInternals::literal(factory, data_.uint64Value));
        }
        return nullptr;
    }

    const detail::LiteralExprNode *detail::UnaryOpExprNode::evalToConstExpr() const {
        auto C = evaluateToLiteralNode(detail::HandleAccess::node(expr_));
        if (!C)
            return nullptr;

        using Op    = detail::UnaryOpExprNode::Operator;
        auto vt     = expr_.getValType();
        unsigned bw = std::max(vt.bitWidth ? vt.bitWidth : 32u, 32u);

        switch (op_) {
            case Op::LogicalNot: {
                bool r = !literalAsBool(*C);
                return literalNode(
                    detail::ExprFactoryInternals::literal(ExprFactoryScope::current(), r));
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
                llvm::APInt value{bw, literalRawU(*C)};
                if (vt.kind == ScalarKind::UInt) {
                    return makeLiteralFromUnifiedType({ScalarKind::UInt, bw}, false,
                                                      (-value).getZExtValue());
                }
                if (value.isMinSignedValue())
                    return nullptr;
                return makeLiteralFromUnifiedType({ScalarKind::Int, bw}, false,
                                                  (-value).getZExtValue());
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

        auto Lc = evaluateToLiteralNode(detail::HandleAccess::node(left_));
        if (!Lc)
            return nullptr;

        if (op_ == BO::LogicalAnd) {
            if (!literalAsBool(*Lc))
                return literalNode(
                    detail::ExprFactoryInternals::literal(ExprFactoryScope::current(), false));
            auto Rc = evaluateToLiteralNode(detail::HandleAccess::node(right_));
            if (!Rc)
                return nullptr;
            return literalNode(detail::ExprFactoryInternals::literal(ExprFactoryScope::current(),
                                                                     literalAsBool(*Rc)));
        }
        if (op_ == BO::LogicalOr) {
            if (literalAsBool(*Lc))
                return literalNode(
                    detail::ExprFactoryInternals::literal(ExprFactoryScope::current(), true));
            auto Rc = evaluateToLiteralNode(detail::HandleAccess::node(right_));
            if (!Rc)
                return nullptr;
            return literalNode(detail::ExprFactoryInternals::literal(ExprFactoryScope::current(),
                                                                     literalAsBool(*Rc)));
        }

        auto Rc = evaluateToLiteralNode(detail::HandleAccess::node(right_));
        if (!Rc)
            return nullptr;

        auto tgt = unify(left_.getValType(), right_.getValType());
        if (tgt.kind == ScalarKind::Void) {
            if (op_ == BO::Equal || op_ == BO::NotEqual) {
                auto emitBool = [](bool b) {
                    return literalNode(
                        detail::ExprFactoryInternals::literal(ExprFactoryScope::current(), b));
                };
                bool eq = (literalRawU(*Lc) == literalRawU(*Rc));
                return emitBool(op_ == BO::Equal ? eq : !eq);
            }
            return nullptr;
        }
        unsigned bw = tgt.bitWidth ? tgt.bitWidth : 64;

        auto emitBool = [](bool b) {
            return literalNode(
                detail::ExprFactoryInternals::literal(ExprFactoryScope::current(), b));
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
            llvm::APInt leftBits{bw, static_cast<uint64_t>(L)};
            llvm::APInt rightBits{bw, static_cast<uint64_t>(R)};

            switch (op_) {
                case BO::Equal: return emitBool(L == R);
                case BO::NotEqual: return emitBool(L != R);
                case BO::LessThan: return emitBool(L < R);
                case BO::LessEqual: return emitBool(L <= R);
                case BO::GreaterThan: return emitBool(L > R);
                case BO::GreaterEqual: return emitBool(L >= R);

                case BO::Add: {
                    bool overflow = false;
                    auto result   = leftBits.sadd_ov(rightBits, overflow);
                    if (overflow)
                        return nullptr;
                    return makeLiteralFromUnifiedType(tgt, false, result.getZExtValue());
                }
                case BO::Subtract: {
                    bool overflow = false;
                    auto result   = leftBits.ssub_ov(rightBits, overflow);
                    if (overflow)
                        return nullptr;
                    return makeLiteralFromUnifiedType(tgt, false, result.getZExtValue());
                }
                case BO::Multiply: {
                    bool overflow = false;
                    auto result   = leftBits.smul_ov(rightBits, overflow);
                    if (overflow)
                        return nullptr;
                    return makeLiteralFromUnifiedType(tgt, false, result.getZExtValue());
                }
                case BO::Divide: {
                    if (R == 0)
                        return nullptr;
                    bool overflow = false;
                    auto result   = leftBits.sdiv_ov(rightBits, overflow);
                    if (overflow)
                        return nullptr;
                    return makeLiteralFromUnifiedType(tgt, false, result.getZExtValue());
                }
                case BO::Remainder: {
                    if (R == 0)
                        return nullptr;
                    bool overflow = false;
                    (void)leftBits.sdiv_ov(rightBits, overflow);
                    if (overflow)
                        return nullptr;
                    return makeLiteralFromUnifiedType(tgt, false,
                                                      leftBits.srem(rightBits).getZExtValue());
                }

                case BO::ShiftLeft: {
                    if (R < 0 || static_cast<uint64_t>(R) >= bw || L < 0)
                        return nullptr;
                    bool overflow = false;
                    auto result   = leftBits.sshl_ov(static_cast<unsigned>(R), overflow);
                    if (overflow)
                        return nullptr;
                    return makeLiteralFromUnifiedType(tgt, false, result.getZExtValue());
                }
                case BO::ShiftRight:
                    if (R < 0 || static_cast<uint64_t>(R) >= bw || L < 0)
                        return nullptr;
                    return makeLiteralFromUnifiedType(
                        tgt, false, leftBits.lshr(static_cast<unsigned>(R)).getZExtValue());

                case BO::BitAnd:
                    return makeLiteralFromUnifiedType(tgt, false,
                                                      (leftBits & rightBits).getZExtValue());
                case BO::BitOr:
                    return makeLiteralFromUnifiedType(tgt, false,
                                                      (leftBits | rightBits).getZExtValue());
                case BO::BitXor:
                    return makeLiteralFromUnifiedType(tgt, false,
                                                      (leftBits ^ rightBits).getZExtValue());

                default: return nullptr;
            }
        }
    }

    namespace {
        const detail::LiteralExprNode *evaluateToLiteralNode(const detail::SymbolicExprNode &expr) {
            if (auto *literal = dyn_cast<const detail::LiteralExprNode>(&expr))
                return literal->evalToConstExpr();
            if (auto *unary = dyn_cast<const detail::UnaryOpExprNode>(&expr))
                return unary->evalToConstExpr();
            if (auto *binary = dyn_cast<const detail::BinaryOpExprNode>(&expr))
                return binary->evalToConstExpr();
            return nullptr;
        }
    } // namespace

    std::optional<int64_t> detail::SymbolicExprNode::tryEvalToConstant() const {
        auto *literal = evaluateToLiteralNode(*this);
        if (literal == nullptr)
            return std::nullopt;
        return literal->getLiteralValue();
    }

    bool detail::LiteralExprNode::equal(const detail::SymbolicExprNode &expr) const {
        const auto liter = dyn_cast<const detail::LiteralExprNode>(&expr);
        if (!liter)
            return false;
        if (getValType() != expr.getValType())
            return false;

        return type_ == liter->type_ && getLiteralValue() == liter->getLiteralValue();
    }

    bool detail::BinaryOpExprNode::equal(const detail::SymbolicExprNode &expr) const {
        const auto binary = dyn_cast<const detail::BinaryOpExprNode>(&expr);
        if (!binary)
            return false;
        if (getValType() != expr.getValType())
            return false;

        return left_.structurallyEqual(binary->left_) && op_ == binary->op_ &&
               right_.structurallyEqual(binary->right_);
    }

    bool detail::UnaryOpExprNode::equal(const detail::SymbolicExprNode &expr) const {
        const auto unary = dyn_cast<const detail::UnaryOpExprNode>(&expr);
        if (!unary)
            return false;
        if (getValType() != expr.getValType())
            return false;

        return op_ == unary->op_ && expr_.structurallyEqual(unary->expr_);
    }

    bool detail::CastExprNode::equal(const detail::SymbolicExprNode &expr) const {
        const auto cast = dyn_cast<const detail::CastExprNode>(&expr);
        return cast && getValType() == cast->getValType() &&
               operand_.structurallyEqual(cast->operand_);
    }

    bool detail::UnknownExprNode::equal(const detail::SymbolicExprNode &expr) const {
        return expr.isUnknown() && getValType() == expr.getValType();
    }

    bool SymbolValueNode::equal(const detail::SymbolicExprNode &expr) const {
        const auto symbolValue = dyn_cast<const SymbolValueNode>(&expr);
        if (!symbolValue)
            return false;
        if (getValType() != expr.getValType())
            return false;

        if (fromPoint_ != symbolValue->fromPoint_)
            return false;

        return fromAddr_.structurallyEqual(symbolValue->fromAddr_);
    }

    bool SymbolAddressNode::equal(const detail::SymbolicExprNode &expr) const {
        auto other = dyn_cast<const SymbolAddressNode>(&expr);
        if (!other)
            return false;
        if (getValType() != expr.getValType())
            return false;

        // compare from
        if (fromAddr_ && other->fromAddr_ && !fromAddr_->structurallyEqual(*other->fromAddr_)) {
            return false;
        }

        if ((other->fromAddr_ == std::nullopt) ^ (fromAddr_ == std::nullopt))
            return false;

        if (fromPoint_ != other->fromPoint_)
            return false;

        // compare offset
        // todo: Need a `offsetEqual`, here is not correct now.
        auto &factory = ExprFactoryScope::current();
        if (!simplifyHandle(factory, offset_)
                 .structurallyEqual(simplifyHandle(factory, other->getOffset()))) {
            return false;
        }

        // compare range
        if (length_ != std::nullopt && other->length_ != std::nullopt) {
            if (!length_->structurallyEqual(*other->length_))
                return false;
        } else if ((length_ == std::nullopt) ^ (other->length_ == std::nullopt)) {
            return false;
        }
        return true;
    }

    bool VariableAddressNode::equal(const detail::SymbolicExprNode &expr) const {
        auto other = dyn_cast<const VariableAddressNode>(&expr);
        if (!other)
            return false;
        if (getValType() != expr.getValType())
            return false;

        return from_ == other->from_;
    }

    bool FieldAddressNode::equal(const detail::SymbolicExprNode &expr) const {
        auto other = dyn_cast<const FieldAddressNode>(&expr);
        if (!other)
            return false;
        if (getValType() != expr.getValType())
            return false;

        return baseAddr_.structurallyEqual(other->baseAddr_) && fieldIndex_ == other->fieldIndex_;
    }

    std::optional<utils::not_null<const clang::VarDecl *>> SymbolAddressNode::getFromRoot() const {
        if (fromAddr_ == std::nullopt)
            return std::nullopt;
        return fromAddr_.value().getFromRoot();
    }

    std::optional<utils::not_null<const clang::VarDecl *>> SymbolAddrBaseInfo::getFromRoot() const {
        if (fromAddr_ == std::nullopt)
            return std::nullopt;
        return fromAddr_.value().getFromRoot();
    }

    std::optional<Addr> SymbolAddrBaseInfo::fromAddress(ExprFactory &factory) const {
        if (!fromAddr_)
            return std::nullopt;
        return fromAddr_->importedInto(factory);
    }

    std::optional<utils::not_null<const clang::VarDecl *>> VariableAddressNode::getFromRoot() const {
        return from_;
    }

    std::optional<utils::not_null<const clang::VarDecl *>> FieldAddressNode::getFromRoot() const {
        return baseAddr_.getFromRoot();
    }

    std::optional<utils::not_null<const clang::VarDecl *>> SymbolValueNode::getFromRoot() const {
        return fromAddr_.getFromRoot();
    }

    bool StructureInfo::equal(const StructureInfo &other) const {
        if (definition_ != other.definition_)
            return false;
        return true;
    }

    bool StructureInfo::operator==(const StructureInfo &other) const { return equal(other); }

    bool StructureNode::equal(const detail::SymbolicExprNode &expr) const {
        const auto st = dyn_cast<const StructureNode>(&expr);
        if (!st)
            return false;
        if (getValType() != expr.getValType())
            return false;
        if (!info_.equal(st->info_))
            return false;
        return std::ranges::equal(fields_, st->fields_,
                                  [](auto lhs, auto rhs) { return lhs.structurallyEqual(rhs); });
    }

    detail::SymbolicExprNode::UsedSet SymbolValueNode::collectUsedSymbols() const {
        return {selfHandle()};
    }

    detail::SymbolicExprNode::UsedSet detail::BinaryOpExprNode::collectUsedSymbols() const {
        auto leftSymbols  = left_.collectUsedSymbols();
        auto rightSymbols = right_.collectUsedSymbols();
        leftSymbols.insert(rightSymbols.begin(), rightSymbols.end());
        return leftSymbols;
    }

    detail::SymbolicExprNode::UsedSet detail::UnaryOpExprNode::collectUsedSymbols() const {
        return expr_.collectUsedSymbols();
    }

    detail::SymbolicExprNode::UsedSet detail::CastExprNode::collectUsedSymbols() const {
        return operand_.collectUsedSymbols();
    }

    detail::SymbolicExprNode::UsedSet SymbolAddressNode::collectUsedSymbols() const {
        if (length_)
            ERROR("Address range is solely for address representation and should not be "
                  "used as an expression.");
        return {selfHandle()};
    }

    SymbolAddressNode::SymbolAddressNode(const clang::QualType pointeeType,
                                         std::optional<AddrHandle> from,
                                         SourcePoint fromPoint,
                                         ExprHandle offset,
                                         std::optional<ExprHandle> length,
                                         std::optional<Type> explicitType)
        : detail::AddressNode(detail::SymbolicExprNode::ExprKind::K_SymbolAddress,
                              ExprType{ExprScalarKind::UInt, 64},
                              pointeeType,
                              explicitType),
          Symbol(Kind::K_SymbolAddress), offset_(offset), fromAddr_(from),
          fromPoint_(std::move(fromPoint)), length_(length) {}

    size_t SymbolAddrBaseInfo::hash() const {
        return utils::hash_val(fromPoint_.hash(), fromAddr_ ? fromAddr_.value().hash() : 0);
    }

    std::optional<ExprHandle> SymbolAddressNode::getRightBound() const {
        // Not sure return which one is better, offset_+1 or nullopt.
        if (length_ == std::nullopt)
            return std::nullopt;
        auto &factory = ExprFactoryScope::current();
        return detail::ExprFactoryInternals::binary(
            factory, detail::ExprFactoryInternals::importExpr(factory, offset_),
            detail::BinaryOpExprNode::Operator::Add,
            detail::ExprFactoryInternals::importExpr(factory, *length_));
    }

    SymbolAddrBaseInfo SymbolAddressNode::getBaseInfo() const {
        if (fromAddr_ == std::nullopt)
            return SymbolAddrBaseInfo{std::nullopt, fromPoint_, pointeeType_};
        return SymbolAddrBaseInfo{FacadeAccess::makeAddressBox(getFromAddrHandle().value()),
                                  fromPoint_, pointeeType_};
    }

    int SymbolAddressNode::getDimension() const {
        if (fromAddr_ == std::nullopt)
            return -1;
        if (auto dim = fromAddr_.value().getDimension(); dim >= 0)
            return dim + 1;
        return -1;
    }

    int VariableAddressNode::getDimension() const { return 0; }

    int FieldAddressNode::getDimension() const { return baseAddr_.getDimension(); }

    StructureNode::StructureNode(StructureInfo info,
                                 std::vector<ExprHandle> fields,
                                 std::optional<Type> explicitType)
        : detail::SymbolicExprNode(
              ExprKind::K_Structure,
              Type{ScalarKind::Structure,
                   static_cast<unsigned>(info.layout_.getSize().getQuantity()) *
                       8 /*By default, char is 8-bit.*/},
              explicitType),
          Symbol(Kind::K_Structure), info_(info) {
        if (fields.size() != info_.layout_.getFieldCount())
            ERROR("Structure field count mismatch");
        fields_.reserve(fields.size());
        for (auto field : fields)
            fields_.emplace_back(field);
    }

    std::ostream &operator<<(std::ostream &os, detail::SymbolicExprNode::ExprKind t) {
        switch (t) {
            using enum detail::SymbolicExprNode::ExprKind;
            case K_LiteralExpr: os << "Literal"; break;
            case K_SymbolValue: os << "SymbolValue"; break;
            case K_SymbolAddress: os << "SymbolAddr"; break;
            case K_VariableAddress: os << "VariableAddr"; break;
            case K_FieldAddress: os << "FieldAddr"; break;
            case K_BinaryOpExpr: os << "BinaryOp"; break;
            case K_UnaryOpExpr: os << "UnaryOp"; break;
            case K_CastExpr: os << "Cast"; break;
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

    detail::BinaryOpExprNode::Operator getCompoundAssignOp(
        clang::BinaryOperatorKind compoundAssignOp) {
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

    ExprType deriveType(clang::QualType type) {
        if (auto atomic = type->getAs<clang::AtomicType>())
            return deriveType(atomic->getValueType());
        if (auto ptr = type->getAs<clang::PointerType>())
            return deriveType(ptr->getPointeeType());
        return llvm::TypeSwitch<clang::QualType, ExprType>(type.getCanonicalType())
            .Case([](const clang::BuiltinType *BT) -> ExprType {
                using Kind = ExprScalarKind;
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
            .Case([](const clang::EnumType * /*ET*/) -> ExprType {
                return {ExprScalarKind::Int, 32};
            })
            .Default([&](clang::QualType QT) -> ExprType {
                if (QT->isStructureType()) {
                    return {ExprScalarKind::Structure, 0};
                }
                UNIMPLEMENT("Unsupported non-builtin type: " << QT.getAsString());
            });
    }

    std::optional<SourcePoint> StructureNode::getFromPoint() const {
        auto origin = getStructureOrigin(*this);
        if (!origin)
            return std::nullopt;
        return origin->point;
    }

    namespace {
        Expr makeSymbol(ExprFactory &factory,
                        clang::QualType type,
                        std::optional<Addr> fromAddr,
                        SourcePoint fromPoint) {
            if (type->isPointerType()) {
                auto pointerType = llvm::cast<clang::PointerType>(type);
                auto addr        = fromAddr ? Addr::symbol(pointerType->getPointeeType(), *fromAddr,
                                                           std::move(fromPoint))
                                            : Addr::symbol(factory, pointerType->getPointeeType(),
                                                           std::move(fromPoint));
                return addr.asExpr();
            }

            if (type->isArrayType()) {
                auto arrayType = llvm::cast<clang::ArrayType>(type);
                auto addr =
                    fromAddr
                        ? Addr::symbol(arrayType->getElementType(), *fromAddr, std::move(fromPoint))
                        : Addr::symbol(factory, arrayType->getElementType(), std::move(fromPoint));
                return addr.asExpr();
            }

            if (type->isStructureType()) {
                if (!fromAddr)
                    ERROR("Structure should *from* an `Address`.");
                auto *RD = type->getAsRecordDecl();
                if (!RD || !RD->isCompleteDefinition())
                    ERROR("Incomplete struct definition");
                RD           = RD->getDefinition();
                auto &layout = RD->getASTContext().getASTRecordLayout(RD);
                return FacadeAccess::makeExpr(factory, detail::ExprFactoryInternals::structure(
                                                           factory, RD, layout,
                                                           FacadeAccess::addressHandle(*fromAddr),
                                                           std::move(fromPoint)));
            }

            if (!fromAddr)
                ERROR("SymbolValue should *from* an `Address`.");
            return Expr::symbolValue(deriveType(type), *fromAddr, std::move(fromPoint));
        }
    } // namespace

    Expr Expr::symbol(clang::QualType type, const Addr &from, SourcePoint fromPoint) {
        return makeSymbol(from.factory(), type, from, std::move(fromPoint));
    }

    Expr Expr::symbol(clang::QualType type, SourcePoint fromPoint) {
        auto &factory = ExprFactoryScope::current();
        return makeSymbol(factory, type, std::nullopt, std::move(fromPoint));
    }

    utils::expected<std::string, ACSLError> detail::Symbol::callGetACSLOfValueProxy(
        const detail::AddressNode &addr,
        const ACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned parentPrec,
        bool isRightChild) {
        return detail::AddressNode::callGetACSLOfValue(addr, config, usedPoints, currentPoint,
                                                       parentPrec, isRightChild);
    }

} // namespace acslg::analyzer::symbolic
