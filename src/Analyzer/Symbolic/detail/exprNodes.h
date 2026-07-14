#pragma once

#include "../expr.h"

namespace acslg::analyzer::symbolic::detail {

    /// Internal node representing a literal constant value.
    class LiteralExprNode : public SymbolicExpr {
      public:
        enum class LiteralType {
            Boolean,
            Int,
            UnsignedInt,
            Short,
            UnsignedShort,
            Int64,
            UInt64
        };

        LiteralExprNode(const LiteralExprNode &) = delete;

      private:
        friend class ::acslg::analyzer::symbolic::ExprFactory;

        LiteralExprNode(bool value, std::optional<Type> explicitType = std::nullopt)
            : SymbolicExpr(ExprKind::K_LiteralExpr, {ScalarKind::Bool, 1}, explicitType),
              type_(LiteralType::Boolean) {
            data_.boolValue = value;
        }

        LiteralExprNode(int value, std::optional<Type> explicitType = std::nullopt)
            : SymbolicExpr(ExprKind::K_LiteralExpr, {ScalarKind::Int, 32}, explicitType),
              type_(LiteralType::Int) {
            data_.intValue = value;
        }

        LiteralExprNode(unsigned int value, std::optional<Type> explicitType = std::nullopt)
            : SymbolicExpr(ExprKind::K_LiteralExpr, {ScalarKind::UInt, 32}, explicitType),
              type_(LiteralType::UnsignedInt) {
            data_.uintValue = value;
        }

        LiteralExprNode(short value, std::optional<Type> explicitType = std::nullopt)
            : SymbolicExpr(ExprKind::K_LiteralExpr, {ScalarKind::Int, 16}, explicitType),
              type_(LiteralType::Short) {
            data_.shortValue = value;
        }

        LiteralExprNode(unsigned short value, std::optional<Type> explicitType = std::nullopt)
            : SymbolicExpr(ExprKind::K_LiteralExpr, {ScalarKind::UInt, 16}, explicitType),
              type_(LiteralType::UnsignedShort) {
            data_.ushortValue = value;
        }

        LiteralExprNode(int64_t value, std::optional<Type> explicitType = std::nullopt)
            : SymbolicExpr(ExprKind::K_LiteralExpr, {ScalarKind::Int, 64}, explicitType),
              type_(LiteralType::Int64) {
            data_.int64Value = value;
        }

        LiteralExprNode(uint64_t value, std::optional<Type> explicitType = std::nullopt)
            : SymbolicExpr(ExprKind::K_LiteralExpr, {ScalarKind::UInt, 64}, explicitType),
              type_(LiteralType::UInt64) {
            data_.uint64Value = value;
        }

      public:
        static bool classof(const SymbolicExpr *expr) {
            return expr->getKind() == ExprKind::K_LiteralExpr;
        }

        LiteralType getLiteralType() const { return type_; }
        ExprHandle importInto(ExprFactory &factory) const;
        std::unique_ptr<LiteralExprNode> rebuildNode(
            std::optional<Type> explicitType = std::nullopt) const;

        std::string dump() const override;
        std::size_t hash() const override;
        const LiteralExprNode *evalToConstExpr() const override;
        bool equal(const SymbolicExpr &expr) const override;
        bool isLinear() const override { return true; }
        int getMaxDegree() const override { return 0; }
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const std::unordered_map<size_t, size_t> &) const override;
        int64_t getLiteralValue() const;

      private:
        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

        LiteralType type_;
        union Data {
            bool boolValue;
            int intValue;
            unsigned int uintValue;
            short shortValue;
            unsigned short ushortValue;
            int64_t int64Value;
            uint64_t uint64Value;

