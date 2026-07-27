#pragma once

#include "handles.h"

namespace acslg::analyzer::symbolic::detail {

    class LiteralExprView {
      public:
        explicit LiteralExprView(ExprHandle handle);

        static std::optional<LiteralExprView> tryFrom(ExprHandle handle);

        ExprHandle handle() const { return handle_; }
        int64_t value() const;

      private:
        ExprHandle handle_;
    };

    class UnaryExprView {
      public:
        explicit UnaryExprView(ExprHandle handle);

        static std::optional<UnaryExprView> tryFrom(ExprHandle handle);

        ExprHandle handle() const { return handle_; }
        UnaryOp operation() const;
        ExprHandle operand() const;

      private:
        ExprHandle handle_;
    };

    class BinaryExprView {
      public:
        explicit BinaryExprView(ExprHandle handle);

        static std::optional<BinaryExprView> tryFrom(ExprHandle handle);

        ExprHandle handle() const { return handle_; }
        BinaryOp operation() const;
        ExprHandle left() const;
        ExprHandle right() const;

      private:
        ExprHandle handle_;
    };

    class StructureView {
      public:
        explicit StructureView(ExprHandle handle);

        static std::optional<StructureView> tryFrom(ExprHandle handle);

        ExprHandle handle() const { return handle_; }
        size_t size() const;
        ExprHandle field(size_t index) const;
        const StructureInfo &info() const;
        std::optional<SourcePoint> fromPoint() const;

        friend bool operator==(StructureView lhs, StructureView rhs) {
            return lhs.handle_ == rhs.handle_;
        }

      private:
        ExprHandle handle_;
    };

    class VariableAddressView {
      public:
        explicit VariableAddressView(AddrHandle handle);

        static std::optional<VariableAddressView> tryFrom(AddrHandle handle);
        static std::optional<VariableAddressView> tryFrom(ExprHandle handle);

        AddrHandle handle() const { return handle_; }
        utils::not_null<const clang::VarDecl *> declaration() const;

      private:
        AddrHandle handle_;
    };

    class FieldAddressView {
      public:
        explicit FieldAddressView(AddrHandle handle);

        static std::optional<FieldAddressView> tryFrom(AddrHandle handle);
        static std::optional<FieldAddressView> tryFrom(ExprHandle handle);

        AddrHandle handle() const { return handle_; }
        utils::not_null<const clang::RecordDecl *> definition() const;
        AddrHandle base() const;
        size_t fieldIndex() const;
        std::optional<utils::not_null<const clang::VarDecl *>> fromRoot() const;

      private:
        AddrHandle handle_;
    };

    class SymbolAddressView {
      public:
        inline static constexpr signed long ZERO_OFFSET = 0;

        explicit SymbolAddressView(AddrHandle handle);

        static std::optional<SymbolAddressView> tryFrom(AddrHandle handle);
        static std::optional<SymbolAddressView> tryFrom(ExprHandle handle);

        AddrHandle handle() const { return handle_; }
        clang::QualType pointeeType() const;
        std::optional<AddrHandle> from() const;
        std::optional<SourcePoint> fromPoint() const;
        ExprHandle offset() const;
        std::optional<ExprHandle> length() const;
        std::optional<ExprHandle> rightBound() const;
        SymbolAddrBaseInfo baseInfo() const;
        std::optional<utils::not_null<const clang::VarDecl *>> fromRoot() const;
        int dimension() const;

      private:
        AddrHandle handle_;
    };

    class SymbolValueView {
      public:
        explicit SymbolValueView(ExprHandle handle);

        static std::optional<SymbolValueView> tryFrom(ExprHandle handle);

        ExprHandle handle() const { return handle_; }
        AddrHandle from() const;
        std::optional<SourcePoint> fromPoint() const;
        std::optional<utils::not_null<const clang::VarDecl *>> fromRoot() const;

      private:
        ExprHandle handle_;
    };

} // namespace acslg::analyzer::symbolic::detail
