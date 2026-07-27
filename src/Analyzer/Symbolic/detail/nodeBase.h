#pragma once

#include "handles.h"

namespace acslg::analyzer::symbolic::detail {

    /// Internal abstract base for all symbolic expression nodes.
    class SymbolicExprNode {
      public:
        enum class ExprKind : uint16_t {
            K_FirstAddr,
            K_SymbolAddress,
            K_VariableAddress,
            K_FieldAddress,
            K_LastAddr,

            K_LiteralExpr,
            K_SymbolValue,
            K_Structure,
            K_BinaryOpExpr,
            K_UnaryOpExpr,
            K_UnknownExpr,

            K_RangeIndex,
            K_FirstOverRange,
            K_SumOverRange,
            K_QuantifierOverRange,
            K_MaxMinOverRange,
            K_LastOverRange
        };

        virtual ~SymbolicExprNode()                           = default;
        SymbolicExprNode(const SymbolicExprNode &)            = delete;
        SymbolicExprNode &operator=(const SymbolicExprNode &) = delete;
        SymbolicExprNode(SymbolicExprNode &&)                 = default;
        SymbolicExprNode &operator=(SymbolicExprNode &&)      = delete;

        ExprKind getKind() const { return kind_; }
        ExprType getValType() const { return valueType_; }

        virtual std::string dump() const = 0;

        utils::expected<std::pair<std::string, std::unordered_set<SourcePoint>>, ACSLError> getACSL(
            const ACSLConfig &config,
            std::optional<SourcePoint> currentPoint = std::nullopt) const {
            std::unordered_set<SourcePoint> usedPoints;
            auto res = callGetACSL(*this, config, usedPoints, currentPoint);
            if (res)
                return std::pair{std::move(res.value()), std::move(usedPoints)};
            return res.error();
        }

        virtual bool equal(const SymbolicExprNode &) const = 0;
        virtual std::size_t hash() const                   = 0;

        friend std::ostream &operator<<(std::ostream &os, const SymbolicExprNode &expr) {
            return os << expr.dump();
        }

        friend bool operator==(const SymbolicExprNode &lhs, const SymbolicExprNode &rhs) {
            if (lhs.kind_ != rhs.kind_)
                return false;
            return lhs.equal(rhs);
        }

        virtual ExprHandle simplifiedExpr() const;
        ExprHandle simplifiedExprIfLinear() const;

        using UsedSet = ExprHandleSet;
        virtual UsedSet collectUsedSymbols() const { return {}; }

        std::optional<int64_t> tryEvalAsConstant() const;
        std::optional<int64_t> tryEvalToConstant() const;

        virtual bool isUnknown() const { return false; }

        bool isLiteralExpr() const { return kind_ == ExprKind::K_LiteralExpr; }
        bool isUnaryExpr() const { return kind_ == ExprKind::K_UnaryOpExpr; }
        bool isBinaryExpr() const { return kind_ == ExprKind::K_BinaryOpExpr; }
        bool isStructure() const { return kind_ == ExprKind::K_Structure; }
        bool isSymbolValue() const { return kind_ == ExprKind::K_SymbolValue; }
        bool isSymbolAddress() const { return kind_ == ExprKind::K_SymbolAddress; }
        bool isVariableAddress() const { return kind_ == ExprKind::K_VariableAddress; }
        bool isFieldAddress() const { return kind_ == ExprKind::K_FieldAddress; }
        bool isRangeIndex() const { return kind_ == ExprKind::K_RangeIndex; }
        bool isSumOverRange() const { return kind_ == ExprKind::K_SumOverRange; }
        bool isQuantifierOverRange() const { return kind_ == ExprKind::K_QuantifierOverRange; }
        bool isMaxMinOverRange() const { return kind_ == ExprKind::K_MaxMinOverRange; }

        bool isOverRange() const {
            return kind_ > ExprKind::K_FirstOverRange && kind_ < ExprKind::K_LastOverRange;
        }

        virtual bool isLinear() const    = 0;
        virtual int getMaxDegree() const = 0;

        virtual std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const {
            ERROR("not implemented for expression type: ");
        }

        virtual Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const ExprHandleIndexMap &) const {
            ERROR("not implemented for expression type: ");
        }

      protected:
        SymbolicExprNode(ExprKind kind,
                         ExprType naturalType,
                         std::optional<ExprType> explicitType = std::nullopt)
            : kind_(kind), valueType_(explicitType.value_or(naturalType)) {}

        ExprHandle selfHandle() const;

        static utils::expected<std::string, ACSLError> callGetACSL(
            const SymbolicExprNode &expr,
            const ACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec = 0,
            bool isRightChild   = false) {
            return expr.doGetACSL(config, usedPoints, currentPoint, parentPrec, isRightChild);
        }

      private:
        virtual utils::expected<std::string, ACSLError> doGetACSL(
            const ACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const = 0;

        ExprKind kind_;
        ExprType valueType_;
    };

} // namespace acslg::analyzer::symbolic::detail

namespace acslg::analyzer::symbolic {
    std::ostream &operator<<(std::ostream &os, detail::SymbolicExprNode::ExprKind kind);
}