            Data() {}
            ~Data() {}
        } data_;
    };

    /// Internal node representing a binary operation expression.
    class BinaryOpExprNode : public SymbolicExpr {
      public:
        using Operator = BinaryOp;

        inline static unsigned getPrecedence(Operator op) {
            switch (op) {
#define BIN_OP(name, tok, prec, isRightAssoc) case Operator::name: return prec;
#include "../operators.def"
                default: ERROR("Unknown Operator");
            }
        }

        inline static bool isRightAssociative(Operator op) {
            switch (op) {
#define BIN_OP(name, tok, prec, isRightAssoc) case Operator::name: return isRightAssoc;
#include "../operators.def"
                default: ERROR("Unknown operator");
            }
        }

        static bool classof(const SymbolicExpr *expr) {
            return expr->getKind() == ExprKind::K_BinaryOpExpr;
        }

        utils::not_null<const SymbolicExpr *> getLeft() const { return left_.get(); }
        utils::not_null<const SymbolicExpr *> getRight() const { return right_.get(); }
        Operator getOperator() const { return op_; }

        std::string dump() const override;
        std::size_t hash() const override;
        const LiteralExprNode *evalToConstExpr() const override;
        bool equal(const SymbolicExpr &expr) const override;
        bool isUnknown() const override { return left_->isUnknown() || right_->isUnknown(); }
        UsedMap collectUsedSymbols() const override;
        bool isLinear() const override;
        int getMaxDegree() const override;
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const std::unordered_map<size_t, size_t> &) const override;

      private:
        friend class ::acslg::analyzer::symbolic::ExprFactory;

        BinaryOpExprNode(ExprHandle left,
                         Operator op,
                         ExprHandle right,
                         std::optional<Type> explicitType = std::nullopt)
            : SymbolicExpr(ExprKind::K_BinaryOpExpr, left->getValType(), explicitType), left_(left),
              op_(op), right_(right) {}

        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

        ExprChild left_;
        Operator op_;
        ExprChild right_;
    };

    /// Internal node representing a unary operation expression.
    class UnaryOpExprNode : public SymbolicExpr {
      public:
        using Operator = UnaryOp;

        inline static unsigned getPrecedence(Operator op) {
            switch (op) {
#define UN_OP(name, tok, prec, isRightAssoc) case Operator::name: return prec;
#include "../operators.def"
                default: ERROR("Unknown Operator");
            }
        }

        inline static bool isRightAssociative(Operator op) {
            switch (op) {
#define UN_OP(name, tok, prec, isRightAssoc) case Operator::name: return isRightAssoc;
#include "../operators.def"
                default: ERROR("Unknown operator");
            }
        }

        static bool classof(const SymbolicExpr *expr) {
            return expr->getKind() == ExprKind::K_UnaryOpExpr;
        }

        utils::not_null<const SymbolicExpr *> getSub() const { return expr_.get(); }
        Operator getOperator() const { return op_; }

        std::string dump() const override;
        std::size_t hash() const override;
        const LiteralExprNode *evalToConstExpr() const override;
        bool equal(const SymbolicExpr &expr) const override;
        bool isUnknown() const override { return expr_->isUnknown(); }
        UsedMap collectUsedSymbols() const override;
        bool isLinear() const override;
        int getMaxDegree() const override;
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const std::unordered_map<size_t, size_t> &) const override;

      private:
        friend class ::acslg::analyzer::symbolic::ExprFactory;

        UnaryOpExprNode(Operator op,
                        ExprHandle expr,
                        std::optional<Type> explicitType = std::nullopt)
            : SymbolicExpr(ExprKind::K_UnaryOpExpr, expr->getValType(), explicitType), op_(op),
              expr_(expr) {}

        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

        Operator op_;
        ExprChild expr_;
    };

    /// Internal node representing a symbolic value that cannot be modeled.
    class UnknownExprNode : public SymbolicExpr {
      public:
        ~UnknownExprNode() = default;

        static bool classof(const SymbolicExpr *expr) {
            return expr->getKind() == ExprKind::K_UnknownExpr;
        }

        std::string dump() const override;
        std::size_t hash() const override;
        bool equal(const SymbolicExpr &expr) const override;
        bool isUnknown() const override { return true; }
        bool isLinear() const override { return false; }
        int getMaxDegree() const override { return 0; }

      private:
        friend class ::acslg::analyzer::symbolic::ExprFactory;

        explicit UnknownExprNode(std::optional<Type> explicitType = std::nullopt)
            : SymbolicExpr(ExprKind::K_UnknownExpr, {ScalarKind::Void, 0}, explicitType) {}

        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;
    };

} // namespace acslg::analyzer::symbolic::detail
