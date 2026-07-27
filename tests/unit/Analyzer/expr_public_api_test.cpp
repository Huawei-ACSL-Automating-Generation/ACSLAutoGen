#include "Analyzer/Symbolic/expr.h"

#include <type_traits>
#include <utility>

template <typename T>
concept CompleteType = requires {
    sizeof(T);
};

template <typename T>
concept ExposesRawBaseAddress = requires(T &value) {
    value.fromAddr_;
};

template <typename T>
concept ExposesBaseFacade = requires(T &value, acslg::analyzer::symbolic::ExprFactory &factory) {
    value.fromAddress(factory);
    value.fromPoint();
    value.pointeeType();
};

template <typename T>
concept ExposesHandle = requires(const T &value) {
    value.handle();
};

template <typename T>
concept ExposesHandleExpression = requires(const T &value) {
    value.asExpr();
};

template <typename T>
concept ExposesInternPoolSize = requires(const T &value) {
    value.size();
};

template <typename T>
concept AcceptsHashSubstitutionKey = requires(
    T &substitutions,
    std::size_t hash,
    const acslg::analyzer::symbolic::Expr &replacement) {
    substitutions.insertOrAssign(hash, replacement);
};

template <typename T>
concept ExposesImportBuilders = requires(T &value,
                                         acslg::analyzer::symbolic::detail::ExprHandle expression,
                                         acslg::analyzer::symbolic::detail::AddrHandle address) {
    value.importExpr(expression);
    value.importAddress(address);
};

template <typename T>
concept ExposesLeafBuilders = requires(T &value) {
    value.literal(1);
    value.unknown();
    value.rangeIndex("i");
};

template <typename T>
concept ExposesOperationBuilders = requires(T &value,
                                            acslg::analyzer::symbolic::detail::ExprHandle expression,
                                            acslg::analyzer::symbolic::ExprType type) {
    value.unary(acslg::analyzer::symbolic::UnaryOp::Minus, expression);
    value.binary(expression, acslg::analyzer::symbolic::BinaryOp::Add, expression);
    value.simplifiedBinary(expression, acslg::analyzer::symbolic::BinaryOp::Add, expression);
    value.withValType(expression, type);
};

template <typename T>
concept ExposesSymbolBuilders = requires(T &value,
                                         acslg::analyzer::symbolic::detail::ExprHandle expression,
                                         acslg::analyzer::symbolic::detail::AddrHandle address,
                                         acslg::analyzer::symbolic::ExprType type,
                                         const clang::RecordDecl *record,
                                         const clang::ASTRecordLayout &layout,
                                         acslg::analyzer::symbolic::SourcePoint point) {
    value.symbolValue(type, address, point);
    value.structure(record, layout, address, point);
    value.withField(expression, 0, expression);
};

template <typename T>
concept ExposesBasicAddressBuilders =
    requires(T &value,
             acslg::utils::not_null<const clang::VarDecl *> variable,
             clang::QualType type,
             const clang::RecordDecl *record,
             acslg::analyzer::symbolic::detail::AddrHandle address) {
    value.variableAddress(variable);
    value.fieldAddress(type, record, address, 0);
};

template <typename T>
concept ExposesRangeAddressBuilders =
    requires(T &value,
             clang::QualType type,
             acslg::analyzer::symbolic::detail::AddrHandle address,
             acslg::analyzer::symbolic::detail::ExprHandle expression,
             acslg::analyzer::symbolic::SourcePoint point) {
    value.symbolAddress(type, address, point, expression, expression);
    value.withOffset(address, expression);
    value.withAddedOffset(address, expression);
    value.withSubtractedOffset(address, expression);
    value.withLength(address, expression);
    value.withAddedLength(address, expression);
    value.withoutLength(address);
};

static_assert(!CompleteType<acslg::analyzer::symbolic::detail::SymbolicExprNode>);
static_assert(!CompleteType<acslg::analyzer::symbolic::detail::AddressNode>);
static_assert(!CompleteType<acslg::analyzer::symbolic::detail::ExprHandle>);
static_assert(!CompleteType<acslg::analyzer::symbolic::detail::AddrHandle>);
static_assert(!CompleteType<acslg::analyzer::symbolic::detail::ExprFactoryBackend>);
static_assert(!ExposesRawBaseAddress<acslg::analyzer::symbolic::SymbolAddrBaseInfo>);
static_assert(ExposesBaseFacade<acslg::analyzer::symbolic::SymbolAddrBaseInfo>);
static_assert(!ExposesHandle<acslg::analyzer::symbolic::Expr>);
static_assert(!ExposesHandle<acslg::analyzer::symbolic::Addr>);
static_assert(!ExposesHandle<acslg::analyzer::symbolic::AddressBox>);
static_assert(!ExposesHandleExpression<acslg::analyzer::symbolic::AddressBox>);
static_assert(!ExposesInternPoolSize<acslg::analyzer::symbolic::ExprFactory>);
static_assert(!ExposesImportBuilders<acslg::analyzer::symbolic::ExprFactory>);
static_assert(!ExposesLeafBuilders<acslg::analyzer::symbolic::ExprFactory>);
static_assert(!ExposesOperationBuilders<acslg::analyzer::symbolic::ExprFactory>);
static_assert(!ExposesSymbolBuilders<acslg::analyzer::symbolic::ExprFactory>);
static_assert(!ExposesBasicAddressBuilders<acslg::analyzer::symbolic::ExprFactory>);
static_assert(!ExposesRangeAddressBuilders<acslg::analyzer::symbolic::ExprFactory>);
static_assert(!AcceptsHashSubstitutionKey<acslg::analyzer::symbolic::ExprSubstitutions>);
static_assert(std::is_same_v<
              decltype(std::declval<const acslg::analyzer::symbolic::Expr &>()
                           .collectUsedSymbols()),
              acslg::analyzer::symbolic::ExprSet>);
using IdentityLinearizer = Parma_Polyhedra_Library::Linear_Expression (
    acslg::analyzer::symbolic::Expr::*)(
    const acslg::analyzer::symbolic::ExprIndexMap &) const;
static_assert(std::is_same_v<
              decltype(static_cast<IdentityLinearizer>(
                  &acslg::analyzer::symbolic::Expr::toLinearExpr)),
              IdentityLinearizer>);
static_assert(std::is_trivially_copyable_v<acslg::analyzer::symbolic::Expr>);
static_assert(std::is_trivially_copyable_v<acslg::analyzer::symbolic::Addr>);
static_assert(sizeof(acslg::analyzer::symbolic::Expr) == 2 * sizeof(void *));
static_assert(sizeof(acslg::analyzer::symbolic::Addr) == 2 * sizeof(void *));
static_assert(sizeof(acslg::analyzer::symbolic::ExprFactory) == sizeof(void *));
