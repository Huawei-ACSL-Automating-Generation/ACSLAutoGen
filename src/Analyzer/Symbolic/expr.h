/// \file symbolic.h
/// @brief Declarations for symbolic expression hierarchy and utilities.
#ifndef SYMBOLIC_H
#define SYMBOLIC_H

#include <string>
#include <memory>
#include <span>
#include <ranges>
#include <algorithm>
#include <ppl.hh>
#include <clang/AST/Decl.h>
#include <clang/AST/Expr.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/RecordLayout.h>
#include <variant>
#include "macros.h"
#include "Utils/utils.h"

using namespace acslg;

namespace Symbolic {
    class Address;
    class SymbolAddress;
    class Variable;
    class LiteralExpr;

    /// @class SymbolicExpr
    /// @brief Base class for all symbolic expressions.
    class SymbolicExpr {
      public:
        /// @class SymbolicExpr
        /// @brief Base class for all symbolic expressions.
        enum class ExprType {
            Literal,
            Variable,
            Address,
            BinaryOp,
            UnaryOp,
            Structure,
            Unknown
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

        /// @brief Construct a symbolic expression.
        /// @param type Expression type
        /// @param valueType Underlying value type
        SymbolicExpr(ExprType type, Type valueType) : type_(type), valueType_(valueType) {}

        virtual ~SymbolicExpr()                       = default;
        SymbolicExpr(const SymbolicExpr &)            = default;
        SymbolicExpr &operator=(const SymbolicExpr &) = default;
        SymbolicExpr(SymbolicExpr &&)                 = default;
        SymbolicExpr &operator=(SymbolicExpr &&)      = default;

        ExprType getType() const { return type_; }
        Type getValType() const { return valueType_; }
        void setValType(Type newType) { valueType_ = newType; }

        /// @brief Clone the expression.
        /// @return Deep copy of the expression.
        virtual not_null<std::unique_ptr<SymbolicExpr>> clone() const = 0;

        /// @brief Dump debug string of the expression.
        /// @return Human-readable representation.
        virtual std::string dump() const = 0;

        /// @brief Emit expression in ACSL-compliant regular form.
        /// @param prefix Optional variable prefix.
        /// @param suffix Optional variable suffix.
        /// @param parentPrec Precedence of parent operator.
        /// @param isRightChild Whether this is right operand.
        /// @return String in ACSL syntax.
        virtual std::optional<std::string> regularForm(
            std::optional<std::string_view> prefix = std::nullopt,
            std::optional<std::string_view> suffix = std::nullopt,
            int parentPrec                         = 0,
            bool isRightChild                      = false) const = 0;

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
            if (LHS.type_ != RHS.type_)
                return false;
            return LHS.equal(RHS);
        }

        /// @brief Get a simplified version of the expression.
        /// @return Simplified expression.
        virtual not_null<std::unique_ptr<SymbolicExpr>> simplifiedExpr() const = 0;

        using UsedMap = std::unordered_map<
            size_t,
            std::variant<not_null<const Variable *>, not_null<const SymbolAddress *>>>;
        using HashIdMap = std::unordered_map<size_t, size_t>;
        /// @brief Collect Variables and Addresses used in the expression.
        /// @return Map from hash to Variable and Address pointer.
        virtual UsedMap collectUsedVarsAndAddrs() const { return {}; };

        template <typename... Exprs>
        static std::pair<UsedMap, HashIdMap> collectUsedVarsAndAddrs(const SymbolicExpr &first,
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

            mergeIntoOne(first.collectUsedVarsAndAddrs());
            (mergeIntoOne(rest.collectUsedVarsAndAddrs()), ...);

            size_t index = 0;
            std::unordered_map<size_t, size_t> hashIndexMap{};
            for (auto &[hash, _] : merged) {
                hashIndexMap[hash] = index++;
            }
            return std::pair{std::move(merged), std::move(hashIndexMap)};
        }

        /// @brief Try to evaluate the expression to an address.
        /// @return Returning `std::nullopt` indicates that the expression is not a valid address.
        virtual std::optional<not_null<std::unique_ptr<SymbolAddress>>> tryEvalAsOffsetedAddr()
            const {
            // TODO: cache the result.
            return std::nullopt;
        };

