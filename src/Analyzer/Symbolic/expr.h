/**
 * @file expr.h
 * @brief Declares the symbolic expression hierarchy, symbolic addresses, and utilities for ACSL
 *        generation.
 */
#ifndef __ACSLG_SRC_ANALYZER_SYMBOLIC_EXPR_H__
#define __ACSLG_SRC_ANALYZER_SYMBOLIC_EXPR_H__

#include <clang/AST/Type.h>
#include <string>
#include <memory>
#include <optional>
#include <span>
#include <vector>
#include <ranges>
#include <algorithm>
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
    class Symbol;
    class ExprFactory;
    class ExprFactoryScope;
    class ExprHandle;
    class AddrHandle;
    struct SymbolAddrBaseInfo;

    enum class BinaryOp : unsigned {
#define BIN_OP(name, tok, prec, isRightAssoc) name,
#include "operators.def"
    };

    enum class UnaryOp : unsigned {
#define UN_OP(name, tok, prec, isRightAssoc) name,
#include "operators.def"
    };

    namespace detail {
        struct ExprFactoryInternals;
    }

    /**
     * @class SourcePoint
     * @brief Represents a concrete source location together with a label prefix for ACSL emission.
     *
     * SourcePoint keeps a `clang::SourceLocation` plus its owning SourceManager and produces
     * stable labels used to tag synthesized symbols and inserted labels in the rewritten source.
     */
    class SourcePoint {
      public:
        SourcePoint(const SourcePoint &) = default;
        SourcePoint &operator=(const SourcePoint &);
        SourcePoint(SourcePoint &&) = default;
        SourcePoint &operator=(SourcePoint &&);

        /**
         * @brief Build a source point at the start of a function body.
         * @param FD [in] Function declaration owning the body.
         * @param SM [in] Source manager for location resolution.
         * @param LO [in] Language options for lexical queries.
         * @return SourcePoint anchored before the first body token.
         */
        static SourcePoint fromFuncDecl(const clang::FunctionDecl *FD,
                                        const clang::SourceManager &SM,
                                        const clang::LangOptions &LO);

        /**
         * @brief Build a source point immediately before a statement.
         * @param S [in] Statement to reference.
         * @param SM [in] Source manager for location resolution.
         * @param LO [in] Language options for lexical queries.
         * @return SourcePoint at the start token of the statement.
         */
        static SourcePoint fromStmtBefore(const clang::Stmt *S,
                                          const clang::SourceManager &SM,
                                          const clang::LangOptions &LO);

        /**
         * @brief Build a source point immediately after a statement.
         * @param S [in] Statement to reference.
         * @param SM [in] Source manager for location resolution.
         * @param LO [in] Language options for lexical queries.
         * @return SourcePoint at the end of the statement token range.
         */
        static SourcePoint fromStmtAfter(const clang::Stmt *S,
                                         const clang::SourceManager &SM,
                                         const clang::LangOptions &LO);

        /**
         * @brief Strict weak ordering by underlying SourceLocation.
         * @param other [in] SourcePoint to compare.
         * @return True if this point precedes the other.
         */
        bool operator<(const SourcePoint &other) const;

        /**
         * @brief Equality based on SourceLocation identity under the same SourceManager.
         * @param other [in] SourcePoint to compare.
         * @return True if both refer to the same location.
         */
        bool operator==(const SourcePoint &other) const;

        /**
         * @brief Access the underlying `clang::SourceLocation`.
         * @return Location value.
         */
        clang::SourceLocation asSourceLocation() const { return loc_; }

        /**
         * @brief Hash based on the wrapped SourceLocation.
         * @return Hash value.
         */
        size_t hash() const { return utils::hash_val(loc_.getHashValue()); }

        /**
         * @brief Dump a human-readable string representation for diagnostics.
         * @return Formatted description.
         */
        std::string dump() const;

        /**
         * @brief Produce a stable label for ACSL annotations, optionally suffixed with a hash.
         * @return Label string.
         */
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
    template <typename To, typename From> To *dyn_cast(From *from) {
        return dynamic_cast<To *>(from);
    }

    template <typename To, typename From> const To *dyn_cast(const From *from) {
        return dynamic_cast<const To *>(from);
    }

    template <typename To, typename From> std::unique_ptr<To> dyn_cast(std::unique_ptr<From> &from) {
        if (auto *casted = dynamic_cast<To *>(from.get())) {
            from.release();
            return std::unique_ptr<To>(casted);
        }
        return nullptr;
    }

    template <typename To, typename From> To *dyn_cast_if_present(From *from) {
        return from == nullptr ? nullptr : dynamic_cast<To *>(from);
    }

    template <typename To, typename From> const To *dyn_cast_if_present(const From *from) {
        return from == nullptr ? nullptr : dynamic_cast<const To *>(from);
    }

    template <typename To, typename From> bool isa(const From *from) {
        if (from == nullptr)
            return false;
        return dyn_cast<To>(from) != nullptr;
    }

    template <typename To, typename From> bool isa(const std::unique_ptr<From> &from) {
        return isa<To>(from.get());
    }

    template <typename To, typename From>
        requires(!std::is_pointer_v<From>)
    bool isa(const From &from) {
        return dyn_cast<To>(&from) != nullptr;
    }

    template <typename To, typename From> To *cast(From *from) {
        auto *result = dyn_cast<To>(from);
        if (result == nullptr)
            ERROR("Invalid symbolic cast.");
        return result;
    }

    template <typename To, typename From> const To *cast(const From *from) {
        auto *result = dyn_cast<To>(from);
        if (result == nullptr)
            ERROR("Invalid symbolic cast.");
        return result;
    }

    /**
     * @class SymbolicExpr
     * @brief Abstract base for all symbolic expressions and addresses used in analysis.
     */
    class SymbolicExpr {
      public:
        /**
         * @enum ExprKind
         * @brief Enumerates all concrete expression kinds, including address variants.
         */
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

        /**
         * @enum ScalarKind
         * @brief Scalar data types used to describe literal widths and signedness.
         */
        enum class ScalarKind {
            Int,
            UInt,
            Bool,
            Void,
            Structure
        };

        /**
         * @struct Type
         * @brief Represents a scalar type and its bit width for literal/value typing.
         */
        struct Type {
            ScalarKind kind;   ///< Base scalar category (int, uint, bool, etc.).
            unsigned bitWidth; ///< Number of bits for the value (0 when unspecified).

            friend bool operator==(Type lhs, Type rhs) {
                return lhs.kind == rhs.kind && lhs.bitWidth == rhs.bitWidth;
            }
            friend bool operator!=(Type lhs, Type rhs) { return !(lhs == rhs); }
        };

        virtual ~SymbolicExpr()                       = default;
        SymbolicExpr(const SymbolicExpr &)            = delete;
        SymbolicExpr &operator=(const SymbolicExpr &) = delete;
        SymbolicExpr(SymbolicExpr &&)                 = default;
        SymbolicExpr &operator=(SymbolicExpr &&)      = delete;

        static bool classof(const SymbolicExpr *) { return true; }
        static bool classof(const Symbol *) { return true; }

        ExprKind getKind() const { return kind_; }
        Type getValType() const { return valueType_; }

        /// @brief Dump debug string of the expression.
        /// @return Human-readable representation.
        virtual std::string dump() const = 0;

        /**
         * @struct GetACSLConfig
         * @brief Configuration for translating symbolic expressions into ACSL strings.
         */
        struct GetACSLConfig {
            bool noStateLabelFunctionAt{false}; ///< Avoid labeling '\at' when true.
            std::unordered_map<SourcePoint, std::string> predefinedLabels{}; ///< Override labels.
            bool useDerefWithZeroOffset{true}; ///< Prefer `*p` instead of `*(p + 0)`.
            bool UnknownExprAsError{true};     ///< Treat UnknownExpr as fatal when true.

            /**
             * @brief Optional filtering for SourcePoint-dependent output (e.g. `\\at(..., L)`).
             *
             * When `whitelist` is set, SourcePoints not contained in it are treated as if they
             * were the current point: they will not produce `\\at(...)` wrappers and will not be
             * collected into the returned `usedPoints` set.
             */
            struct SourcePointOutputFilter {
                std::optional<std::unordered_set<SourcePoint>> whitelist{std::nullopt};
            };
            SourcePointOutputFilter sourcePointOutputFilter{};
        };

        /**
         * @enum GetACSLError
         * @brief Error categories reported during ACSL conversion.
         */
        enum class GetACSLError {
            HeapAddress,
            PartiallyModifiedStruct,
            UnknownExpr,
        };

        /**
         * @brief Convert the expression into ACSL text, collecting any used SourcePoints.
         * @param config [in] Conversion options controlling label and unknown handling.
         * @param currentPoint [in] Optional substitution point for state labels.
         * @return Expected pair of ACSL string and used points, or an error category.
         */
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
        virtual ExprHandle simplifiedExpr() const;

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
                        const auto same = it->second.get() == v.get();
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

        /**
         * @brief Attempt to evaluate the expression to a concrete integer constant.
         * @return Constant value when expression is linear and homogeneous; nullopt otherwise.
         */
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
        /**
         * @brief Evaluate any fully constant expression using symbolic operator semantics.
         * @return The resulting integer value, or nullopt when evaluation is not possible.
        */
        std::optional<int64_t> tryEvalToConstant() const;

        /**
         * @brief Identify whether this expression represents an unknown value.
         * @return True if the expression is UnknownExpr-derived.
         */
        virtual bool isUnknown() const { return false; };

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
        /// @param kind Concrete expression kind.
        /// @param naturalType Type derived from the node's value and children.
        /// @param explicitType Optional factory-requested type override.
        SymbolicExpr(ExprKind kind,
                     Type naturalType,
                     std::optional<Type> explicitType = std::nullopt)
            : kind_(kind), valueType_(explicitType.value_or(naturalType)) {}

        /// @brief Simplify a linear expression through the active factory.
        ExprHandle simplifiedExprIfLinear() const;

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

      private:
        friend ExprHandle simplifiedExprHandle(ExprFactory &factory, const SymbolicExpr &expr);

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

    class ExprHandle {
      public:
        explicit ExprHandle(const SymbolicExpr *expr)
            : ExprHandle(utils::not_null<const SymbolicExpr *>{expr}) {}
        explicit ExprHandle(utils::not_null<const SymbolicExpr *> expr) : expr_(expr) {}

        utils::not_null<const SymbolicExpr *> get() const { return expr_; }
        const SymbolicExpr &operator*() const { return *expr_; }
        const SymbolicExpr *operator->() const { return expr_.get(); }

        std::size_t hash() const { return expr_->hash(); }
        std::string dump() const { return expr_->dump(); }
        SymbolicExpr::Type getValType() const { return expr_->getValType(); }
        auto getACSL(
            const SymbolicExpr::GetACSLConfig &config,
            std::optional<SourcePoint> currentPoint = std::nullopt) const {
            return expr_->getACSL(config, currentPoint);
        }

        template <typename T> bool isa() const { return symbolic::isa<T>(expr_.get()); }
        template <typename T> const T *dyn_cast() const {
            return symbolic::dyn_cast<T>(expr_.get());
        }
        template <typename T> const T &cast() const { return *symbolic::cast<T>(expr_.get()); }

        friend bool operator==(ExprHandle lhs, ExprHandle rhs) {
            return lhs.expr_.get() == rhs.expr_.get();
        }

      private:
        utils::not_null<const SymbolicExpr *> expr_;
    };

    using HashExprHandleMap = std::unordered_map<size_t, ExprHandle>;
    ExprHandle getSubstitutedValueHandle(ExprFactory &factory,
                                         const SymbolicExpr &expr,
                                         const HashExprHandleMap &hashToExprMap);
    ExprHandle getSubstitutedExprHandle(ExprFactory &factory,
                                        const SymbolicExpr &expr,
                                        const Path &pathSubTo,
                                        const SourcePoint &pointToSub);
    ExprHandle getRangeIndexSubstitutedHandle(ExprFactory &factory,
                                              const SymbolicExpr &expr,
                                              const SymbolAddrBaseInfo &rangeBase,
                                              ExprHandle indexExpr);
    ExprHandle simplifiedExprHandle(ExprFactory &factory, const SymbolicExpr &expr);

    /// Public read-only access to a factory-owned literal expression.
    class LiteralExprView {
      public:
        explicit LiteralExprView(ExprHandle handle);

        static std::optional<LiteralExprView> tryFrom(ExprHandle handle);

        ExprHandle handle() const { return handle_; }
        int64_t value() const;

      private:
        ExprHandle handle_;
    };

    /// Public read-only access to a factory-owned unary expression.
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

    /// Public read-only access to a factory-owned binary expression.
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

    class ExprChild {
      public:
        explicit ExprChild(ExprHandle handle) : handle_(handle) {}

        utils::not_null<const SymbolicExpr *> get() const { return handle_.get(); }

        const SymbolicExpr &operator*() const { return *get(); }
        const SymbolicExpr *operator->() const { return get().get(); }

        ExprHandle handle() const { return handle_; }

      private:
        ExprHandle handle_;
    };

    /**
     * @class Symbol
     * @brief Mix-in for symbolic entities that carry provenance (address and source point).
     */
    class Symbol {
      public:
        virtual ~Symbol()                 = default;
        Symbol(const Symbol &)            = delete;
        Symbol &operator=(const Symbol &) = delete;
        Symbol(Symbol &&)                 = default;
        Symbol &operator=(Symbol &&)      = delete;

        enum class Kind {
            K_Structure,
            K_SymbolAddress,
            K_SymbolValue,
            K_SumOverRange,
            K_MaxMinOverRange,
        };
        Kind getKind() const { return kind_; }

        static bool classof(const SymbolicExpr *e);
        static bool classof(const Symbol *) { return true; }

        static Symbol *toThis(SymbolicExpr *e);
        static const Symbol *toThis(const SymbolicExpr *e);

        /// @brief Downcast to the SymbolicExpr base.
        utils::not_null<SymbolicExpr *> toSymbolicExpr();
        /// @brief Const downcast to the SymbolicExpr base.
        utils::not_null<const SymbolicExpr *> toSymbolicExpr() const;
        /// @brief Source point that created the symbol, if tracked.
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

    /// Record metadata shared by factory-built structure nodes and read-only views.

    struct StructureInfo {
        utils::not_null<const clang::RecordDecl *> definition_;
        const clang::ASTRecordLayout &layout_;

        StructureInfo(const clang::RecordDecl *record, const clang::ASTRecordLayout &layout)
            : definition_(record), layout_(layout) {
            if (!record->isCompleteDefinition())
                ERROR("Incomplete struct definition");
            definition_ = record->getDefinition();
        }

        StructureInfo(const StructureInfo &other)
            : definition_(other.definition_), layout_(other.layout_) {}
        StructureInfo(StructureInfo &&) = default;

        bool equal(const StructureInfo &other) const;
        bool operator==(const StructureInfo &other) const;
        std::string dump() const;
        size_t getNumFields() const { return layout_.getFieldCount(); }
    };

    /// Public read-only access to a factory-owned structure expression.
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

    /**
     * @class Address
     * @brief Base class for symbolic memory addresses (variables, fields, pointer arithmetic).
     */
    class Address : public SymbolicExpr {
      public:
        virtual ~Address()                  = default;
        Address(const Address &)            = delete;
        Address &operator=(const Address &) = delete;
        Address(Address &&)                 = default;
        Address &operator=(Address &&)      = delete;

        static bool classof(const SymbolicExpr *e) {
            auto k = e->getKind();
            return (k > ExprKind::K_FirstAddr) && (k < ExprKind::K_LastAddr);
        }

        /**
         * @brief Convert the pointed-to value into ACSL text.
         * @param config [in] Conversion configuration.
         * @param currentPoint [in] Optional substitution point for labels.
         * @return Expected ACSL string plus used points, or error category.
         */
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

        auto getPointeeType() const -> const auto & { return pointeeType_; }

      protected:
        Address(ExprKind kind,
                Type naturalType,
                const clang::QualType &pointeeType,
                std::optional<Type> explicitType = std::nullopt)
            : SymbolicExpr(kind, naturalType, explicitType), pointeeType_(pointeeType) {};

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
        explicit AddressBox(AddrHandle handle) noexcept;
        AddressBox(const AddressBox &)                = default;
        AddressBox &operator=(const AddressBox &)     = default;
        AddressBox(AddressBox &&) noexcept            = default;
        AddressBox &operator=(AddressBox &&) noexcept = default;

        const Address &get() const { return *ptr_; }
        AddrHandle handle() const;

        friend bool operator==(const AddressBox &a, const AddressBox &b) {
            return a.ptr_->equal(*b.ptr_);
        }

        friend bool operator!=(const AddressBox &a, const AddressBox &b) { return !(a == b); }

        std::size_t hash() const noexcept { return ptr_->hash(); }

      private:
        const Address *ptr_;
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
        bool operator()(const Address &a, const AddressBox &b) const { return a.equal(b.get()); }
    };

    template <class T>
    using AddressBoxMap = std::unordered_map<AddressBox, T, AddressBoxHash, AddressBoxEq>;

    class AddrHandle {
      public:
        explicit AddrHandle(const Address *ptr) : ptr_(ptr) {
            if (ptr_ == nullptr)
                ERROR("AddrHandle cannot wrap null.");
        }

        const Address &operator*() const { return *ptr_; }
        const Address *operator->() const { return ptr_; }
        utils::not_null<const Address *> get() const { return ptr_; }
        ExprHandle asExpr() const { return ExprHandle{ptr_}; }

        std::size_t hash() const { return ptr_->hash(); }
        std::string dump() const { return ptr_->dump(); }
        SymbolicExpr::Type getValType() const { return ptr_->getValType(); }

        template <typename T> bool isa() const {
            return ::acslg::analyzer::symbolic::isa<T>(ptr_);
        }

        template <typename T> const T *dyn_cast() const {
            return ::acslg::analyzer::symbolic::dyn_cast<T>(ptr_);
        }

        template <typename T> const T &cast() const {
            return *::acslg::analyzer::symbolic::cast<T>(ptr_);
        }

        friend bool operator==(AddrHandle lhs, AddrHandle rhs) {
            return lhs.ptr_ == rhs.ptr_;
        }

      private:
        const Address *ptr_;
    };

    inline AddressBox::AddressBox(AddrHandle handle) noexcept : ptr_(handle.get().get()) {}

    inline AddrHandle AddressBox::handle() const { return AddrHandle{ptr_}; }

    class AddressChild {
      public:
        explicit AddressChild(AddrHandle handle) : handle_(handle) {}

        utils::not_null<const Address *> get() const { return handle_.get(); }

        const Address &operator*() const { return *get(); }
        const Address *operator->() const { return get().get(); }
        AddrHandle handle() const { return handle_; }

      private:
        AddrHandle handle_;
    };

    std::optional<AddrHandle> tryEvalAsSymbolAddrHandle(ExprFactory &factory,
                                                        const SymbolicExpr &expr);

    class ExprFactory {
      public:
        ExprHandle literal(bool value);
        ExprHandle literal(int value);
        ExprHandle literal(unsigned int value);
        ExprHandle literal(short value);
        ExprHandle literal(unsigned short value);
        ExprHandle literal(int64_t value);
        ExprHandle literal(uint64_t value);

        ExprHandle unknown();

        ExprHandle rangeIndex(std::string_view name);
        ExprHandle symbolValue(SymbolicExpr::Type varType,
                               AddrHandle from,
                               SourcePoint fromPoint);

        ExprHandle unary(UnaryOp op, ExprHandle expr);

        ExprHandle binary(ExprHandle left, BinaryOp op, ExprHandle right);
        ExprHandle simplifiedBinary(ExprHandle left, BinaryOp op, ExprHandle right);

        ExprHandle withValType(ExprHandle expr, SymbolicExpr::Type newType);

        ExprHandle importExpr(const SymbolicExpr &expr);
        AddrHandle importAddress(const Address &address);
        AddrHandle variableAddress(utils::not_null<const clang::VarDecl *> from);
        AddrHandle symbolAddress(
            clang::QualType pointeeType,
            std::optional<AddrHandle> from,
            SourcePoint fromPoint,
            std::optional<ExprHandle> offset = std::nullopt,
            std::optional<ExprHandle> length = std::nullopt);
        AddrHandle withOffset(AddrHandle address, ExprHandle offset);
        AddrHandle withAddedOffset(AddrHandle address, ExprHandle extra);
        AddrHandle withSubtractedOffset(AddrHandle address, ExprHandle extra);
        AddrHandle withLength(AddrHandle address, ExprHandle length);
        AddrHandle withAddedLength(AddrHandle address, ExprHandle extra);
        AddrHandle withoutLength(AddrHandle address);
        AddrHandle fieldAddress(clang::QualType pointeeType,
                                const clang::RecordDecl *record,
                                AddrHandle baseAddr,
                                size_t fieldIndex);
        ExprHandle structure(const clang::RecordDecl *record,
                             const clang::ASTRecordLayout &layout,
                             AddrHandle from,
                             SourcePoint fromPoint);
        ExprHandle withField(ExprHandle structure, size_t index, ExprHandle value);

        size_t size() const { return owned_.size(); }

      private:
        friend struct detail::ExprFactoryInternals;

        template <typename Node, typename... Args>
        static std::unique_ptr<Node> makeNode(Args &&...args);

        ExprHandle intern(utils::not_null<std::unique_ptr<SymbolicExpr>> node);
        AddrHandle internAddress(utils::not_null<std::unique_ptr<Address>> node);

        std::vector<std::unique_ptr<SymbolicExpr>> owned_;
        std::unordered_map<size_t, std::vector<const SymbolicExpr *>> interned_;
    };

    class ExprFactoryScope {
      public:
        explicit ExprFactoryScope(ExprFactory &factory);
        ~ExprFactoryScope();

        ExprFactoryScope(const ExprFactoryScope &)            = delete;
        ExprFactoryScope(ExprFactoryScope &&)                 = delete;
        ExprFactoryScope &operator=(const ExprFactoryScope &) = delete;
        ExprFactoryScope &operator=(ExprFactoryScope &&)      = delete;

        static ExprFactory &current();
        static bool hasCurrent();

      private:
        ExprFactory *previous_;
        static thread_local ExprFactory *current_;
    };

    class Expr;

    class Addr {
      public:
        Addr(ExprFactory &factory, AddrHandle handle) : factory_(&factory), handle_(handle) {}
        explicit Addr(AddrHandle handle) : Addr(ExprFactoryScope::current(), handle) {}
        explicit Addr(const Address &address)
            : Addr(ExprFactoryScope::current(),
                   ExprFactoryScope::current().importAddress(address)) {}

        static Addr variable(utils::not_null<const clang::VarDecl *> from);
        static Addr symbol(ExprFactory &factory,
                           clang::QualType pointeeType,
                           SourcePoint fromPoint);
        static Addr symbol(clang::QualType pointeeType, SourcePoint fromPoint);
        static Addr symbol(clang::QualType pointeeType,
                           SourcePoint fromPoint,
                           const Expr &offset);
        static Addr symbol(clang::QualType pointeeType,
                           SourcePoint fromPoint,
                           const Expr &offset,
                           const Expr &length);
        static Addr symbol(clang::QualType pointeeType,
                           const Addr &from,
                           SourcePoint fromPoint);
        static Addr symbol(clang::QualType pointeeType,
                           const Addr &from,
                           SourcePoint fromPoint,
                           const Expr &offset);
        static Addr symbol(clang::QualType pointeeType,
                           const Addr &from,
                           SourcePoint fromPoint,
                           const Expr &offset,
                           const Expr &length);

        const Address &operator*() const { return *handle_; }
        const Address *operator->() const { return handle_.get().get(); }
        AddrHandle handle() const { return handle_; }
        ExprFactory &factory() const { return *factory_; }

        std::size_t hash() const { return handle_.hash(); }
        std::string dump() const { return handle_.dump(); }
        SymbolicExpr::Type getValType() const { return handle_.getValType(); }
        auto getACSL(
            const SymbolicExpr::GetACSLConfig &config,
            std::optional<SourcePoint> currentPoint = std::nullopt) const {
            return handle_.asExpr().getACSL(config, currentPoint);
        }
        auto getACSLOfValue(
            const SymbolicExpr::GetACSLConfig &config,
            std::optional<SourcePoint> currentPoint = std::nullopt) const {
            return handle_->getACSLOfValue(config, currentPoint);
        }
        Expr asExpr() const;
        Addr withOffset(const Expr &offset) const;
        Addr withAddedOffset(const Expr &extra) const;
        Addr withSubtractedOffset(const Expr &extra) const;
        Addr withLength(const Expr &length) const;
        Addr withAddedLength(const Expr &extra) const;
        Addr withoutLength() const;
        Addr field(clang::QualType pointeeType,
                   const clang::RecordDecl *record,
                   size_t fieldIndex) const;

        template <typename T> bool isa() const { return handle_.isa<T>(); }
        template <typename T> const T *dyn_cast() const { return handle_.dyn_cast<T>(); }
        template <typename T> const T &cast() const { return handle_.cast<T>(); }

        friend bool operator==(const Addr &lhs, const Addr &rhs) {
            return lhs.factory_ == rhs.factory_ && lhs.handle_ == rhs.handle_;
        }

      private:
        void ensureSameFactory(const Expr &expr) const;

        ExprFactory *factory_;
        AddrHandle handle_;
    };

    class Expr {
      public:
        Expr(ExprFactory &factory, ExprHandle handle) : factory_(&factory), handle_(handle) {}
        explicit Expr(ExprHandle handle) : Expr(ExprFactoryScope::current(), handle) {}
        explicit Expr(const SymbolicExpr &expr)
            : Expr(ExprFactoryScope::current(),
                   ExprFactoryScope::current().importExpr(expr)) {}

        static Expr unknown() {
            auto &factory = ExprFactoryScope::current();
            return Expr{factory, factory.unknown()};
        }
        static Expr rangeIndex(std::string_view name) {
            auto &factory = ExprFactoryScope::current();
            return Expr{factory, factory.rangeIndex(name)};
        }
        static Expr symbolValue(SymbolicExpr::Type varType,
                                const Addr &from,
                                SourcePoint fromPoint) {
            return Expr{from.factory(), from.factory().symbolValue(varType, from.handle(), fromPoint)};
        }

        const SymbolicExpr &operator*() const { return *handle_; }
        const SymbolicExpr *operator->() const { return handle_.get().get(); }
        ExprHandle handle() const { return handle_; }
        ExprFactory &factory() const { return *factory_; }

        std::size_t hash() const { return handle_.hash(); }
        std::string dump() const { return handle_.dump(); }
        SymbolicExpr::Type getValType() const { return handle_.getValType(); }
        auto getACSL(
            const SymbolicExpr::GetACSLConfig &config,
            std::optional<SourcePoint> currentPoint = std::nullopt) const {
            return handle_.getACSL(config, currentPoint);
        }
        Expr withType(SymbolicExpr::Type newType) const {
            return Expr{factory(), factory().withValType(handle_, newType)};
        }
        Expr simplified() const {
            return Expr{factory(), simplifiedExprHandle(factory(), *handle_)};
        }

        template <typename T> bool isa() const { return handle_.isa<T>(); }
        template <typename T> const T *dyn_cast() const { return handle_.dyn_cast<T>(); }
        template <typename T> const T &cast() const { return handle_.cast<T>(); }

        Expr binary(BinaryOp op, const Expr &rhs) const {
            ensureSameFactory(rhs);
            return Expr{factory(), factory().binary(handle_, op, rhs.handle_)};
        }

        Expr unary(UnaryOp op) const {
            return Expr{factory(), factory().unary(op, handle_)};
        }

        Expr equalTo(const Expr &rhs) const {
            return binary(BinaryOp::Equal, rhs);
        }
        Expr notEqualTo(const Expr &rhs) const {
            return binary(BinaryOp::NotEqual, rhs);
        }
        Expr lessThan(const Expr &rhs) const {
            return binary(BinaryOp::LessThan, rhs);
        }
        Expr lessEqual(const Expr &rhs) const {
            return binary(BinaryOp::LessEqual, rhs);
        }
        Expr greaterThan(const Expr &rhs) const {
            return binary(BinaryOp::GreaterThan, rhs);
        }
        Expr greaterEqual(const Expr &rhs) const {
            return binary(BinaryOp::GreaterEqual, rhs);
        }
        Expr logicalAnd(const Expr &rhs) const {
            return binary(BinaryOp::LogicalAnd, rhs);
        }
        Expr logicalOr(const Expr &rhs) const {
            return binary(BinaryOp::LogicalOr, rhs);
        }
        Expr logicalNot() const {
            return unary(UnaryOp::LogicalNot);
        }

        friend bool operator==(const Expr &lhs, const Expr &rhs) {
            return lhs.factory_ == rhs.factory_ && lhs.handle_ == rhs.handle_;
        }

        friend Expr operator+(const Expr &lhs, const Expr &rhs) {
            return lhs.binary(BinaryOp::Add, rhs);
        }
        friend Expr operator-(const Expr &lhs, const Expr &rhs) {
            return lhs.binary(BinaryOp::Subtract, rhs);
        }
        friend Expr operator*(const Expr &lhs, const Expr &rhs) {
            return lhs.binary(BinaryOp::Multiply, rhs);
        }
        friend Expr operator/(const Expr &lhs, const Expr &rhs) {
            return lhs.binary(BinaryOp::Divide, rhs);
        }
        friend Expr operator-(const Expr &expr) {
            return expr.unary(UnaryOp::Minus);
        }
        friend Expr operator!(const Expr &expr) {
            return expr.logicalNot();
        }

      private:
        void ensureSameFactory(const Expr &rhs) const {
            if (factory_ != rhs.factory_)
                ERROR("Cannot combine expressions from different factories.");
        }

        ExprFactory *factory_;
        ExprHandle handle_;
    };

    inline Expr Addr::asExpr() const {
        return Expr{factory(), handle_.asExpr()};
    }

    inline Addr Addr::variable(utils::not_null<const clang::VarDecl *> from) {
        auto &factory = ExprFactoryScope::current();
        return Addr{factory, factory.variableAddress(from)};
    }

    inline Addr Addr::symbol(ExprFactory &factory,
                             clang::QualType pointeeType,
                             SourcePoint fromPoint) {
        return Addr{factory, factory.symbolAddress(pointeeType, std::nullopt, fromPoint)};
    }

    inline Addr Addr::symbol(clang::QualType pointeeType, SourcePoint fromPoint) {
        auto &factory = ExprFactoryScope::current();
        return symbol(factory, pointeeType, fromPoint);
    }

    inline Addr Addr::symbol(clang::QualType pointeeType,
                             SourcePoint fromPoint,
                             const Expr &offset) {
        return Addr{offset.factory(),
                    offset.factory().symbolAddress(pointeeType, std::nullopt, fromPoint,
                                                   offset.handle())};
    }

    inline Addr Addr::symbol(clang::QualType pointeeType,
                             SourcePoint fromPoint,
                             const Expr &offset,
                             const Expr &length) {
        if (&offset.factory() != &length.factory())
            ERROR("Cannot build address with range expressions from different factories.");
        return Addr{offset.factory(),
                    offset.factory().symbolAddress(pointeeType, std::nullopt, fromPoint,
                                                   offset.handle(), length.handle())};
    }

    inline Addr Addr::symbol(clang::QualType pointeeType,
                             const Addr &from,
                             SourcePoint fromPoint) {
        return Addr{from.factory(),
                    from.factory().symbolAddress(pointeeType, from.handle(), fromPoint)};
    }

    inline Addr Addr::symbol(clang::QualType pointeeType,
                             const Addr &from,
                             SourcePoint fromPoint,
                             const Expr &offset) {
        from.ensureSameFactory(offset);
        return Addr{from.factory(),
                    from.factory().symbolAddress(pointeeType, from.handle(), fromPoint,
                                                 offset.handle())};
    }

    inline Addr Addr::symbol(clang::QualType pointeeType,
                             const Addr &from,
                             SourcePoint fromPoint,
                             const Expr &offset,
                             const Expr &length) {
        from.ensureSameFactory(offset);
        from.ensureSameFactory(length);
        return Addr{from.factory(),
                    from.factory().symbolAddress(pointeeType, from.handle(), fromPoint,
                                                 offset.handle(), length.handle())};
    }

    inline Addr Addr::withOffset(const Expr &offset) const {
        ensureSameFactory(offset);
        return Addr{factory(), factory().withOffset(handle_, offset.handle())};
    }

    inline Addr Addr::withAddedOffset(const Expr &extra) const {
        ensureSameFactory(extra);
        return Addr{factory(), factory().withAddedOffset(handle_, extra.handle())};
    }

    inline Addr Addr::withSubtractedOffset(const Expr &extra) const {
        ensureSameFactory(extra);
        return Addr{factory(), factory().withSubtractedOffset(handle_, extra.handle())};
    }

    inline Addr Addr::withLength(const Expr &length) const {
        ensureSameFactory(length);
        return Addr{factory(), factory().withLength(handle_, length.handle())};
    }

    inline Addr Addr::withAddedLength(const Expr &extra) const {
        ensureSameFactory(extra);
        return Addr{factory(), factory().withAddedLength(handle_, extra.handle())};
    }

    inline Addr Addr::withoutLength() const {
        return Addr{factory(), factory().withoutLength(handle_)};
    }

    inline Addr Addr::field(clang::QualType pointeeType,
                            const clang::RecordDecl *record,
                            size_t fieldIndex) const {
        return Addr{factory(), factory().fieldAddress(pointeeType, record, handle_, fieldIndex)};
    }

    inline void Addr::ensureSameFactory(const Expr &expr) const {
        if (factory_ != &expr.factory())
            ERROR("Cannot rebuild address with expression from a different factory.");
    }

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

    class LiteralExpr : public Expr {
      public:
        explicit LiteralExpr(bool value) : Expr(make(value)) {}
        explicit LiteralExpr(int value) : Expr(make(value)) {}
        explicit LiteralExpr(unsigned int value) : Expr(make(value)) {}
        explicit LiteralExpr(short value) : Expr(make(value)) {}
        explicit LiteralExpr(unsigned short value) : Expr(make(value)) {}
        explicit LiteralExpr(int64_t value) : Expr(make(value)) {}
        explicit LiteralExpr(uint64_t value) : Expr(make(value)) {}
        LiteralExpr(ExprFactory &factory, bool value) : Expr(make(factory, value)) {}
        LiteralExpr(ExprFactory &factory, int value) : Expr(make(factory, value)) {}
        LiteralExpr(ExprFactory &factory, unsigned int value) : Expr(make(factory, value)) {}
        LiteralExpr(ExprFactory &factory, short value) : Expr(make(factory, value)) {}
        LiteralExpr(ExprFactory &factory, unsigned short value) : Expr(make(factory, value)) {}
        LiteralExpr(ExprFactory &factory, int64_t value) : Expr(make(factory, value)) {}
        LiteralExpr(ExprFactory &factory, uint64_t value) : Expr(make(factory, value)) {}

      private:
        template <typename T> static Expr make(T value) {
            auto &factory = ExprFactoryScope::current();
            return make(factory, value);
        }

        template <typename T> static Expr make(ExprFactory &factory, T value) {
            return Expr{factory, factory.literal(value)};
        }
    };

    struct SymbolAddrBaseInfo {
        std::optional<AddressChild> fromAddr_;
        SourcePoint fromPoint_;
        clang::QualType pointeeType_;

        SymbolAddrBaseInfo(std::optional<AddrHandle> fromAddr,
                           SourcePoint fromPoint,
                           clang::QualType pointeeType)
            : fromAddr_(std::nullopt), fromPoint_(std::move(fromPoint)),
              pointeeType_(pointeeType) {
            if (fromAddr)
                fromAddr_.emplace(*fromAddr);
        }
        SymbolAddrBaseInfo(const SymbolAddrBaseInfo &);
        SymbolAddrBaseInfo &operator=(const SymbolAddrBaseInfo &other) {
            if (&other == this)
                return *this;
            if (other.fromAddr_)
                fromAddr_.emplace(other.fromAddr_.value());
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

    struct AddressHash {
        std::size_t operator()(const Address &addr) const noexcept { return addr.hash(); }
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

    BinaryOp getCompoundAssignOp(clang::BinaryOperatorKind compoundAssignOp);
    BinaryOp getBinaryOp(clang::BinaryOperatorKind op);
    SymbolicExpr::Type deriveType(clang::QualType type);
    bool isValidOffsetOrLength(const SymbolicExpr &expr);

    bool is_symbol_addr(AddrHandle address) noexcept;

    bool isFrom(ExprHandle expr, AddrHandle fromAddr, SourcePoint fromPoint);
    std::optional<AddrHandle> getFromAddrHandle(ExprFactory &factory, const Symbol &symbol);
    std::optional<AddrHandle> getFromAddrHandle(ExprFactory &factory, ExprHandle symbol);

    ExprHandle getSymbol(clang::QualType type,
                         std::optional<AddrHandle> from,
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
    template <> struct hash<acslg::analyzer::symbolic::SymbolAddrBaseInfo> {
        size_t operator()(const acslg::analyzer::symbolic::SymbolAddrBaseInfo &bi) const noexcept {
            return bi.hash();
        }
    };
} // namespace std

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
    inline ExprHandle strip_sizeof_factor(ExprFactory &factory,
                                          ExprHandle in,
                                          std::uint64_t sizeofBytes) {
        if (auto lit = LiteralExprView::tryFrom(in)) {
            const auto value = static_cast<std::uint64_t>(lit->value());
            return value == sizeofBytes ? factory.literal(std::uint64_t{1}) : in;
        }

        if (auto bin = BinaryExprView::tryFrom(in);
            bin && bin->operation() == BinaryOp::Multiply) {
            auto left  = bin->left();
            auto right = bin->right();
            if (auto literal = LiteralExprView::tryFrom(left);
                literal && static_cast<std::uint64_t>(literal->value()) == sizeofBytes)
                return factory.importExpr(*right);
            if (auto literal = LiteralExprView::tryFrom(right);
                literal && static_cast<std::uint64_t>(literal->value()) == sizeofBytes)
                return factory.importExpr(*left);
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
            if (config.sourcePointOutputFilter.whitelist) {
                const auto &wl = *config.sourcePointOutputFilter.whitelist;
                if (!wl.contains(myPoint))
                    return {};
            }

            if (auto it = config.predefinedLabels.find(myPoint);
                it != config.predefinedLabels.end()) {
                return std::pair{"\\at(", ", " + it->second + ")"};
            }

            auto label = myPoint.getLabel();
            usedPoints.insert(myPoint);
            return std::pair{"\\at(", ", " + label + ")"};
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
