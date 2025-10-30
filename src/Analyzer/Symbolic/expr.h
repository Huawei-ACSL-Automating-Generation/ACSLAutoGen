/// \file symbolic.h
/// @brief Declarations for symbolic expression hierarchy and utilities.
#ifndef __ACSLG_SRC_ANALYZER_SYMBOLIC_EXPR_H__
#define __ACSLG_SRC_ANALYZER_SYMBOLIC_EXPR_H__

#include <clang/AST/Type.h>
#include <string>
#include <memory>
#include <span>
#include <ranges>
#include <algorithm>
#include <string_view>
#include <type_traits>
#include <ppl.hh>
#include <clang/AST/Decl.h>
#include <clang/AST/Expr.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/RecordLayout.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Lexer.h>
#include <unordered_set>

#include "macros.h"
#include "Utils/utils.h"
#include "config.h"

namespace acslg::analyzer {
    class Path;
}

namespace acslg::analyzer::symbolic {
    class Address;
    class SymbolAddress;
    class SymbolValue;
    class LiteralExpr;
    class Symbol;
    struct SymbolAddrBaseInfo;

    /**
     * @class SourcePoint
     * @brief Represents a unique source position in the source code.
     *
     * This class encapsulates a `clang::SourceLocation` together with its associated
     * `SourceManager`, and provides utilities for constructing points relative to statements,
     * comparing positions, generating hash values, and dumping human-readable information.
     *
     * This class is designed to express a program location in the source text, not a control flow
     * node.
     */
    class SourcePoint {
      public:
        SourcePoint(const SourcePoint &) = default;
        SourcePoint &operator=(const SourcePoint &);
        SourcePoint(SourcePoint &&) = default;
        SourcePoint &operator=(SourcePoint &&);

        /**
         * @brief Construct a SourcePoint at the start of a given FunctionDecl's body.
         *
         * @param FD  The FunctionDecl to reference.
         * @param SM The SourceManager providing context for the source file.
         * @param LO The language options used for retrieving locations.
         * @return A SourcePoint located at the start of the function body.
         */
        static SourcePoint fromFuncDecl(const clang::FunctionDecl *FD,
                                        const clang::SourceManager &SM,
                                        const clang::LangOptions &LO);

        /**
         * @brief Construct a SourcePoint at the position just before a given statement.
         *
         * @param S  The statement to reference.
         * @param SM The SourceManager providing context for the source file.
         * @param LO The language options used for retrieving locations.
         * @return A SourcePoint located before the given statement.
         */
        static SourcePoint fromStmtBefore(const clang::Stmt *S,
                                          const clang::SourceManager &SM,
                                          const clang::LangOptions &LO);

        /**
         * @brief Construct a SourcePoint at the position just after a given statement.
         *
         * @param S  The statement to reference.
         * @param SM The SourceManager providing context for the source file.
         * @param LO The language options used for retrieving locations.
         * @return A SourcePoint located after the given statement.
         */
        static SourcePoint fromStmtAfter(const clang::Stmt *S,
                                         const clang::SourceManager &SM,
                                         const clang::LangOptions &LO);

        /**
         * @brief Compare this SourcePoint with another.
         *
         * @param other The SourcePoint to compare against.
         * @return True if this point is strictly before the other, false otherwise.
         */
        bool operator<(const SourcePoint &other) const;

        /**
         * @brief Test equality between two SourcePoints.
         *
         * Two points are equal if their underlying `SourceLocation`s compare equal
         * under the same SourceManager.
         *
         * @param other The SourcePoint to compare against.
         * @return True if both points represent the same location, false otherwise.
         */
        bool operator==(const SourcePoint &other) const;

        /**
         * @brief Get the underlying `clang::SourceLocation` represented by this point.
         *
         * @return An `clang::SourceLocation`.
         */
        clang::SourceLocation asSourceLocation() const { return loc_; }

        /**
         * @brief Generate a hash value for this SourcePoint.
         *
         * Computed from the underlying `SourceLocation`'s hash value.
         *
         * @return Hash value suitable for use in unordered containers.
         */
        size_t hash() const { return utils::hash_val(loc_.getHashValue()); }

        /**
         * @brief Dump a human-readable string representation of the SourcePoint.
         *
         * @return A string representation of this SourcePoint.
         */
        std::string dump() const;

        std::string getLabel() const {
            auto &config = GlobalConfig::instance();
            auto suffix =
                config.acslLabelSuffixLength
                    ? "_" + utils::hash_prefix_hex_chars(hash(), config.acslLabelSuffixLength)
                    : "";
            return labelPrefix_ + suffix;
        }

      private:
        /**
         * @brief Private constructor to initialize a SourcePoint from a SourceManager.
         *
         * Only accessible to the static factory functions.
         *
         * @param SM The SourceManager to associate with this SourcePoint.
         */
        SourcePoint(const clang::SourceManager &SM, std::string labelPrefix)
            : SM_(SM), labelPrefix_(labelPrefix) {};

        clang::SourceLocation loc_;      ///< Clang source location.
        const clang::SourceManager &SM_; ///< Reference to the source manager for resolution.

        // helper member
        std::string labelPrefix_;
    };
} // namespace acslg::analyzer::symbolic

namespace std {
    template <> struct hash<acslg::analyzer::symbolic::SourcePoint> {
        size_t operator()(const acslg::analyzer::symbolic::SourcePoint &sp) const noexcept {
            return sp.hash();
        }
    };
} // namespace std

namespace acslg::analyzer::symbolic {
    /// @class SymbolicExpr
    /// @brief Base class for all symbolic expressions.
    class SymbolicExpr {
      public:
        /// @class SymbolicExpr
        /// @brief Base class for all symbolic expressions.
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
            K_LastOverRange
        };

        enum Trait : uint32_t {
            T_Symbol = 1u << 0 /* ... */
        };

        /// @enum ScalarKind
        /// @brief Scalar data types for expression values.
        enum class ScalarKind {
            Int,
            UInt,
            Bool,
            Void,
            Structure
        };

        /// @struct Type
        /// @brief Represents a scalar type with bit width.
        struct Type {
            ScalarKind kind;   ///< Base scalar kind
            unsigned bitWidth; ///< Number of bits
        };

        virtual ~SymbolicExpr()                       = default;
        SymbolicExpr(const SymbolicExpr &)            = default;
        SymbolicExpr &operator=(const SymbolicExpr &) = default;
        SymbolicExpr(SymbolicExpr &&)                 = default;
        SymbolicExpr &operator=(SymbolicExpr &&)      = default;

        static bool classof(const SymbolicExpr *) { return true; }
        static bool classof(const Symbol *) { return true; }

        ExprKind getKind() const { return kind_; }
        Type getValType() const { return valueType_; }
        void setValType(Type newType) { valueType_ = newType; }

        /// @brief Clone the expression.
        /// @return Deep copy of the expression.
        virtual utils::not_null<std::unique_ptr<SymbolicExpr>> clone() const = 0;

        /// @brief Dump debug string of the expression.
        /// @return Human-readable representation.
        virtual std::string dump() const = 0;

        struct GetACSLConfig {
            bool noStateLabelFunctionAt{false};
            std::unordered_map<SourcePoint, std::string> predefinedLabels{};
            bool useDerefWithZeroOffset{true};
            bool UnknownExprAsError{true};
        };

        enum class GetACSLError {
            HeapAddress,
            PartiallyModifiedStruct,
            UnknownExpr,
        };