        // May merge `tryEvalAsConstant` and `evalToConstExpr` into one.
        std::optional<int64_t> tryEvalAsConstant() const {
            // TODO: cache the result.
            if (!isLinear())
                return std::nullopt;
            auto hashPtrMap = collectUsedVarsAndAddrs();
            std::unordered_map<size_t, size_t> hashIdMap;
            size_t counter = 0;
            for (auto &[hash, _] : hashPtrMap) {
                hashIdMap[hash] = counter++;
            }
            auto linearExpr = toLinearExpr(hashIdMap);
            if (linearExpr.all_homogeneous_terms_are_zero())
                return linearExpr.inhomogeneous_term().get_si();
            return std::nullopt;
        };
        virtual std::unique_ptr<LiteralExpr> evalToConstExpr() const { return nullptr; }

        /// @brief Is an unknown expression?
        /// @return
        virtual bool isUnknown() const { return false; };

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
        /// Only support varDecl's value(`Address` and `Variable`, see their `toLinearExpr` for more
        /// details), return nullopt otherwise.
        /// @param varMap Mapping from names to the index of the Cartesian axis.
        /// @return PPL linear expression or nullopt if contains symbolic value from pointer, array,
        /// etc.
        virtual std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const {
            ERROR("not implemented for expression type: ");
        }

        /// @brief Convert to PPL linear expression without custom mapping.
        /// Use Variable's id_ as its index of the Cartesian axis.
        /// @param hashIdMap Mapping from hash of variable/symbolAddress to the index of the
        /// Cartesian axis.  Will throw an error if a non-existent hash is encountered. The ID
        /// represents the dimension of variables in the PPL library, so hashIdMap should be a
        /// sequentially numbered mapping of hash values, such as {{hash_1: 0}, {hash_2: 1}, ...}.
        /// @return PPL linear expression.
        virtual Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const std::unordered_map<size_t, size_t> &) const {
            ERROR("not implemented for expression type: ");
        }

      protected:
        /// @brief Simplify expression if it's linear, just call clone() otherwise.
        not_null<std::unique_ptr<SymbolicExpr>> simplifiedExprIfLinear() const;

