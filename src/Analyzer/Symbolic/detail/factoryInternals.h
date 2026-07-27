#pragma once

#include "handleBridge.h"
#include "nodeBase.h"

namespace acslg::analyzer::symbolic::detail {

    struct ExprFactoryInternals {
        static size_t size(const ExprFactory &factory);

        static ExprHandle importExpr(ExprFactory &factory, ExprHandle expression);
        static AddrHandle importAddress(ExprFactory &factory, AddrHandle address);

        static ExprHandle literal(ExprFactory &factory, bool value);
        static ExprHandle literal(ExprFactory &factory, int value);
        static ExprHandle literal(ExprFactory &factory, unsigned int value);
        static ExprHandle literal(ExprFactory &factory, short value);
        static ExprHandle literal(ExprFactory &factory, unsigned short value);
        static ExprHandle literal(ExprFactory &factory, int64_t value);
        static ExprHandle literal(ExprFactory &factory, uint64_t value);
        static ExprHandle unknown(ExprFactory &factory);
        static ExprHandle rangeIndex(ExprFactory &factory, std::string_view name);

        static AddrHandle variableAddress(ExprFactory &factory,
                                          utils::not_null<const clang::VarDecl *> variable);
        static AddrHandle fieldAddress(ExprFactory &factory,
                                       clang::QualType pointeeType,
                                       const clang::RecordDecl *record,
                                       AddrHandle base,
                                       size_t fieldIndex);
        static AddrHandle symbolAddress(ExprFactory &factory,
                                        clang::QualType pointeeType,
                                        std::optional<AddrHandle> from,
                                        SourcePoint fromPoint,
                                        std::optional<ExprHandle> offset = std::nullopt,
                                        std::optional<ExprHandle> length = std::nullopt);
        static AddrHandle withOffset(ExprFactory &factory, AddrHandle address, ExprHandle offset);
        static AddrHandle withAddedOffset(ExprFactory &factory,
                                          AddrHandle address,
                                          ExprHandle extra);
        static AddrHandle withSubtractedOffset(ExprFactory &factory,
                                               AddrHandle address,
                                               ExprHandle extra);
        static AddrHandle withLength(ExprFactory &factory, AddrHandle address, ExprHandle length);
        static AddrHandle withAddedLength(ExprFactory &factory,
                                          AddrHandle address,
                                          ExprHandle extra);
        static AddrHandle withoutLength(ExprFactory &factory, AddrHandle address);

        static ExprHandle symbolValue(ExprFactory &factory,
                                      ExprType type,
                                      AddrHandle from,
                                      SourcePoint fromPoint);
        static ExprHandle structure(ExprFactory &factory,
                                    const clang::RecordDecl *record,
                                    const clang::ASTRecordLayout &layout,
                                    AddrHandle from,
                                    SourcePoint fromPoint);
        static ExprHandle withField(ExprFactory &factory,
                                    ExprHandle structure,
                                    size_t index,
                                    ExprHandle value);
        static ExprHandle unary(ExprFactory &factory, UnaryOp op, ExprHandle expression);
        static ExprHandle binary(ExprFactory &factory,
                                 ExprHandle left,
                                 BinaryOp op,
                                 ExprHandle right);
        static ExprHandle simplifiedBinary(ExprFactory &factory,
                                           ExprHandle left,
                                           BinaryOp op,
                                           ExprHandle right);
        static ExprHandle withValType(ExprFactory &factory, ExprHandle expression, ExprType type);

        template <typename Node, typename... Args>
        static std::unique_ptr<Node> makeNode(Args &&...args) {
            return std::unique_ptr<Node>{new Node(std::forward<Args>(args)...)};
        }

        static ExprHandle intern(ExprFactory &factory,
                                 utils::not_null<std::unique_ptr<SymbolicExprNode>> node);
    };

} // namespace acslg::analyzer::symbolic::detail
