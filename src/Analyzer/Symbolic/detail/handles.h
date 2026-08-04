#pragma once

#include "../expr.h"

namespace acslg::analyzer::symbolic::detail {

    class ExprHandle;
    struct ExprHandleIdentityHash {
        std::size_t operator()(ExprHandle expression) const;
    };
    using ExprHandleSet = std::unordered_set<ExprHandle, ExprHandleIdentityHash>;
    using ExprHandleIndexMap =
        std::unordered_map<ExprHandle, std::size_t, ExprHandleIdentityHash>;

    class ExprHandle {
      public:
        ExprHandle(const ExprHandle &)            = default;
        ExprHandle &operator=(const ExprHandle &) = default;

        std::size_t hash() const;
        std::string dump() const;
        ExprType getValType() const;
        bool structurallyEqual(ExprHandle other) const;
        bool isUnknown() const;
        bool isLiteralExpr() const;
        bool isUnaryExpr() const;
        bool isBinaryExpr() const;
        bool isCastExpr() const;
        bool isStructure() const;
        bool isSymbolValue() const;
        bool isSymbolAddress() const;
        bool isVariableAddress() const;
        bool isFieldAddress() const;
        bool isRangeIndex() const;
        bool isSumOverRange() const;
        bool isQuantifierOverRange() const;
        bool isMaxMinOverRange() const;
        bool isOverRange() const;
        bool isLinear() const;
        int getMaxDegree() const;
        ExprHandleSet collectUsedSymbols() const;
        std::optional<int64_t> tryEvalAsConstant() const;
        std::optional<int64_t> tryEvalToConstant() const;
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &varIndexMap) const;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const ExprHandleIndexMap &expressionIndexMap) const;
        utils::expected<std::pair<std::string, std::unordered_set<SourcePoint>>, ACSLError> getACSL(
            const ACSLConfig &config,
            std::optional<SourcePoint> currentPoint = std::nullopt) const;

        friend bool operator==(ExprHandle lhs, ExprHandle rhs) {
            return lhs.expr_.get() == rhs.expr_.get();
        }

      private:
        friend class AddrHandle;
        friend class ::acslg::analyzer::symbolic::ExprFactory;
        friend struct HandleAccess;
        friend class SymbolicExprNode;

        explicit ExprHandle(const SymbolicExprNode *expr)
            : ExprHandle(utils::not_null<const SymbolicExprNode *>{expr}) {}
        explicit ExprHandle(utils::not_null<const SymbolicExprNode *> expr) : expr_(expr) {}

        utils::not_null<const SymbolicExprNode *> expr_;
    };

    inline std::size_t ExprHandleIdentityHash::operator()(ExprHandle expression) const {
        return expression.hash();
    }

    class AddrHandle {
      public:
        AddrHandle(const AddrHandle &)            = default;
        AddrHandle &operator=(const AddrHandle &) = default;

        static std::optional<AddrHandle> tryFrom(ExprHandle handle);

        ExprHandle asExpr() const;

        std::size_t hash() const;
        std::string dump() const;
        ExprType getValType() const;
        bool structurallyEqual(AddrHandle other) const;
        bool isSymbolAddress() const;
        bool isVariableAddress() const;
        bool isFieldAddress() const;
        std::optional<utils::not_null<const clang::VarDecl *>> getFromRoot() const;
        int getDimension() const;
        const clang::QualType &getPointeeType() const;
        utils::expected<std::pair<std::string, std::unordered_set<SourcePoint>>, ACSLError> getACSLOfValue(
            const ACSLConfig &config,
            std::optional<SourcePoint> currentPoint = std::nullopt) const;

        friend bool operator==(AddrHandle lhs, AddrHandle rhs) { return lhs.ptr_ == rhs.ptr_; }

      private:
        friend class ::acslg::analyzer::symbolic::AddressBox;
        friend struct HandleAccess;

        explicit AddrHandle(const AddressNode *ptr) : ptr_(ptr) {
            if (ptr_ == nullptr)
                ERROR("AddrHandle cannot wrap null.");
        }

        const AddressNode *ptr_;
    };

} // namespace acslg::analyzer::symbolic::detail