      private:
        ExprType type_;  ///< Kind of expression
        Type valueType_; ///< Underlying type
    };

    std::ostream &operator<<(std::ostream &os, SymbolicExpr::ExprType t);

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
            : SymbolicExpr(ExprType::Literal, {ScalarKind::Bool, 1}), type_(LiteralType::Boolean) {
            data_.boolValue = value;
        }

        LiteralExpr(int value)
            : SymbolicExpr(ExprType::Literal, {ScalarKind::Int, 32}), type_(LiteralType::Int) {
            data_.intValue = value;
        }

        LiteralExpr(unsigned int value)
            : SymbolicExpr(ExprType::Literal, {ScalarKind::UInt, 32}),
              type_(LiteralType::UnsignedInt) {
            data_.uintValue = value;
        }

        LiteralExpr(short value)
            : SymbolicExpr(ExprType::Literal, {ScalarKind::Int, 16}), type_(LiteralType::Short) {
            data_.shortValue = value;
        }

        LiteralExpr(unsigned short value)
            : SymbolicExpr(ExprType::Literal, {ScalarKind::UInt, 16}),
              type_(LiteralType::UnsignedShort) {
            data_.ushortValue = value;
        }

        LiteralExpr(int64_t value)
            : SymbolicExpr(ExprType::Literal, {ScalarKind::Int, 64}), type_(LiteralType::Int64) {
            data_.int64Value = value;
        }

        LiteralExpr(uint64_t value)
            : SymbolicExpr(ExprType::Literal, {ScalarKind::UInt, 64}), type_(LiteralType::UInt64) {
            data_.uint64Value = value;
        }

        LiteralType getLiteralType() const { return type_; }

        not_null<std::unique_ptr<SymbolicExpr>> clone() const override;
        std::string dump() const override;
        virtual std::optional<std::string> regularForm(
            std::optional<std::string_view> prefix = std::nullopt,
            std::optional<std::string_view> suffix = std::nullopt,
            int parentPrec                         = 0,
            bool isRightChild                      = false) const override;
        virtual not_null<std::unique_ptr<SymbolicExpr>> simplifiedExpr() const override;
        virtual std::size_t hash() const override;
        std::unique_ptr<LiteralExpr> evalToConstExpr() const override;

        virtual bool equal(const SymbolicExpr &expr) const override;

        // StInG: Support functions for affine invariant analysis
        bool isLinear() const override { return true; }
        int getMaxDegree() const override { return 0; }
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const std::unordered_map<size_t, size_t> &) const override;
        int64_t getLiteralValue() const;

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
        enum class Operator {
#define BIN_OP(name, tok, prec, isRight) name,
#include "operators.def"
        };

        inline static int getPrecedence(BinaryOpExpr::Operator op) {
            switch (op) {
#define BIN_OP(name, tok, prec, right)                                                             \
    case BinaryOpExpr::Operator::name: return prec;
#include "operators.def"
                default: ERROR("Unknown Operator");
            }
        }

        inline static bool isRightAssociative(BinaryOpExpr::Operator op) {
            switch (op) {
#define BIN_OP(name, tok, prec, right)                                                             \
    case BinaryOpExpr::Operator::name: return right;
#include "operators.def"
                default: ERROR("Unknown operator");
            }
        }

        // TODO(style): May use template to unify constructors.
        BinaryOpExpr(not_null<std::unique_ptr<SymbolicExpr>> left,
                     Operator op,
                     not_null<std::unique_ptr<SymbolicExpr>> right)
            : SymbolicExpr(ExprType::BinaryOp, left->getValType()), left_(std::move(left)), op_(op),
              right_(std::move(right)) {}

        BinaryOpExpr(not_null<SymbolicExpr *> left, Operator op, not_null<SymbolicExpr *> right)
            : SymbolicExpr(ExprType::BinaryOp, left->getValType()),
              left_(std::unique_ptr<SymbolicExpr>{left}), op_(op),
              right_(std::unique_ptr<SymbolicExpr>{right}) {}

        not_null<const SymbolicExpr *> getLeft() const { return left_.get().get(); }
        not_null<const SymbolicExpr *> getRight() const { return right_.get().get(); }
        auto getLeft() -> auto & { return left_; }
        auto getRight() -> auto & { return right_; }
        Operator getOperator() const { return op_; }

        not_null<std::unique_ptr<SymbolicExpr>> clone() const override;
        std::string dump() const override;
        virtual std::optional<std::string> regularForm(
            std::optional<std::string_view> prefix = std::nullopt,
            std::optional<std::string_view> suffix = std::nullopt,
            int parentPrec                         = 0,
            bool isRightChild                      = false) const override;
        virtual not_null<std::unique_ptr<SymbolicExpr>> simplifiedExpr() const override;
        virtual std::size_t hash() const override;
        std::unique_ptr<LiteralExpr> evalToConstExpr() const override;

        virtual bool equal(const SymbolicExpr &expr) const override;
        virtual std::optional<not_null<std::unique_ptr<SymbolAddress>>> tryEvalAsOffsetedAddr()
            const override;
        virtual bool isUnknown() const override {
            return left_->isUnknown() || right_->isUnknown();
        };

        // StInG: Support functions for affine invariant analysis
        UsedMap collectUsedVarsAndAddrs() const override;
        bool isLinear() const override;
        int getMaxDegree() const override;
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const std::unordered_map<size_t, size_t> &) const override;

      private:
        not_null<std::unique_ptr<SymbolicExpr>> left_;
        Operator op_;
        not_null<std::unique_ptr<SymbolicExpr>> right_;
    };

    /// @class UnaryOpExpr
    /// @brief Represents a unary operation expression.
    class UnaryOpExpr : public SymbolicExpr {
      public:
        enum class Operator {
#define UN_OP(name, tok, prec, isRight) name,
#include "operators.def"
        };

        inline static int getPrecedence(UnaryOpExpr::Operator op) {
            switch (op) {
#define UN_OP(name, tok, prec, right)                                                              \
    case UnaryOpExpr::Operator::name: return prec;
#include "operators.def"
                default: ERROR("Unknown Operator");
            }
        }

        inline static bool isRightAssociative(UnaryOpExpr::Operator op) {
            switch (op) {
#define UN_OP(name, tok, prec, right)                                                              \
    case UnaryOpExpr::Operator::name: return right;
#include "operators.def"
                default: ERROR("Unknown operator");
            }
        }

        UnaryOpExpr(Operator op, not_null<std::unique_ptr<SymbolicExpr>> expr)
            : SymbolicExpr(ExprType::UnaryOp, expr->getValType()), op_(op), expr_(std::move(expr)) {
        }

        not_null<const SymbolicExpr *> getSub() const { return expr_.get().get(); }
        auto getSub() -> auto & { return expr_; }
        Operator getOperator() const { return op_; }

        not_null<std::unique_ptr<SymbolicExpr>> clone() const override;
        std::string dump() const override;
        virtual std::optional<std::string> regularForm(
            std::optional<std::string_view> prefix = std::nullopt,
            std::optional<std::string_view> suffix = std::nullopt,
            int parentPrec                         = 0,
            bool isRightChild                      = false) const override;
        virtual not_null<std::unique_ptr<SymbolicExpr>> simplifiedExpr() const override;
        virtual std::size_t hash() const override;
        std::unique_ptr<LiteralExpr> evalToConstExpr() const override;

        virtual bool equal(const SymbolicExpr &expr) const override;
        virtual bool isUnknown() const override { return expr_->isUnknown(); };

        // StInG: Support functions for affine invariant analysis
        UsedMap collectUsedVarsAndAddrs() const override;
        bool isLinear() const override;
        int getMaxDegree() const override;
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const std::unordered_map<size_t, size_t> &) const override;

      private:
        Operator op_;
        not_null<std::unique_ptr<SymbolicExpr>> expr_;
    };

    /// @class UnknownExpr
    /// @brief Represents a unknown symbolic expression, primarily used to denote cases beyond
    /// capabilities.
    class UnknownExpr : public SymbolicExpr {
      public:
        UnknownExpr() : SymbolicExpr(ExprType::Unknown, {ScalarKind::Void, 0}) {}
        ~UnknownExpr() = default;

        /// @brief Create a unknown symbolic expression.
        /// @return Unique pointer to a Unknown expression.
        static not_null<std::unique_ptr<UnknownExpr>> makeUnknown();

        not_null<std::unique_ptr<SymbolicExpr>> clone() const override;
        std::string dump() const override;
        virtual std::optional<std::string> regularForm(
            std::optional<std::string_view> prefix = std::nullopt,
            std::optional<std::string_view> suffix = std::nullopt,
            int parentPrec                         = 0,
            bool isRightChild                      = false) const override;
        virtual not_null<std::unique_ptr<SymbolicExpr>> simplifiedExpr() const override;
        virtual std::size_t hash() const override;
        virtual bool equal(const SymbolicExpr &expr) const override;
        virtual bool isUnknown() const override { return true; };

        // StInG: Support functions for affine invariant analysis
        bool isLinear() const override { return false; }
        int getMaxDegree() const override { return 0; }
    };

    class Symbol {
      public:
        virtual ~Symbol()                 = default;
        Symbol()                          = default;
        Symbol(const Symbol &)            = default;
        Symbol &operator=(const Symbol &) = default;
        Symbol(Symbol &&)                 = default;
        Symbol &operator=(Symbol &&)      = default;
        virtual std::variant<std::monostate, not_null<std::unique_ptr<const Symbolic::Address>>> getFrom()
            const = 0;
    };

    class Structure : public SymbolicExpr, public Symbol {
      public:
        struct Info {
            not_null<const clang::RecordDecl *> definition_;
            const clang::ASTRecordLayout &layout_;
            Info(const clang::RecordDecl *RD, const clang::ASTRecordLayout &layout)
                : definition_(RD) /*not_null has no default constructor*/, layout_(layout) {
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

        Structure(
            const clang::RecordDecl *RD,
            const clang::ASTRecordLayout &layout,
            std::variant<std::monostate, not_null<std::unique_ptr<const Symbolic::Address>>> from);

        Structure(const Structure &other) : SymbolicExpr(other), info_(other.info_) {
            fields_.clear();
            fields_.reserve(other.fields_.size());
            std::ranges::transform(other.fields_, std::back_inserter(fields_),
                                   [](auto &field) -> not_null<std::unique_ptr<SymbolicExpr>> {
                                       return field->clone();
                                   });
        }

        size_t getNumFields() const { return info_.getNumFields(); }
        void setFieldValue(size_t index, not_null<std::unique_ptr<SymbolicExpr>> expr);
        not_null<const SymbolicExpr *> getFieldValue(size_t index) const {
            if (index >= fields_.size())
                ERROR("Out-of-bounds access");
            return fields_[index].get().get();
        };
        not_null<std::unique_ptr<SymbolicExpr>> &getFieldValue(size_t index) {
            if (index >= fields_.size())
                ERROR("Out-of-bounds access");
            return fields_[index];
        };
        auto fieldsValues() { return std::span{fields_}; }
        auto fieldsValues() const {
            return fields_ | std::views::transform(
                                 [](auto const &up) -> not_null<const Symbolic::SymbolicExpr *> {
                                     return up.value().get().get();
                                 });
        }
        auto getInfo() const -> const auto & { return info_; }

        not_null<std::unique_ptr<SymbolicExpr>> clone() const override;
        std::string dump() const override;
        std::variant<std::monostate, not_null<std::unique_ptr<const Address>>> getFrom()
            const override;
        std::optional<std::string> regularForm(
            std::optional<std::string_view> prefix = std::nullopt,
            std::optional<std::string_view> suffix = std::nullopt,
            int parentPrec                         = 0,
            bool isRightChild                      = false) const override;
        std::optional<std::string> regularFormOfField(
            size_t index,
            std::optional<std::string_view> prefix = std::nullopt,
            std::optional<std::string_view> suffix = std::nullopt,
            int parentPrec                         = 0,
            bool isRightChild                      = false) const;
        not_null<std::unique_ptr<SymbolicExpr>> simplifiedExpr() const override;
        virtual std::size_t hash() const override;
        bool equal(const SymbolicExpr &expr) const override;

        // StInG: Support functions for affine invariant analysis
        bool isLinear() const override {
            WARN("Met Structure in isLinear.");
            return false;
        }
        int getMaxDegree() const override {
            WARN("Met Structure in getMaxDegree.");
            return -1;
        }

      private:
        Info info_;
        std::vector<not_null<std::unique_ptr<SymbolicExpr>>> fields_;
    };

    class Address : public SymbolicExpr {
      public:
        virtual ~Address()                  = default;
        Address(const Address &)            = default;
        Address &operator=(const Address &) = default;
        Address(Address &&)                 = default;
        Address &operator=(Address &&)      = default;
        enum class AddressType {
            SymbolAddr,
            VariableAddr,
            FieldAddr
        };
        Address(AddressType addrType)
            : SymbolicExpr(SymbolicExpr::ExprType::Address,
                           SymbolicExpr::Type{SymbolicExpr::ScalarKind::UInt, 64}),
              addrType_(addrType) {};
        virtual std::optional<std::string> regularFormOfValue(
            std::optional<std::string_view> prefix = std::nullopt,
            std::optional<std::string_view> suffix = std::nullopt,
            int parentPrec                         = 0,
            bool isRightChild                      = false) const                                        = 0;
        virtual std::optional<not_null<const clang::VarDecl *>> getFromRoot() const = 0;
        virtual int getDimension() const                                            = 0;
        virtual not_null<std::unique_ptr<Address>> addressClone() const             = 0;

        auto getAddressType() const -> const auto & { return addrType_; }

      private:
        AddressType addrType_;
    };

    class AddressBox {
      public:
        using pointer = not_null<std::unique_ptr<Address>>;

        explicit AddressBox(pointer p) noexcept : ptr_(std::move(p)) {}
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
        pointer ptr_;
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
    /// @brief Symbolic address with unique ID, optional from, offset and length.
    /// From can't be nullptr, use monostate or nullopt.
    class SymbolAddress : public Address, public Symbol {
      private:
        struct Range {
            not_null<std::unique_ptr<const SymbolicExpr>>
                len_; ///< The length of an address. The Address type does not store pointer types
                      ///< currently, thus it does not support C-style pointer conversion.
            not_null<std::unique_ptr<const Variable>>
                index_; ///< Vaule of this AddressRange may rely on this ghost variable.

            Range(not_null<std::unique_ptr<const SymbolicExpr>> len)
                : len_(std::move(len)),
                  index_(make_unique<Variable>(SymbolicExpr::Type{ScalarKind::UInt, 32},
                                               std::monostate{})) {}

            Range(const Range &other)
                : len_(other.len_->clone().into_underlying()),
                  index_(std::make_unique<Variable>(*other.index_)) {}
            Range &operator=(const Range &other) {
                len_   = other.len_->clone().into_underlying();
                index_ = std::make_unique<Variable>(*other.index_);
                return *this;
            }
            Range(Range &&other)       = default;
            Range &operator=(Range &&) = default;
        };

      public:
        inline static constexpr signed long ZERO_OFFSET =
            0; ///< Unify the type of zero under zero offset. This type should be the same as the
               ///< type of the zero value in SymbolicExpr::simplifiedExprIfLinear, or relax the
               ///< type comparison in LiteralExpr's equal method.

        struct BaseInfo {
            std::variant<std::monostate, not_null<std::unique_ptr<const Address>>> from_;
            BaseInfo(std::variant<std::monostate, not_null<std::unique_ptr<const Address>>> from)
                : from_(std::move(from)) {}
            BaseInfo(const BaseInfo &);
            BaseInfo &operator=(const BaseInfo &);
            BaseInfo(BaseInfo &&)            = default;
            BaseInfo &operator=(BaseInfo &&) = default;
            size_t hash() const;
            bool operator==(const BaseInfo &other) const {
                return std::visit(
                    [&](auto &&arg) -> bool {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, std::monostate>) {
                            TODO();
                        } else if constexpr (std::is_same_v<
                                                 T, not_null<std::unique_ptr<const Address>>>) {
                            if (auto addrPtr =
                                    std::get_if<not_null<std::unique_ptr<const Address>>>(
                                        &other.from_);
                                addrPtr != nullptr && *arg == **addrPtr) {
                                return true;
                            }
                            return false;
                        }
                    },
                    from_);
            }
        };

        SymbolAddress(const SymbolAddress &other);
        SymbolAddress &operator=(const SymbolAddress &other);
        SymbolAddress(SymbolAddress &&) = default;
        SymbolAddress &operator=(SymbolAddress &&);

        bool operator==(const SymbolAddress &other) const { return equal(other); }

        SymbolAddress(
            std::variant<std::monostate, not_null<std::unique_ptr<const Address>>> from,
            std::optional<not_null<std::unique_ptr<const SymbolicExpr>>> offset = std::nullopt,
            std::optional<not_null<std::unique_ptr<const SymbolicExpr>>> length = std::nullopt);

        not_null<std::unique_ptr<SymbolicExpr>> clone() const override;
        std::string dump() const override;
        virtual std::optional<std::string> regularForm(
            std::optional<std::string_view> prefix = std::nullopt,
            std::optional<std::string_view> suffix = std::nullopt,
            int parentPrec                         = 0,
            bool isRightChild                      = false) const override;
        virtual not_null<std::unique_ptr<SymbolicExpr>> simplifiedExpr() const override;
        virtual bool equal(const SymbolicExpr &expr) const override;
        virtual std::optional<not_null<std::unique_ptr<SymbolAddress>>> tryEvalAsOffsetedAddr()
            const override;
        virtual std::size_t hash() const override;

        /// @brief Get the value's regular form on this address.
        std::optional<std::string> regularFormOfValue(
            std::optional<std::string_view> prefix = std::nullopt,
            std::optional<std::string_view> suffix = std::nullopt,
            int parentPrec                         = 0,
            bool isRightChild                      = false) const override;
        std::variant<std::monostate, not_null<std::unique_ptr<const Address>>> getFrom()
            const override {
            return std::visit(
                [&](auto &&arg)
                    -> std::variant<std::monostate, not_null<std::unique_ptr<const Address>>> {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, std::monostate>) {
                        return std::monostate();
                    } else if constexpr (std::is_same_v<T,
                                                        not_null<std::unique_ptr<const Address>>>) {
                        return arg->addressClone().into_underlying();
                    }
                },
                from_);
        }
        std::optional<not_null<const clang::VarDecl *>> getFromRoot() const override;
        int getDimension() const override;
        virtual not_null<std::unique_ptr<Address>> addressClone() const override;

        not_null<const SymbolicExpr *> getOffset() const { return offset_.get().get(); }
        void setOffset(not_null<std::unique_ptr<SymbolicExpr>> offset);
        void addOffset(not_null<std::unique_ptr<SymbolicExpr>> extra);
        void subOffset(not_null<std::unique_ptr<SymbolicExpr>> extra);
        void resetOffset() { offset_ = std::make_unique<LiteralExpr>(ZERO_OFFSET); }

        void setLength(not_null<std::unique_ptr<SymbolicExpr>> len);
        auto getLength() const -> const auto & {
            if (range_ == std::nullopt)
                ERROR("Is not a range! Do isRange first.");
            return range_.value().len_;
        }
        auto getIndex() const -> const auto & {
            if (range_ == std::nullopt)
                ERROR("Is not a range! Do isRange first.");
            return range_.value().index_;
        }
        bool isRange() const { return range_ != std::nullopt; }
        void resetRange() { range_ = std::nullopt; }
        BaseInfo getBaseInfo() const;

        // StInG: Support functions for affine invariant analysis
        UsedMap collectUsedVarsAndAddrs() const override;
        bool isLinear() const override {
            if (isRange())
                ERROR("Address range is solely for address representation and should not be "
                      "used as an expression.");
            return true;
        }
        int getMaxDegree() const override {
            if (isRange())
                ERROR("Address range is solely for address representation and should not be "
                      "used as an expression.");
            return 1;
        }
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const std::unordered_map<size_t, size_t> &) const override;

      private:
        not_null<std::unique_ptr<const SymbolicExpr>> offset_; ///< Offset relative to an address.
        std::variant<std::monostate,
                     not_null<std::unique_ptr<const Address>>>
            from_; ///< From another Address p means this is a value(may with offset) of a
                   ///< pointer variable whose address is p, from {Structure::Info, size_t} means
                   ///< this is a field(a pointer)'s value.
        std::optional<Range> range_;
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

        VariableAddress(std::variant<std::monostate, not_null<const clang::VarDecl *>> from)
            : Address(AddressType::VariableAddr), from_(std::move(from)) {};

        not_null<std::unique_ptr<SymbolicExpr>> clone() const override;
        std::string dump() const override;
        virtual std::optional<std::string> regularForm(
            std::optional<std::string_view> prefix = std::nullopt,
            std::optional<std::string_view> suffix = std::nullopt,
            int parentPrec                         = 0,
            bool isRightChild                      = false) const override;
        virtual not_null<std::unique_ptr<SymbolicExpr>> simplifiedExpr() const override {
            ERROR("VariableAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        };
        virtual bool equal(const SymbolicExpr &expr) const override;
        virtual std::optional<not_null<std::unique_ptr<SymbolAddress>>> tryEvalAsOffsetedAddr()
            const override {
            ERROR("VariableAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        };
        virtual std::size_t hash() const override;

        /// @brief Get the value's regular form on this address.
        std::optional<std::string> regularFormOfValue(
            std::optional<std::string_view> prefix = std::nullopt,
            std::optional<std::string_view> suffix = std::nullopt,
            int parentPrec                         = 0,
            bool isRightChild                      = false) const override;
        auto getFrom() const -> const auto & { return from_; }
        std::optional<not_null<const clang::VarDecl *>> getFromRoot() const override;
        int getDimension() const override;
        virtual not_null<std::unique_ptr<Address>> addressClone() const override;

        // StInG: Support functions for affine invariant analysis
        UsedMap collectUsedVarsAndAddrs() const override {
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
        std::variant<std::monostate, not_null<const clang::VarDecl *>> from_;
    };

    /// @class FieldAddress
    /// @brief Represents the address of a C Structure's member.
    class FieldAddress : public Address {
      public:
        FieldAddress(const FieldAddress &other);
        FieldAddress &operator=(const FieldAddress &other);
        FieldAddress(FieldAddress &&) = default;

        FieldAddress(
            const clang::RecordDecl *RD,
            std::variant<std::monostate,
                         std::pair<not_null<std::unique_ptr<const Address>>, const size_t>> from)
            : Address(AddressType::FieldAddr), definition_(RD), from_(std::move(from)) {
            if (!RD->isCompleteDefinition())
                ERROR("Incomplete struct definition");
            definition_ = RD->getDefinition();
        };

        not_null<std::unique_ptr<SymbolicExpr>> clone() const override;
        std::string dump() const override;
        virtual std::optional<std::string> regularForm(
            std::optional<std::string_view> prefix = std::nullopt,
            std::optional<std::string_view> suffix = std::nullopt,
            int parentPrec                         = 0,
            bool isRightChild                      = false) const override;
        virtual not_null<std::unique_ptr<SymbolicExpr>> simplifiedExpr() const override {
            ERROR("FieldAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        };
        virtual bool equal(const SymbolicExpr &expr) const override;
        virtual std::optional<not_null<std::unique_ptr<SymbolAddress>>> tryEvalAsOffsetedAddr()
            const override {
            ERROR("FieldAddress should not appear in expressions, and therefore, this function "
                  "should not be called.");
        };
        virtual std::size_t hash() const override;

        /// @brief Get the value's regular form on this address.
        std::optional<std::string> regularFormOfValue(
            std::optional<std::string_view> prefix = std::nullopt,
            std::optional<std::string_view> suffix = std::nullopt,
            int parentPrec                         = 0,
            bool isRightChild                      = false) const override;
        auto getFrom() const -> const auto & { return from_; }
        std::optional<not_null<const clang::VarDecl *>> getFromRoot() const override;
        int getDimension() const override;
        virtual not_null<std::unique_ptr<Address>> addressClone() const override;

        // StInG: Support functions for affine invariant analysis
        UsedMap collectUsedVarsAndAddrs() const override {
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
        not_null<const clang::RecordDecl *> definition_;
        std::variant<std::monostate,
                     std::pair<not_null<std::unique_ptr<const Address>>, const size_t>>
            from_;
    };

    struct AddressHash {
        std::size_t operator()(const Address &addr) const noexcept { return addr.hash(); }
    };

    /// @class Symbol value
    /// @brief Symbolic value with unique ID and optional origin.
    /// Origin can't be nullptr, use nullopt.
    class Variable : public SymbolicExpr, public Symbol {
      public:
        Variable(Type varType,
                 std::variant<std::monostate, not_null<std::unique_ptr<const Address>>> from)
            : SymbolicExpr(ExprType::Variable, varType), varType_(varType), from_(std::move(from)) {
        }

        Variable(const Variable &other);

        Type getVarType() const { return varType_; }
        void setVarType(Type vt) {
            varType_ = vt;
            setValType(vt);
        }

        not_null<std::unique_ptr<SymbolicExpr>> clone() const override;
        std::string dump() const override;
        virtual std::optional<std::string> regularForm(
            std::optional<std::string_view> prefix = std::nullopt,
            std::optional<std::string_view> suffix = std::nullopt,
            int parentPrec                         = 0,
            bool isRightChild                      = false) const override;
        virtual not_null<std::unique_ptr<SymbolicExpr>> simplifiedExpr() const override;
        virtual std::size_t hash() const override;
        virtual bool equal(const SymbolicExpr &expr) const override;
        std::variant<std::monostate, not_null<std::unique_ptr<const Address>>> getFrom()
            const override {
            return std::visit(
                [&](auto &&arg)
                    -> std::variant<std::monostate, not_null<std::unique_ptr<const Address>>> {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, std::monostate>) {
                        return std::monostate();
                    } else if constexpr (std::is_same_v<T,
                                                        not_null<std::unique_ptr<const Address>>>) {
                        return arg->addressClone().into_underlying();
                    }
                },
                from_);
        }
        std::optional<not_null<const clang::VarDecl *>> getFromRoot() const;

        // StInG: Support functions for affine invariant analysis
        UsedMap collectUsedVarsAndAddrs() const override;
        bool isLinear() const override { return true; }
        int getMaxDegree() const override { return 1; }
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const std::unordered_map<size_t, size_t> &) const override;

      private:
        Type varType_; ///< Symbol value's type.
        std::variant<std::monostate,
                     not_null<std::unique_ptr<const Address>>>
            from_; ///< The original Address of the value or the Structure it belongs.
    };

    std::unique_ptr<SymbolicExpr> createLNotExpr(not_null<std::unique_ptr<SymbolicExpr>> expr);
    BinaryOpExpr::Operator getCompoundAssignOp(clang::BinaryOperatorKind compoundAssignOp);
    BinaryOpExpr::Operator getBinaryOp(clang::BinaryOperatorKind op);
    SymbolicExpr::Type deriveVarType(clang::QualType type);
    bool isValidOffsetOrLength(const SymbolicExpr &expr);
    bool isFrom(const Symbolic::Address &addr, const SymbolicExpr &expr);

} // namespace Symbolic

namespace std {
    template <> struct hash<Symbolic::VariableAddress> {
        size_t operator()(const Symbolic::VariableAddress &va) const noexcept { return va.hash(); }
    };
    template <> struct hash<Symbolic::SymbolAddress> {
        size_t operator()(const Symbolic::SymbolAddress &sa) const noexcept { return sa.hash(); }
    };
    template <> struct hash<Symbolic::SymbolAddress::BaseInfo> {
        size_t operator()(const Symbolic::SymbolAddress::BaseInfo &bi) const noexcept {
            return bi.hash();
        }
    };
} // namespace std

#endif // SYMBOLIC_H