        utils::expected<std::pair<std::string, std::unordered_set<SourcePoint>>, GetACSLError> getACSL(
            const GetACSLConfig &config,
            std::optional<SourcePoint> currentPoint = std::nullopt) const {
            std::unordered_set<SourcePoint> usedPoints;
            auto res = callGetACSL(*this, config, usedPoints, currentPoint);
            if (res)
                return std::pair{std::move(res.value()), std::move(usedPoints)};
            return res.error();
        }

        /// @brief Compare with another expression for structural equality.
        /// @param other Expression to compare.
        /// @return True if equal.
        virtual bool equal(const SymbolicExpr &) const = 0;

        /// @brief The inter-path hash – computed recursively via from_ – is used for SymbolicExpr
        /// comparison/storage between pathes and incurs higher computational cost.
        /// @return
        virtual std::size_t hash() const = 0; // todo: cache the result

        friend std::ostream &operator<<(std::ostream &os, const SymbolicExpr &expr) {
            return os << expr.dump();
        }

        friend bool operator==(const SymbolicExpr &LHS, const SymbolicExpr &RHS) {
            if (LHS.kind_ != RHS.kind_)
                return false;
            return LHS.equal(RHS);
        }

        /// @brief Get a simplified version of the expression.
        /// @return Simplified expression.
        virtual utils::not_null<std::unique_ptr<SymbolicExpr>> simplifiedExpr() const {
            return clone();
        };

        using UsedMap   = std::unordered_map<size_t, utils::not_null<const Symbol *>>;
        using HashIdMap = std::unordered_map<size_t, size_t>;
        /// @brief Collect `Symbols` used in the expression.
        /// @return usedPoints from hash to `Symbol`.
        virtual UsedMap collectUsedSymbols() const {
            return {};
        }; // todo: may use virtual inheritance to override this at the level of `Symbol`.

        template <typename... Exprs>
        static std::pair<UsedMap, HashIdMap> collectUsedSymbols(const SymbolicExpr &first,
                                                                const Exprs &...rest) {
            UsedMap merged;

            auto mergeIntoOne = [&](const UsedMap &m) {
                for (const auto &[k, v] : m) {
                    auto it = merged.find(k);
                    if (it == merged.end()) {
                        merged.emplace(k, v);
                    } else {
#ifndef DEBUG_MODE
                        const auto same = std::visit(
                            [&](auto a) -> bool {
                                using T = std::decay_t<decltype(a)>;
                                if (!std::holds_alternative<T>(it->second))
                                    return false;
                                auto b = std::get<T>(it->second);
                                return a.get() == b.get();
                            },
                            v);
                        assert(same &&
                               "collectUsedVarsAndAddrs key conflict with different targets");
#endif
                    }
                }
            };

            mergeIntoOne(first.collectUsedSymbols());
            (mergeIntoOne(rest.collectUsedSymbols()), ...);

            size_t index = 0;
            std::unordered_map<size_t, size_t> hashIndexMap{};
            for (auto &[hash, _] : merged) {
                hashIndexMap[hash] = index++;
            }
            return std::pair{std::move(merged), std::move(hashIndexMap)};
        }

        /// @brief Try to evaluate the expression to an symbol address.
        /// @return Returning `std::nullopt` indicates that the expression is not a valid address.
        std::optional<utils::not_null<std::unique_ptr<SymbolAddress>>> tryEvalAsSymbolAddr() const {
            return callTryEvalAsAddr(*simplifiedExpr());
        };

        // May merge `tryEvalAsConstant` and `evalToConstExpr` into one.
        std::optional<int64_t> tryEvalAsConstant() const {
            // TODO: cache the result.
            if (!isLinear())
                return std::nullopt;
            auto [hashPtrMap, hashIdMap] = SymbolicExpr::collectUsedSymbols(*this);
            auto linearExpr              = toLinearExpr(hashIdMap);
            if (linearExpr.all_homogeneous_terms_are_zero())
                return linearExpr.inhomogeneous_term().get_si();
            return std::nullopt;
        };
        virtual std::unique_ptr<LiteralExpr> evalToConstExpr() const { return nullptr; }

        /// @brief Is an unknown expression?
        /// @return
        virtual bool isUnknown() const { return false; };

        /**
         * @brief Substitute symbols with fromPoint same as `pointToSub` in an expression to the
         * given program point (pathSubTo). A substitution typically means locating the expression
         * at the address at the program point through fromAddr_ (or a similar member).
         *
         * @param pathSubTo      The path that symbols should be substituted to.
         *
         * @param pointToSub     Symbols with fromPoint same as pointToSub should be subtituted.

         */
        virtual utils::not_null<std::unique_ptr<SymbolicExpr>> getSubstitutedExpr(
            const Path &pathSubTo,
            const SourcePoint &pointToSub) const = 0;

        virtual utils::not_null<std::unique_ptr<SymbolicExpr>> getRangeIndexSubstituted(
            const SymbolAddrBaseInfo &rangeBase,
            const SymbolicExpr &indexExpr) const = 0;

        //===----------------------------------------------------------------------===//
        // StInG Interface Utilities - Symbolic Expression Adapter
        //
        // This section defines support for converting internal symbolic expressions
        // into a form consumable by the StInG (Static Invariant Generator) tool,
        // which synthesizes affine invariants via constraint solving.
        //===----------------------------------------------------------------------===//

        /// @brief Check if expression is affine (linear).
        /// @return True if linear.
        virtual bool isLinear() const = 0;

        /// @brief Maximum polynomial degree of the expression.
        /// @return Degree (0 for constants, 1 for variables), -1 means invalid or undefined.
        virtual int getMaxDegree() const = 0;

        /// @brief Convert to PPL linear expression with custom mapping.
        /// Only valid for expressions that are affine (i.e., linear w.r.t. variables).
        /// Throws or fails if the expression is not representable in linear form.
        /// Only support varDecl's value(`Address` and `SymbolValue`, see their `toLinearExpr` for
        /// more details), return nullopt otherwise.
        /// @param varMap Mapping from names to the index of the Cartesian axis.
        /// @return PPL linear expression or nullopt if contains symbolic value from pointer, array,
        /// etc.
        virtual std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const {
            ERROR("not implemented for expression type: ");
        }

        /// @brief Convert to PPL linear expression without custom mapping.
        /// Use SymbolValue's id_ as its index of the Cartesian axis.
        /// @param hashIdMap Mapping from hash of SymbolValue/symbolAddress to the index of the
        /// Cartesian axis.  Will throw an error if a non-existent hash is encountered. The ID
        /// represents the dimension of variables in the PPL library, so hashIdMap should be a
        /// sequentially numbered mapping of hash values, such as {{hash_1: 0}, {hash_2: 1}, ...}.
        /// @return PPL linear expression.
        virtual Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const std::unordered_map<size_t, size_t> &) const {
            ERROR("not implemented for expression type: ");
        }

      protected:
        /// @brief Construct a symbolic expression.
        /// @param type Expression type
        /// @param valueType Underlying value type
        SymbolicExpr(ExprKind kind, Type valueType) : kind_(kind), valueType_(valueType) {}

        /// @brief Simplify expression if it's linear, just call clone() otherwise.
        utils::not_null<std::unique_ptr<SymbolicExpr>> simplifiedExprIfLinear() const;

        /*-------------- Bridge ----------------- */
        static utils::expected<std::string, GetACSLError> callGetACSL(
            const SymbolicExpr &e,
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec = 0,
            bool isRightChild   = false) {
            return e.doGetACSL(config, usedPoints, currentPoint, parentPrec, isRightChild);
        }

        static std::optional<utils::not_null<std::unique_ptr<SymbolAddress>>> callTryEvalAsAddr(
            const SymbolicExpr &e) {
            return e.doTryEvalAsSymbolAddr();
        }

      private:
        /// @brief Try to evaluate the expression to an symbol address.
        /// @return Returning `std::nullopt` indicates that the expression is not a valid address.
        virtual std::optional<utils::not_null<std::unique_ptr<SymbolAddress>>> doTryEvalAsSymbolAddr()
            const {
            // TODO: cache the result.
            return std::nullopt;
        };

        virtual utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const = 0;

        ExprKind kind_;  ///< Kind of expression
        Type valueType_; ///< Underlying type
    };

    std::ostream &operator<<(std::ostream &os, SymbolicExpr::ExprKind t);

    /// @class LiteralExpr
    /// @brief Represents a literal constant value.
    class LiteralExpr : public SymbolicExpr {
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

        LiteralExpr(bool value)
            : SymbolicExpr(ExprKind::K_LiteralExpr, {ScalarKind::Bool, 1}),
              type_(LiteralType::Boolean) {
            data_.boolValue = value;
        }

        LiteralExpr(int value)
            : SymbolicExpr(ExprKind::K_LiteralExpr, {ScalarKind::Int, 32}),
              type_(LiteralType::Int) {
            data_.intValue = value;
        }

        LiteralExpr(unsigned int value)
            : SymbolicExpr(ExprKind::K_LiteralExpr, {ScalarKind::UInt, 32}),
              type_(LiteralType::UnsignedInt) {
            data_.uintValue = value;
        }

        LiteralExpr(short value)
            : SymbolicExpr(ExprKind::K_LiteralExpr, {ScalarKind::Int, 16}),
              type_(LiteralType::Short) {
            data_.shortValue = value;
        }

        LiteralExpr(unsigned short value)
            : SymbolicExpr(ExprKind::K_LiteralExpr, {ScalarKind::UInt, 16}),
              type_(LiteralType::UnsignedShort) {
            data_.ushortValue = value;
        }

        LiteralExpr(int64_t value)
            : SymbolicExpr(ExprKind::K_LiteralExpr, {ScalarKind::Int, 64}),
              type_(LiteralType::Int64) {
            data_.int64Value = value;
        }

        LiteralExpr(uint64_t value)
            : SymbolicExpr(ExprKind::K_LiteralExpr, {ScalarKind::UInt, 64}),
              type_(LiteralType::UInt64) {
            data_.uint64Value = value;
        }

        static bool classof(const SymbolicExpr *expr) {
            return expr->getKind() == ExprKind::K_LiteralExpr;
        }

        LiteralType getLiteralType() const { return type_; }

        utils::not_null<std::unique_ptr<SymbolicExpr>> clone() const override;
        std::string dump() const override;
        virtual utils::not_null<std::unique_ptr<SymbolicExpr>> simplifiedExpr() const override;
        virtual std::size_t hash() const override;
        std::unique_ptr<LiteralExpr> evalToConstExpr() const override;

        virtual bool equal(const SymbolicExpr &expr) const override;
        utils::not_null<std::unique_ptr<SymbolicExpr>> getSubstitutedExpr(
            const Path &pathSubTo,
            const SourcePoint &pointToSub) const override;
        utils::not_null<std::unique_ptr<SymbolicExpr>> getRangeIndexSubstituted(
            const SymbolAddrBaseInfo &rangeBase,
            const SymbolicExpr &indexExpr) const override;

        // StInG: Support functions for affine invariant analysis
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

      private:
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

    /// @class BinaryOpExpr
    /// @brief Represents a binary operation expression.
    class BinaryOpExpr : public SymbolicExpr {
      public:
        enum class Operator : unsigned {
#define BIN_OP(name, tok, prec, isRightAssoc) name,
#include "operators.def"
        };

        inline static unsigned getPrecedence(BinaryOpExpr::Operator op) {
            switch (op) {
#define BIN_OP(name, tok, prec, isRightAssoc)                                                      \
    case BinaryOpExpr::Operator::name: return prec;
#include "operators.def"
                default: ERROR("Unknown Operator");
            }
        }

        inline static bool isRightAssociative(BinaryOpExpr::Operator op) {
            switch (op) {
#define BIN_OP(name, tok, prec, isRightAssoc)                                                      \
    case BinaryOpExpr::Operator::name: return isRightAssoc;
#include "operators.def"
                default: ERROR("Unknown operator");
            }
        }

        // TODO(style): May use template to unify constructors.
        BinaryOpExpr(utils::not_null<std::unique_ptr<SymbolicExpr>> left,
                     Operator op,
                     utils::not_null<std::unique_ptr<SymbolicExpr>> right)
            : SymbolicExpr(ExprKind::K_BinaryOpExpr, left->getValType()), left_(std::move(left)),
              op_(op), right_(std::move(right)) {}

        BinaryOpExpr(utils::not_null<SymbolicExpr *> left,
                     Operator op,
                     utils::not_null<SymbolicExpr *> right)
            : SymbolicExpr(ExprKind::K_BinaryOpExpr, left->getValType()),
              left_(std::unique_ptr<SymbolicExpr>{left}), op_(op),
              right_(std::unique_ptr<SymbolicExpr>{right}) {}

        static bool classof(const SymbolicExpr *expr) {
            return expr->getKind() == ExprKind::K_BinaryOpExpr;
        }

        utils::not_null<const SymbolicExpr *> getLeft() const { return left_.get().get(); }
        utils::not_null<const SymbolicExpr *> getRight() const { return right_.get().get(); }
        auto getLeft() -> auto & { return left_; }
        auto getRight() -> auto & { return right_; }
        Operator getOperator() const { return op_; }

        utils::not_null<std::unique_ptr<SymbolicExpr>> clone() const override;
        std::string dump() const override;
        virtual utils::not_null<std::unique_ptr<SymbolicExpr>> simplifiedExpr() const override;
        virtual std::size_t hash() const override;
        std::unique_ptr<LiteralExpr> evalToConstExpr() const override;

        virtual bool equal(const SymbolicExpr &expr) const override;
        virtual bool isUnknown() const override {
            return left_->isUnknown() || right_->isUnknown();
        };
        utils::not_null<std::unique_ptr<SymbolicExpr>> getSubstitutedExpr(
            const Path &pathSubTo,
            const SourcePoint &pointToSub) const override;
        utils::not_null<std::unique_ptr<SymbolicExpr>> getRangeIndexSubstituted(
            const SymbolAddrBaseInfo &rangeBase,
            const SymbolicExpr &indexExpr) const override;

        // StInG: Support functions for affine invariant analysis
        UsedMap collectUsedSymbols() const override;
        bool isLinear() const override;
        int getMaxDegree() const override;
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const std::unordered_map<size_t, size_t> &) const override;

      private:
        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

        virtual std::optional<utils::not_null<std::unique_ptr<SymbolAddress>>> doTryEvalAsSymbolAddr()
            const override;

      private:
        utils::not_null<std::unique_ptr<SymbolicExpr>> left_;
        Operator op_;
        utils::not_null<std::unique_ptr<SymbolicExpr>> right_;
    };

    /// @class UnaryOpExpr
    /// @brief Represents a unary operation expression.
    class UnaryOpExpr : public SymbolicExpr {
      public:
        enum class Operator : unsigned {
#define UN_OP(name, tok, prec, isRightAssoc) name,
#include "operators.def"
        };

        inline static unsigned getPrecedence(UnaryOpExpr::Operator op) {
            switch (op) {
#define UN_OP(name, tok, prec, isRightAssoc)                                                       \
    case UnaryOpExpr::Operator::name: return prec;
#include "operators.def"
                default: ERROR("Unknown Operator");
            }
        }

        inline static bool isRightAssociative(UnaryOpExpr::Operator op) {
            switch (op) {
#define UN_OP(name, tok, prec, isRightAssoc)                                                       \
    case UnaryOpExpr::Operator::name: return isRightAssoc;
#include "operators.def"
                default: ERROR("Unknown operator");
            }
        }

        UnaryOpExpr(Operator op, utils::not_null<std::unique_ptr<SymbolicExpr>> expr)
            : SymbolicExpr(ExprKind::K_UnaryOpExpr, expr->getValType()), op_(op),
              expr_(std::move(expr)) {}

        static bool classof(const SymbolicExpr *expr) {
            return expr->getKind() == ExprKind::K_UnaryOpExpr;
        }

        utils::not_null<const SymbolicExpr *> getSub() const { return expr_.get().get(); }
        auto getSub() -> auto & { return expr_; }
        Operator getOperator() const { return op_; }

        utils::not_null<std::unique_ptr<SymbolicExpr>> clone() const override;
        std::string dump() const override;
        virtual utils::not_null<std::unique_ptr<SymbolicExpr>> simplifiedExpr() const override;
        virtual std::size_t hash() const override;
        std::unique_ptr<LiteralExpr> evalToConstExpr() const override;
        utils::not_null<std::unique_ptr<SymbolicExpr>> getSubstitutedExpr(
            const Path &pathSubTo,
            const SourcePoint &pointToSub) const override;
        utils::not_null<std::unique_ptr<SymbolicExpr>> getRangeIndexSubstituted(
            const SymbolAddrBaseInfo &rangeBase,
            const SymbolicExpr &indexExpr) const override;

        virtual bool equal(const SymbolicExpr &expr) const override;
        virtual bool isUnknown() const override { return expr_->isUnknown(); };

        // StInG: Support functions for affine invariant analysis
        UsedMap collectUsedSymbols() const override;
        bool isLinear() const override;
        int getMaxDegree() const override;
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const std::unordered_map<size_t, size_t> &) const override;

      private:
        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

      private:
        Operator op_;
        utils::not_null<std::unique_ptr<SymbolicExpr>> expr_;
    };

    /// @class UnknownExpr
    /// @brief Represents a unknown symbolic expression, primarily used to denote cases beyond
    /// capabilities.
    class UnknownExpr : public SymbolicExpr {
      public:
        UnknownExpr() : SymbolicExpr(ExprKind::K_UnknownExpr, {ScalarKind::Void, 0}) {}
        ~UnknownExpr() = default;

        static bool classof(const SymbolicExpr *expr) {
            return expr->getKind() == ExprKind::K_UnknownExpr;
        }

        /// @brief Create a unknown symbolic expression.
        /// @return Unique pointer to a Unknown expression.
        static utils::not_null<std::unique_ptr<UnknownExpr>> makeUnknown();

        utils::not_null<std::unique_ptr<SymbolicExpr>> clone() const override;
        std::string dump() const override;
        virtual std::size_t hash() const override;
        virtual bool equal(const SymbolicExpr &expr) const override;
        virtual bool isUnknown() const override { return true; };
        utils::not_null<std::unique_ptr<SymbolicExpr>> getSubstitutedExpr(
            const Path &pathSubTo,
            const SourcePoint &pointToSub) const override;
        utils::not_null<std::unique_ptr<SymbolicExpr>> getRangeIndexSubstituted(
            const SymbolAddrBaseInfo &rangeBase,
            const SymbolicExpr &indexExpr) const override;

        // StInG: Support functions for affine invariant analysis
        bool isLinear() const override { return false; }
        int getMaxDegree() const override { return 0; }

      private:
        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;
    };

    class Symbol {
      public:
        virtual ~Symbol()                 = default;
        Symbol(const Symbol &)            = default;
        Symbol &operator=(const Symbol &) = default;
        Symbol(Symbol &&)                 = default;
        Symbol &operator=(Symbol &&)      = default;

        enum class Kind {
            K_Structure,
            K_SymbolAddress,
            K_SymbolValue,
            K_SumOverRange,
        };
        Kind getKind() const { return kind_; }

        static bool classof(const SymbolicExpr *e);
        static bool classof(const Symbol *) { return true; }

        // For LLVM RTTI.
        static Symbol *toThis(SymbolicExpr *e);
        static const Symbol *toThis(const SymbolicExpr *e);

        utils::not_null<SymbolicExpr *> toSymbolicExpr();
        utils::not_null<const SymbolicExpr *> toSymbolicExpr() const;
        virtual std::optional<utils::not_null<std::unique_ptr<const Address>>> getFromAddr()
            const                                               = 0;
        virtual std::optional<SourcePoint> getFromPoint() const = 0;

      protected:
        Symbol(Kind kind) : kind_(kind) {}

        static utils::expected<std::string, SymbolicExpr::GetACSLError> callGetACSLOfValueProxy(
            const Address &addr,
            const SymbolicExpr::GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec = 0,
            bool isRightChild   = false);

      private:
        Kind kind_;
    };

    /**
     * @brief Aggregate container for field values.
     *
     * @details
     * The Structure class represents a structured collection of field values. It is not intended to
     * be used in arithmetic expressions, but rather serves as a container and query interface for
     * structured data.
     *
     * - **Construction**:
     *   Since id_ and name_ have been removed, a Structure can be constructed directly from the
     *   symbolic values of all its fields.
     *
     * - **getFromAddr()**:
     *   Invokes getFromAddr() on each field value. The function returns a common Address only if
     *   every field yields a valid result and all results are FieldAddress instances with the same
     *   base address and indices that correspond to the field values. Otherwise, it returns
     *   monostate.
     *
     * - **Equality and Hashing**:
     *   Both equality comparison and hash computation are defined as aggregation operations over
     *   all field values.
     */

    class Structure : public SymbolicExpr, public Symbol {
      public:
        struct Info {
            utils::not_null<const clang::RecordDecl *> definition_;
            const clang::ASTRecordLayout &layout_;
            Info(const clang::RecordDecl *RD, const clang::ASTRecordLayout &layout)
                : definition_(RD) /*utils::not_null has no default constructor*/, layout_(layout) {
                if (!RD->isCompleteDefinition())
                    ERROR("Incomplete struct definition");
                definition_ = RD->getDefinition();
            }
            Info(const Info &other) : definition_(other.definition_), layout_(other.layout_) {};
            Info(Info &&) = default;

            bool equal(const Structure::Info &other) const;
            bool operator==(const Info &other) const;
            std::string dump() const;
            size_t getNumFields() const { return layout_.getFieldCount(); }
        };

        Structure(const clang::RecordDecl *RD,
                  const clang::ASTRecordLayout &layout,
                  utils::not_null<std::unique_ptr<const Address>> from,
                  SourcePoint fromPoint);

        Structure(const Structure &other) : SymbolicExpr(other), Symbol(other), info_(other.info_) {
            fields_.clear();
            fields_.reserve(other.fields_.size());
            std::ranges::transform(
                other.fields_, std::back_inserter(fields_),
                [](auto &field) -> utils::not_null<std::unique_ptr<SymbolicExpr>> {
                    return field->clone();
                });
        }

        static bool classof(const SymbolicExpr *expr) {
            return expr->getKind() == ExprKind::K_Structure;
        }
        static bool classof(const Symbol *e) { return e->getKind() == Symbol::Kind::K_Structure; }

        size_t getNumFields() const { return info_.getNumFields(); }
        void setFieldValue(size_t index, utils::not_null<std::unique_ptr<SymbolicExpr>> expr);
        utils::not_null<const SymbolicExpr *> getFieldValue(size_t index) const {
            if (index >= fields_.size())
                ERROR("Out-of-bounds access");
            return fields_[index].get().get();
        };
        utils::not_null<std::unique_ptr<SymbolicExpr>> &getFieldValue(size_t index) {
            if (index >= fields_.size())
                ERROR("Out-of-bounds access");
            return fields_[index];
        };
        auto fieldsValues() { return std::span{fields_}; }
        auto fieldsValues() const {
            return fields_ | std::views::transform(
                                 [](auto const &up) -> utils::not_null<const SymbolicExpr *> {
                                     return up.value().get().get();
                                 });
        }
        auto getInfo() const -> const auto & { return info_; }

        utils::not_null<std::unique_ptr<SymbolicExpr>> clone() const override;
        std::string dump() const override;
        std::optional<utils::not_null<std::unique_ptr<const Address>>> getFromAddr() const override;
        std::optional<SourcePoint> getFromPoint() const override;
        virtual std::size_t hash() const override;
        bool equal(const SymbolicExpr &expr) const override;
        utils::not_null<std::unique_ptr<SymbolicExpr>> getSubstitutedExpr(
            const Path &pathSubTo,
            const SourcePoint &pointToSub) const override;
        utils::not_null<std::unique_ptr<SymbolicExpr>> getRangeIndexSubstituted(
            const SymbolAddrBaseInfo &rangeBase,
            const SymbolicExpr &indexExpr) const override;

        // StInG: Support functions for affine invariant analysis
        bool isLinear() const override {
            WARN("Met Structure in isLinear.");
            return false;
        }
        int getMaxDegree() const override {
            WARN("Met Structure in getMaxDegree.");
            return -1;
        }

      protected:
        using From =
            std::optional<std::pair<utils::not_null<std::unique_ptr<const Address>>, SourcePoint>>;
        From getFrom() const;

      private:
        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

      private:
        Info info_;
        std::vector<utils::not_null<std::unique_ptr<SymbolicExpr>>> fields_;
    };

    class Address : public SymbolicExpr {
      public:
        virtual ~Address()                  = default;
        Address(const Address &)            = default;
        Address &operator=(const Address &) = default;
        Address(Address &&)                 = default;
        Address &operator=(Address &&)      = default;

        static bool classof(const SymbolicExpr *e) {
            auto k = e->getKind();
            return (k > ExprKind::K_FirstAddr) && (k < ExprKind::K_LastAddr);
        }

        utils::expected<std::pair<std::string, std::unordered_set<SourcePoint>>, GetACSLError> getACSLOfValue(
            const GetACSLConfig &config,
            std::optional<SourcePoint> currentPoint = std::nullopt) const {
            std::unordered_set<SourcePoint> usedPoints;
            auto res = callGetACSLOfValue(*this, config, usedPoints, currentPoint);
            if (res)
                return std::pair{std::move(res.value()), std::move(usedPoints)};
            return res.error();
        }
        virtual std::optional<utils::not_null<const clang::VarDecl *>> getFromRoot() const = 0;
        virtual int getDimension() const                                                   = 0;
        virtual utils::not_null<std::unique_ptr<Address>> addressClone() const             = 0;

        auto getPointeeType() const -> const auto & { return pointeeType_; }

      protected:
        Address(ExprKind kind, Type valueType, const clang::QualType &pointeeType)
            : SymbolicExpr(kind, valueType), pointeeType_(pointeeType) {};

        /*---------------- Bridge -----------------*/
        static utils::expected<std::string, GetACSLError> callGetACSLOfValue(
            const Address &addr,
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec = 0,
            bool isRightChild   = false) {
            return addr.doGetACSLOfValue(config, usedPoints, currentPoint, parentPrec,
                                         isRightChild);
        }

      private:
        virtual utils::expected<std::string, GetACSLError> doGetACSLOfValue(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const = 0;

      protected:
        clang::QualType pointeeType_;

      private:
        friend Symbol;
    };

    class AddressBox {
      public:
        explicit AddressBox(utils::not_null<std::unique_ptr<Address>> p) noexcept
            : ptr_(std::move(p)) {}
        AddressBox(const Address &other) : ptr_(other.addressClone()) {};

        AddressBox(const AddressBox &other) : ptr_(other.ptr_->addressClone()) {}
        AddressBox &operator=(const AddressBox &other) {
            if (this == &other)
                return *this;
            ptr_ = other.ptr_->addressClone();
            return *this;
        }

        AddressBox(AddressBox &&) noexcept            = default;
        AddressBox &operator=(AddressBox &&) noexcept = default;

        operator Address &() { return *ptr_; }
        operator const Address &() const { return *ptr_; }

        Address &get() { return *ptr_; }
        const Address &get() const { return *ptr_; }

        friend bool operator==(const AddressBox &a, const AddressBox &b) {
            return a.ptr_->equal(*b.ptr_);
        }

        friend bool operator!=(const AddressBox &a, const AddressBox &b) { return !(a == b); }

        std::size_t hash() const noexcept { return ptr_->hash(); }

      private:
        utils::not_null<std::unique_ptr<Address>> ptr_;
    };

    struct AddressBoxHash {
        using is_transparent = void;

        std::size_t operator()(const AddressBox &k) const noexcept { return k.hash(); }
        std::size_t operator()(const Address &k) const noexcept { return k.hash(); }
    };

    struct AddressBoxEq {
        using is_transparent = void;

        bool operator()(const AddressBox &a, const AddressBox &b) const { return a == b; }
        bool operator()(const AddressBox &a, const Address &b) const { return a.get().equal(b); }
        bool operator()(const Address &a, const AddressBox &b) const { return a.equal(b); }
    };

    template <class T>
    using AddressBoxMap = std::unordered_map<AddressBox, T, AddressBoxHash, AddressBoxEq>;

    /// @class SymbolAddress
    /// @brief Symbolic address with fromAddr, fromPoint, offset and length. Maybe a symbol value
    /// of pointer variable or an address of heap.
    class SymbolAddress : public Address, public Symbol {
      public:
        inline static constexpr signed long ZERO_OFFSET =
            0; ///< Unify the type of zero under zero offset. This type should be the same as the
               ///< type of the zero value in SymbolicExpr::simplifiedExprIfLinear, or relax the
               ///< type comparison in LiteralExpr's equal method.
        struct BaseInfo;
        class RangeIndex; // todo: Separate `SymbolAddress` into `SymbolAddress` and `RangeExpr`,
                          // making `RangeIndex` a nested type within `RangeExpr`.

        SymbolAddress(const SymbolAddress &other);
        SymbolAddress(SymbolAddress &&) = default;

        SymbolAddress(const clang::QualType pointeeType,
                      std::optional<utils::not_null<std::unique_ptr<const Address>>> from,
                      SourcePoint fromPoint,
                      std::optional<utils::not_null<std::unique_ptr<const SymbolicExpr>>> offset =
                          std::nullopt,
                      std::optional<utils::not_null<std::unique_ptr<const SymbolicExpr>>> length =
                          std::nullopt);

        static bool classof(const SymbolicExpr *expr) {
            return expr->getKind() == ExprKind::K_SymbolAddress;
        }
        static bool classof(const Symbol *e) {
            return e->getKind() == Symbol::Kind::K_SymbolAddress;
        }

        bool operator==(const SymbolAddress &other) const { return equal(other); }

        utils::not_null<const SymbolicExpr *> getOffset() const { return offset_.get().get(); }
        void setOffset(utils::not_null<std::unique_ptr<SymbolicExpr>> offset);
        void addOffset(utils::not_null<std::unique_ptr<SymbolicExpr>> extra);
        void subOffset(utils::not_null<std::unique_ptr<SymbolicExpr>> extra);
        void resetOffset() { offset_ = std::make_unique<LiteralExpr>(ZERO_OFFSET); }

        void setLength(utils::not_null<std::unique_ptr<SymbolicExpr>> len);
        void addLength(utils::not_null<std::unique_ptr<SymbolicExpr>> extra);
        auto getLength() const -> const auto & { return length_; }
        void resetLength() { length_ = std::nullopt; }

        std::optional<utils::not_null<std::unique_ptr<SymbolicExpr>>> getRightBound() const;
        SymbolAddrBaseInfo getBaseInfo() const;
        utils::not_null<std::unique_ptr<RangeIndex>> getRangeIndex(std::string_view indexName) const;

        // SymbolExpr
      public:
        utils::not_null<std::unique_ptr<SymbolicExpr>> clone() const override;
        std::string dump() const override;
        virtual utils::not_null<std::unique_ptr<SymbolicExpr>> simplifiedExpr() const override;
        virtual bool equal(const SymbolicExpr &expr) const override;
        virtual std::size_t hash() const override;
        utils::not_null<std::unique_ptr<SymbolicExpr>> getSubstitutedExpr(
            const Path &pathSubTo,
            const SourcePoint &pointToSub) const override;
        utils::not_null<std::unique_ptr<SymbolicExpr>> getRangeIndexSubstituted(
            const SymbolAddrBaseInfo &rangeBase,
            const SymbolicExpr &indexExpr) const override;
        // StInG: Support functions for affine invariant analysis
        UsedMap collectUsedSymbols() const override;
        bool isLinear() const override {
            if (length_)
                ERROR("Address range is solely for address representation and should not be "
                      "used as an expression.");
            return true;
        }
        int getMaxDegree() const override {
            if (length_)
                ERROR("Address range is solely for address representation and should not be "
                      "used as an expression.");
            return 1;
        }
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const std::unordered_map<size_t, size_t> &) const override;

      private:
        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChilds) const override;
        virtual std::optional<utils::not_null<std::unique_ptr<SymbolAddress>>> doTryEvalAsSymbolAddr()
            const override;

        // Address
      public:
        std::optional<utils::not_null<const clang::VarDecl *>> getFromRoot() const override;
        int getDimension() const override;
        virtual utils::not_null<std::unique_ptr<Address>> addressClone() const override;

      private:
        utils::expected<std::string, GetACSLError> doGetACSLOfValue(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

        // Symbol
      public:
        std::optional<utils::not_null<std::unique_ptr<const Address>>> getFromAddr() const override {
            if (fromAddr_ == std::nullopt)
                return std::nullopt;
            return fromAddr_.value()->addressClone().into_underlying();
        }
        std::optional<SourcePoint> getFromPoint() const override { return fromPoint_; }

      private:
        utils::not_null<std::unique_ptr<const SymbolicExpr>>
            offset_; ///< Offset relative to an address.
        std::optional<utils::not_null<std::unique_ptr<const Address>>>
            fromAddr_; ///< From another Address p means this is a value(may with offset) of a
                       ///< pointer variable whose address is p; std::nullopt means this a
                       ///< address of heap, in which case `fromPoint_` is the source point after
                       ///< the *alloc*.

        SourcePoint fromPoint_;
        std::optional<utils::not_null<std::unique_ptr<const SymbolicExpr>>> length_;
    };

    struct SymbolAddrBaseInfo {
        std::optional<utils::not_null<std::unique_ptr<const Address>>> fromAddr_;
        SourcePoint fromPoint_;
        clang::QualType pointeeType_;

        SymbolAddrBaseInfo(std::optional<utils::not_null<std::unique_ptr<const Address>>> fromAddr,
                           SourcePoint fromPoint,
                           clang::QualType pointeeType)
            : fromAddr_(std::move(fromAddr)), fromPoint_(std::move(fromPoint)),
              pointeeType_(pointeeType) {}
        SymbolAddrBaseInfo(const SymbolAddrBaseInfo &);
        SymbolAddrBaseInfo &operator=(const SymbolAddrBaseInfo &other) {
            if (&other == this)
                return *this;
            if (other.fromAddr_)
                fromAddr_ = other.fromAddr_.value()->addressClone().into_underlying();
            else
                fromAddr_ = std::nullopt;
            fromPoint_   = other.fromPoint_;
            pointeeType_ = other.pointeeType_;
            return *this;
        }
        SymbolAddrBaseInfo(SymbolAddrBaseInfo &&) = default;
        SymbolAddrBaseInfo &operator=(SymbolAddrBaseInfo &&other) {
            if (&other == this)
                return *this;
            fromAddr_    = std::move(other.fromAddr_);
            fromPoint_   = std::move(other.fromPoint_);
            pointeeType_ = std::move(other.pointeeType_);
            return *this;
        }
        size_t hash() const;
        bool operator==(const SymbolAddrBaseInfo &other) const {
            if (fromPoint_ != other.fromPoint_)
                return false;
            if (fromAddr_ && other.fromAddr_ && *fromAddr_.value() != *other.fromAddr_.value())
                return false;
            if ((fromAddr_ == std::nullopt) ^ (other.fromAddr_ == std::nullopt))
                return false;
            return true;
        }

        std::optional<utils::not_null<const clang::VarDecl *>> getFromRoot() const;
    };

    /// @class VariableAddress
    /// @brief Represents the address of a C variable.
    class VariableAddress : public Address {
      public:
        VariableAddress(const VariableAddress &other);
        VariableAddress &operator=(const VariableAddress &other);
        VariableAddress(VariableAddress &&)            = default;
        VariableAddress &operator=(VariableAddress &&) = default;

        bool operator==(const VariableAddress &other) const { return equal(other); }

        VariableAddress(utils::not_null<const clang::VarDecl *> from)
            : Address(SymbolicExpr::ExprKind::K_VariableAddress,
                      SymbolicExpr::Type{SymbolicExpr::ScalarKind::UInt, 64},
                      from->getType()),
              from_(std::move(from)) {};

        static bool classof(const SymbolicExpr *expr) {
            return expr->getKind() == ExprKind::K_VariableAddress;
        }

        utils::not_null<std::unique_ptr<SymbolicExpr>> clone() const override;
        std::string dump() const override;

        virtual utils::not_null<std::unique_ptr<SymbolicExpr>> simplifiedExpr() const override {
            ERROR("VariableAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        };
        virtual bool equal(const SymbolicExpr &expr) const override;
        virtual std::size_t hash() const override;
        utils::not_null<std::unique_ptr<SymbolicExpr>> getSubstitutedExpr(
            const Path &pathSubTo,
            const SourcePoint &pointToSub) const override;
        utils::not_null<std::unique_ptr<SymbolicExpr>> getRangeIndexSubstituted(
            const SymbolAddrBaseInfo &rangeBase,
            const SymbolicExpr &indexExpr) const override;

        auto getFrom() const -> const auto & { return from_; }
        std::optional<utils::not_null<const clang::VarDecl *>> getFromRoot() const override;
        int getDimension() const override;
        virtual utils::not_null<std::unique_ptr<Address>> addressClone() const override;

        // StInG: Support functions for affine invariant analysis
        UsedMap collectUsedSymbols() const override {
            ERROR("VariableAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        };
        bool isLinear() const override {
            ERROR("VariableAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        }
        int getMaxDegree() const override {
            ERROR("VariableAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        }
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override {
            ERROR("VariableAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        };
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const std::unordered_map<size_t, size_t> &) const override {
            ERROR("VariableAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        };

      private:
        virtual std::optional<utils::not_null<std::unique_ptr<SymbolAddress>>> doTryEvalAsSymbolAddr()
            const override {
            ERROR("VariableAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        };

        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;
        utils::expected<std::string, GetACSLError> doGetACSLOfValue(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

      private:
        utils::not_null<const clang::VarDecl *> from_;
    };

    /// @class FieldAddress
    /// @brief Represents the address of a C Structure's member.
    class FieldAddress : public Address {
      public:
        FieldAddress(const FieldAddress &other);
        FieldAddress &operator=(const FieldAddress &other);
        FieldAddress(FieldAddress &&) = default;

        FieldAddress(const clang::QualType pointeeType,
                     const clang::RecordDecl *RD,
                     utils::not_null<std::unique_ptr<const Address>> baseAddr,
                     size_t fieldIndex)
            : Address(SymbolicExpr::ExprKind::K_FieldAddress,
                      SymbolicExpr::Type{SymbolicExpr::ScalarKind::UInt, 64},
                      pointeeType),
              definition_(RD), baseAddr_(std::move(baseAddr)), fieldIndex_(fieldIndex) {
            if (!RD->isCompleteDefinition())
                ERROR("Incomplete struct definition");
            definition_ = RD->getDefinition();
        };

        static bool classof(const SymbolicExpr *expr) {
            return expr->getKind() == ExprKind::K_FieldAddress;
        }

        utils::not_null<std::unique_ptr<SymbolicExpr>> clone() const override;
        std::string dump() const override;

        virtual utils::not_null<std::unique_ptr<SymbolicExpr>> simplifiedExpr() const override {
            ERROR("FieldAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        };
        virtual bool equal(const SymbolicExpr &expr) const override;
        virtual std::size_t hash() const override;
        utils::not_null<std::unique_ptr<SymbolicExpr>> getSubstitutedExpr(
            const Path &pathSubTo,
            const SourcePoint &pointToSub) const override;
        utils::not_null<std::unique_ptr<SymbolicExpr>> getRangeIndexSubstituted(
            const SymbolAddrBaseInfo &rangeBase,
            const SymbolicExpr &indexExpr) const override;

        auto getDefinition() const -> const auto & { return definition_; }
        auto getBaseAddr() const -> const auto & { return baseAddr_; }
        auto getFieldIndex() const -> const auto & { return fieldIndex_; }
        std::optional<utils::not_null<const clang::VarDecl *>> getFromRoot() const override;
        int getDimension() const override;
        virtual utils::not_null<std::unique_ptr<Address>> addressClone() const override;

        // StInG: Support functions for affine invariant analysis
        UsedMap collectUsedSymbols() const override {
            ERROR("FieldAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        };
        bool isLinear() const override {
            ERROR("FieldAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        }
        int getMaxDegree() const override {
            ERROR("FieldAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        }
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override {
            ERROR("FieldAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        };
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const std::unordered_map<size_t, size_t> &) const override {
            ERROR("FieldAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        };

      private:
        virtual std::optional<utils::not_null<std::unique_ptr<SymbolAddress>>> doTryEvalAsSymbolAddr()
            const override {
            ERROR("FieldAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        };

        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;
        utils::expected<std::string, GetACSLError> doGetACSLOfValue(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

      private:
        utils::not_null<const clang::RecordDecl *> definition_;
        utils::not_null<std::unique_ptr<const Address>> baseAddr_;
        size_t fieldIndex_;
    };

    struct AddressHash {
        std::size_t operator()(const Address &addr) const noexcept { return addr.hash(); }
    };

    /// @class Symbol value
    /// @brief Symbolic value with unique ID and optional origin.
    /// Origin can't be nullptr, use nullopt.
    class SymbolValue : public SymbolicExpr, public Symbol {
      public:
        SymbolValue(Type varType,
                    utils::not_null<std::unique_ptr<const Address>> from,
                    SourcePoint fromPoint)
            : SymbolicExpr(ExprKind::K_SymbolValue, varType), Symbol(Kind::K_SymbolValue),
              fromAddr_(std::move(from)), fromPoint_(std::move(fromPoint)) {}

        SymbolValue(const SymbolValue &other);
        SymbolValue(SymbolValue &&) = default;

        static bool classof(const SymbolicExpr *expr) {
            return expr->getKind() == ExprKind::K_SymbolValue;
        }
        static bool classof(const Symbol *e) { return e->getKind() == Symbol::Kind::K_SymbolValue; }

        utils::not_null<std::unique_ptr<SymbolicExpr>> clone() const override;
        std::string dump() const override;

        virtual std::size_t hash() const override;
        virtual bool equal(const SymbolicExpr &expr) const override;
        utils::not_null<std::unique_ptr<SymbolicExpr>> getSubstitutedExpr(
            const Path &pathSubTo,
            const SourcePoint &pointToSub) const override;
        utils::not_null<std::unique_ptr<SymbolicExpr>> getRangeIndexSubstituted(
            const SymbolAddrBaseInfo &rangeBase,
            const SymbolicExpr &indexExpr) const override;
        std::optional<utils::not_null<std::unique_ptr<const Address>>> getFromAddr() const override {
            return fromAddr_->addressClone().into_underlying();
        }
        std::optional<SourcePoint> getFromPoint() const override { return fromPoint_; }
        std::optional<utils::not_null<const clang::VarDecl *>> getFromRoot() const;

        // StInG: Support functions for affine invariant analysis
        UsedMap collectUsedSymbols() const override;
        bool isLinear() const override { return true; }
        int getMaxDegree() const override { return 1; }
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const std::unordered_map<size_t, size_t> &) const override;

      private:
        utils::expected<std::string, GetACSLError> doGetACSL(
            const GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const override;

      private:
        utils::not_null<std::unique_ptr<const Address>>
            fromAddr_; ///< The original Address of the value or the Structure it belongs.

        SourcePoint fromPoint_;
    };

    std::unique_ptr<SymbolicExpr> createLNotExpr(
        utils::not_null<std::unique_ptr<SymbolicExpr>> expr);
    utils::not_null<std::unique_ptr<SymbolicExpr>> makeUnknownStructure(
        const clang::QualType &ty,
        utils::not_null<std::unique_ptr<const Address>> baseAddr,
        SourcePoint fromPoint);

    BinaryOpExpr::Operator getCompoundAssignOp(clang::BinaryOperatorKind compoundAssignOp);
    BinaryOpExpr::Operator getBinaryOp(clang::BinaryOperatorKind op);
    SymbolicExpr::Type deriveType(clang::QualType type);
    bool isValidOffsetOrLength(const SymbolicExpr &expr);

    bool is_symbol_addr(const Address &a) noexcept;

    bool isFrom(const SymbolicExpr &expr, const Address &fromAddr, SourcePoint fromPoint);

    utils::not_null<std::unique_ptr<SymbolicExpr>> getSymbol(
        clang::QualType type,
        std::optional<utils::not_null<std::unique_ptr<const Address>>> from,
        SourcePoint fromPoint);

    enum class Operator : unsigned {
#define ALL_OP(name, tok, prec, isRightAssoc) name,
#include "operators.def"
    };

    inline unsigned getPrecedence(Operator op) {
        switch (op) {
#define ALL_OP(name, tok, prec, isRightAssoc)                                                      \
    case Operator::name: return prec;
#include "operators.def"
            default: ERROR("Unknown Operator");
        }
    }

    inline bool isRightAssociative(Operator op) {
        switch (op) {
#define ALL_OP(name, tok, prec, isRightAssoc)                                                      \
    case Operator::name: return isRightAssoc;
#include "operators.def"
            default: ERROR("Unknown operator");
        }
    }
} // namespace acslg::analyzer::symbolic

namespace std {
    template <> struct hash<acslg::analyzer::symbolic::VariableAddress> {
        size_t operator()(const acslg::analyzer::symbolic::VariableAddress &va) const noexcept {
            return va.hash();
        }
    };
    template <> struct hash<acslg::analyzer::symbolic::SymbolAddress> {
        size_t operator()(const acslg::analyzer::symbolic::SymbolAddress &sa) const noexcept {
            return sa.hash();
        }
    };
    template <> struct hash<acslg::analyzer::symbolic::SymbolAddrBaseInfo> {
        size_t operator()(const acslg::analyzer::symbolic::SymbolAddrBaseInfo &bi) const noexcept {
            return bi.hash();
        }
    };
} // namespace std

namespace llvm {
    namespace symb = acslg::analyzer::symbolic;

    template <typename T>
    concept NotDerivedFromSymbolicExpr = !std::is_base_of_v<symb::SymbolicExpr, T>;

    template <NotDerivedFromSymbolicExpr To>
    struct CastInfo<To, symb::SymbolicExpr *>
        : CastIsPossible<To, symb::SymbolicExpr *>,
          NullableValueCastFailed<To *>,
          DefaultDoCastIfPossible<To *, symb::SymbolicExpr *, CastInfo<To, symb::SymbolicExpr *>> {
        static To *doCast(symb::SymbolicExpr *e) { return To::toThis(e); }
    };

    template <NotDerivedFromSymbolicExpr To>
    struct CastInfo<const To, const symb::SymbolicExpr *>
        : CastIsPossible<const To, const symb::SymbolicExpr *>,
          NullableValueCastFailed<const To *>,
          DefaultDoCastIfPossible<const To *,
                                  const symb::SymbolicExpr *,
                                  CastInfo<const To, const symb::SymbolicExpr *>> {
        static const To *doCast(const symb::SymbolicExpr *e) { return To::toThis(e); }
    };

    template <NotDerivedFromSymbolicExpr From>
    struct CastInfo<symb::SymbolicExpr, From *>
        : CastIsPossible<symb::SymbolicExpr, From *>,
          NullableValueCastFailed<symb::SymbolicExpr *>,
          DefaultDoCastIfPossible<symb::SymbolicExpr *, From *, CastInfo<symb::SymbolicExpr, From *>> {
        static symb::SymbolicExpr *doCast(From *e) {
            if (e == nullptr)
                return nullptr;
            return e->toSymbolicExpr();
        }
    };

    template <NotDerivedFromSymbolicExpr From>
    struct CastInfo<const symb::SymbolicExpr, const From *>
        : CastIsPossible<const symb::SymbolicExpr, const From *>,
          NullableValueCastFailed<const symb::SymbolicExpr *>,
          DefaultDoCastIfPossible<const symb::SymbolicExpr *,
                                  const From *,
                                  CastInfo<const symb::SymbolicExpr, const From *>> {
        static const symb::SymbolicExpr *doCast(const From *e) {
            if (e == nullptr)
                return nullptr;
            return e->toSymbolicExpr();
        }
    };
} // namespace llvm

namespace acslg::analyzer::symbolic {
    // @WindOctober: TODO Split define and declaration.
    // @WindOctober: TODO process more complicate expr case.
    // Strip one exact factor `sizeofBytes` if it appears as a literal factor.
    //
    // Handles:
    //   - sizeofBytes * X  -> X
    //   - X * sizeofBytes  -> X
    //   - sizeofBytes      -> 1
    // Otherwise returns the input unchanged.
    inline ::acslg::utils::not_null<std::unique_ptr<::acslg::analyzer::symbolic::SymbolicExpr>> strip_sizeof_factor(
        ::acslg::utils::not_null<std::unique_ptr<::acslg::analyzer::symbolic::SymbolicExpr>> in,
        std::uint64_t sizeofBytes) {
        using ::acslg::analyzer::symbolic::BinaryOpExpr;
        using ::acslg::analyzer::symbolic::LiteralExpr;
        using ::acslg::analyzer::symbolic::SymbolicExpr;

        // Literal equals sizeofBytes -> return 1
        if (auto *lit = llvm::dyn_cast<LiteralExpr>(in.get().get())) {
            const auto v = static_cast<std::uint64_t>(lit->getLiteralValue());
            if (v == sizeofBytes) {
                return ::acslg::utils::not_null<std::unique_ptr<SymbolicExpr>>{
                    std::make_unique<LiteralExpr>(std::uint64_t{1})};
            }
            return in;
        }

        // Multiply(sizeofBytes, X) or Multiply(X, sizeofBytes) -> return X
        if (auto *bin = llvm::dyn_cast<BinaryOpExpr>(in.get().get())) {
            using Op = BinaryOpExpr::Operator;
            if (bin->getOperator() == Op::Multiply) {
                auto &L = bin->getLeft();
                auto &R = bin->getRight();

                if (auto *lLit = llvm::dyn_cast<LiteralExpr>(L.get().get())) {
                    if (static_cast<std::uint64_t>(lLit->getLiteralValue()) == sizeofBytes) {
                        return ::acslg::utils::not_null<std::unique_ptr<SymbolicExpr>>{R->clone()};
                    }
                }
                if (auto *rLit = llvm::dyn_cast<LiteralExpr>(R.get().get())) {
                    if (static_cast<std::uint64_t>(rLit->getLiteralValue()) == sizeofBytes) {
                        return ::acslg::utils::not_null<std::unique_ptr<SymbolicExpr>>{L->clone()};
                    }
                }
            }
        }

        return in;
    }

} // namespace acslg::analyzer::symbolic

// The project's file structure makes it difficult to distinguish between internal and external
// header files, so they are directly placed here. It would be better to place external header files
// in a unified `/include` directory, which would also help maintain consistent #include path formats.
namespace acslg::analyzer::symbolic::details {
    inline std::pair<std::string, std::string> getPrefixSuffixAndUpdateMap(
        const SymbolicExpr::GetACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        const SourcePoint &myPoint) {
        if (!config.noStateLabelFunctionAt && myPoint != currentPoint) {
            if (auto it = config.predefinedLabels.find(myPoint);
                it != config.predefinedLabels.end()) {
                return std::pair{"\\at(", ", " + it->second + ")"};
            } else {
                auto label = myPoint.getLabel();
                usedPoints.insert(myPoint);
                return std::pair{"\\at(", ", " + label + ")"};
            }
        }
        return {};
    }
    inline bool isNeedParens(Operator myOp, unsigned parentPrec, bool isRightChild) {
        auto myPrec = getPrecedence(myOp);
        return (myPrec < parentPrec) ||
               (myPrec == parentPrec && isRightChild && !isRightAssociative(myOp));
    }
} // namespace acslg::analyzer::symbolic::details

#endif
