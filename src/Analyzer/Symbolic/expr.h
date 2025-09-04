/// \file symbolic.h
/// @brief Declarations for symbolic expression hierarchy and utilities.
#ifndef SYMBOLIC_H
#define SYMBOLIC_H

#include <string>
#include <memory>
#include <span>
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
    class Variable;

    /// @class SymbolicExpr
    /// @brief Base class for all symbolic expressions.
    class SymbolicExpr {
      public:
        /// @class SymbolicExpr
        /// @brief Base class for all symbolic expressions.
        enum class ExprType {
            Literal,
            Variable,
            SymbolAddress,
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
        virtual std::unique_ptr<SymbolicExpr> clone() const = 0;

        /// @brief Dump debug string of the expression.
        /// @return Human-readable representation.
        virtual std::string dump() const = 0;

        /// @brief Emit expression in ACSL-compliant regular form.
        /// @param prefix Optional variable prefix.
        /// @param suffix Optional variable suffix.
        /// @param parentPrec Precedence of parent operator.
        /// @param isRightChild Whether this is right operand.
        /// @return String in ACSL syntax.
        virtual std::string regularForm(std::optional<std::string_view> prefix = std::nullopt,
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
        virtual std::unique_ptr<SymbolicExpr> simplifiedExpr() const = 0;

        /// @brief Collect Variables and Addresses used in the expression.
        /// @return Map from ID to Variable and Address pointer.
        virtual std::unordered_map<unsigned int, std::variant<const Variable *, const Address *>> collectUsedVarsAndAddrs()
            const {
            return {};
        };

        /// @brief Try to evaluate the expression to an address.
        /// @return Returning `std::nullopt` indicates that the expression is not a valid address.
        virtual std::optional<not_null<std::unique_ptr<Address>>> tryEvalAsOffsetedAddr() const {
            // TODO: cache the result.
            return std::nullopt;
        };

        std::optional<int64_t> tryEvalAsConstant() const {
            // TODO: cache the result.
            if (!isLinear())
                return std::nullopt;
            auto linearExpr = toLinearExpr();
            if (linearExpr.all_homogeneous_terms_are_zero())
                return linearExpr.inhomogeneous_term().get_si();
            return std::nullopt;
        };

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
        /// @return PPL linear expression.
        virtual Parma_Polyhedra_Library::Linear_Expression toLinearExpr() const {
            ERROR("not implemented for expression type: ");
        }

      protected:
        /// @brief Simplify expression if it's linear, just call clone() otherwise.
        std::unique_ptr<SymbolicExpr> simplifiedExprIfLinear() const;

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

        std::unique_ptr<SymbolicExpr> clone() const override;
        std::string dump() const override;
        virtual std::string regularForm(std::optional<std::string_view> prefix = std::nullopt,
                                        std::optional<std::string_view> suffix = std::nullopt,
                                        int parentPrec                         = 0,
                                        bool isRightChild = false) const override;
        virtual std::unique_ptr<SymbolicExpr> simplifiedExpr() const override;
        virtual std::size_t hash() const override;
        virtual bool equal(const SymbolicExpr &expr) const override;

        // StInG: Support functions for affine invariant analysis
        bool isLinear() const override { return true; }
        int getMaxDegree() const override { return 0; }
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr() const override;
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

        std::unique_ptr<SymbolicExpr> clone() const override;
        std::string dump() const override;
        virtual std::string regularForm(std::optional<std::string_view> prefix = std::nullopt,
                                        std::optional<std::string_view> suffix = std::nullopt,
                                        int parentPrec                         = 0,
                                        bool isRightChild = false) const override;
        virtual std::unique_ptr<SymbolicExpr> simplifiedExpr() const override;
        virtual std::size_t hash() const override;
        virtual bool equal(const SymbolicExpr &expr) const override;
        virtual std::optional<not_null<std::unique_ptr<Address>>> tryEvalAsOffsetedAddr()
            const override;
        virtual bool isUnknown() const override {
            return left_->isUnknown() || right_->isUnknown();
        };

        // StInG: Support functions for affine invariant analysis
        std::unordered_map<unsigned int, std::variant<const Variable *, const Address *>> collectUsedVarsAndAddrs()
            const override;
        bool isLinear() const override;
        int getMaxDegree() const override;
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr() const override;

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

        std::unique_ptr<SymbolicExpr> clone() const override;
        std::string dump() const override;
        virtual std::string regularForm(std::optional<std::string_view> prefix = std::nullopt,
                                        std::optional<std::string_view> suffix = std::nullopt,
                                        int parentPrec                         = 0,
                                        bool isRightChild = false) const override;
        virtual std::unique_ptr<SymbolicExpr> simplifiedExpr() const override;
        virtual std::size_t hash() const override;
        virtual bool equal(const SymbolicExpr &expr) const override;
        virtual bool isUnknown() const override { return expr_->isUnknown(); };

        // StInG: Support functions for affine invariant analysis
        std::unordered_map<unsigned int, std::variant<const Variable *, const Address *>> collectUsedVarsAndAddrs()
            const override;
        bool isLinear() const override;
        int getMaxDegree() const override;
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr() const override;

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
        static std::unique_ptr<UnknownExpr> makeUnknown();

        std::unique_ptr<SymbolicExpr> clone() const override;
        std::string dump() const override;
        virtual std::string regularForm(std::optional<std::string_view> prefix = std::nullopt,
                                        std::optional<std::string_view> suffix = std::nullopt,
                                        int parentPrec                         = 0,
                                        bool isRightChild = false) const override;
        virtual std::unique_ptr<SymbolicExpr> simplifiedExpr() const override;
        virtual std::size_t hash() const override;
        virtual bool equal(const SymbolicExpr &expr) const override;
        virtual bool isUnknown() const override { return true; };

        // StInG: Support functions for affine invariant analysis
        bool isLinear() const override { return false; }
        int getMaxDegree() const override { return 0; }
    };

    class Structure : public SymbolicExpr {
      public:
        struct Info {
            not_null<const clang::RecordDecl *> definition_;
            const clang::ASTRecordLayout &layout_;
            std::variant<std::monostate,
                         not_null<std::unique_ptr<const Address>>,
                         std::pair<not_null<std::shared_ptr<const Structure::Info>>, const size_t>>
                from_;
            Info(const clang::RecordDecl *RD,
                 const clang::ASTRecordLayout &layout,
                 std::variant<std::monostate,
                              std::unique_ptr<const Address>,
                              std::pair<std::shared_ptr<const Structure::Info>, const size_t>> from)
                : definition_(RD) /*not_null has no default constructor*/, layout_(layout) {
                if (!RD->isCompleteDefinition())
                    ERROR("Incomplete struct definition");
                definition_ = RD->getDefinition();

                std::visit(
                    [this](auto &&arg) {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, std::monostate>) {
                            from_ = std::monostate{};
                        } else if constexpr (std::is_same_v<T, std::unique_ptr<const Address>>) {
                            from_.emplace<1>(std::move(arg));
                        } else if constexpr (std::is_same_v<
                                                 T, std::pair<std::shared_ptr<const Structure::Info>,
                                                              const size_t>>) {
                            from_.emplace<2>(std::move(arg));
                        }
                    },
                    from);
            }
            Info(const Info &other);
            Info(Info &&) = default;

            std::string regularForm(std::optional<std::string_view> prefix = std::nullopt,
                                    std::optional<std::string_view> suffix = std::nullopt,
                                    int parentPrec                         = 0,
                                    bool isRightChild                      = false) const;
            std::string regularFormOfField(size_t index,
                                           std::optional<std::string_view> prefix = std::nullopt,
                                           std::optional<std::string_view> suffix = std::nullopt,
                                           int parentPrec                         = 0,
                                           bool isRightChild                      = false) const;
            bool equal(const Structure::Info &other) const;
            std::size_t hash() const;
            std::string dump() const;
            size_t getNumFields() const { return layout_.getFieldCount(); }
            auto getFrom() const -> const auto & { return from_; }
            const clang::VarDecl *getFromRoot() const;
        };

        Structure(unsigned int id,
                  const clang::RecordDecl *RD,
                  const clang::ASTRecordLayout &layout,
                  std::variant<std::monostate,
                               std::unique_ptr<const Address>,
                               std::pair<std::shared_ptr<const Structure::Info>, const size_t>> from)
            : SymbolicExpr(ExprType::Structure,
                           Type{ScalarKind::Structure,
                                static_cast<unsigned>(layout.getSize().getQuantity()) *
                                    8 /*By default, char is 8-bit.*/}),
              id_(id), info_(make_shared<Info>(RD, layout, std::move(from))) {
            fields_.resize(info_->layout_.getFieldCount());
        }

        Structure(const Structure &other)
            : SymbolicExpr(other), id_(other.id_), info_(other.info_) {
            fields_.resize(other.fields_.size());
            std::ranges::transform(
                other.fields_, fields_.begin(),
                [](auto &field) -> std::optional<not_null<std::unique_ptr<SymbolicExpr>>> {
                    if (field == std::nullopt)
                        return std::nullopt;
                    return field.value()->clone();
                });
        }

        bool isComplete() const;
        size_t getNumFields() const { return info_->getNumFields(); }
        void setFieldValue(size_t index, const SymbolicExpr &expr);
        std::unique_ptr<SymbolicExpr> getFieldValue(size_t index) const;
        auto fieldsValues() { return std::span{fields_}; }
        auto fieldsValues() const { return std::span{fields_}; }
        auto getInfo() -> const auto & { return info_; }
        std::string regularFormOfField(size_t index,
                                       std::optional<std::string_view> prefix = std::nullopt,
                                       std::optional<std::string_view> suffix = std::nullopt,
                                       int parentPrec                         = 0,
                                       bool isRightChild                      = false) const;

        std::unique_ptr<SymbolicExpr> clone() const override;
        std::string dump() const override;
        std::string regularForm(std::optional<std::string_view> prefix = std::nullopt,
                                std::optional<std::string_view> suffix = std::nullopt,
                                int parentPrec                         = 0,
                                bool isRightChild                      = false) const override;
        std::unique_ptr<SymbolicExpr> simplifiedExpr() const override;
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
        unsigned int id_;
        not_null<std::shared_ptr<Info>> info_;
        std::vector<std::optional<not_null<std::unique_ptr<SymbolicExpr>>>> fields_;
    };

    /// @class Address
    /// @brief Symbolic address with unique ID, optional offset and origin.
    /// Origin can't be nullptr, use monostate or nullopt.
    class Address : public SymbolicExpr {
      private:
        struct Range {
            not_null<std::unique_ptr<const SymbolicExpr>>
                len_; ///< The length of an address. The Address type does not store pointer types
                      ///< currently, thus it does not support C-style pointer conversion.
            not_null<std::unique_ptr<const Variable>>
                index_; ///< Vaule of this AddressRange may rely on this ghost variable.

            Range(unsigned long id, not_null<std::unique_ptr<const SymbolicExpr>> len)
                : len_(std::move(len)),
                  index_(make_unique<Variable>("index of AddressRange{" + std::to_string(id) + "}",
                                               SymbolicExpr::Type{ScalarKind::UInt, 32},
                                               id,
                                               std::monostate{})) {}

            Range(const Range &other)
                : len_(other.len_->clone()), index_(std::make_unique<Variable>(*other.index_)) {}
            Range &operator=(const Range &other) {
                len_   = other.len_->clone();
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

        Address(const Address &other);
        Address &operator=(const Address &other);
        Address(Address &&) = default;
        Address &operator=(Address &&);

        Address(unsigned int id,
                std::variant<std::monostate,
                             const clang::VarDecl *,
                             std::unique_ptr<const Address>,
                             std::pair<std::shared_ptr<const Structure::Info>, const size_t>> from,
                std::unique_ptr<const SymbolicExpr> offset = nullptr,
                std::unique_ptr<const SymbolicExpr> length = nullptr);

        std::unique_ptr<SymbolicExpr> clone() const override;
        std::string dump() const override;
        virtual std::string regularForm(std::optional<std::string_view> prefix = std::nullopt,
                                        std::optional<std::string_view> suffix = std::nullopt,
                                        int parentPrec                         = 0,
                                        bool isRightChild = false) const override;
        virtual std::unique_ptr<SymbolicExpr> simplifiedExpr() const override;
        virtual bool equal(const SymbolicExpr &expr) const override;
        virtual std::optional<not_null<std::unique_ptr<Address>>> tryEvalAsOffsetedAddr()
            const override;
        virtual std::size_t hash() const override;

        unsigned int getId() const { return id_; }
        /// @brief Get the value's regular form on this address.
        std::string regularFormOfValue(std::optional<std::string_view> prefix = std::nullopt,
                                       std::optional<std::string_view> suffix = std::nullopt,
                                       int parentPrec                         = 0,
                                       bool isRightChild                      = false) const;
        not_null<const SymbolicExpr *> getOffset() const {
            if (offset_ == std::nullopt)
                ERROR("There is no offset, call isOffseted first.");
            return offset_.value().get().get();
        }
        auto getFrom() const -> const auto & { return from_; }
        const clang::VarDecl *getFromRoot() const;
        int getDimension() const;
        std::string getBaseName() const;
        not_null<std::unique_ptr<Address>> getBaseAddr() const;

        void setOffset(not_null<std::unique_ptr<SymbolicExpr>> offset);
        void addOffset(not_null<std::unique_ptr<SymbolicExpr>> extra);
        void subOffset(not_null<std::unique_ptr<SymbolicExpr>> extra);
        void resetOffset() { offset_ = std::nullopt; }
        bool isOffseted() const { return offset_ != std::nullopt; }

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

        // StInG: Support functions for affine invariant analysis
        std::unordered_map<unsigned int, std::variant<const Variable *, const Address *>> collectUsedVarsAndAddrs()
            const override;
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
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr() const override;

      protected:
        [[deprecated("use `getFromRoot`")]]
        const clang::VarDecl *retrieveVarDecl() const;

      private:
        unsigned int id_; ///< This ID is unique within its path, but not globally unique across
                          ///< different paths.
        std::optional<not_null<std::unique_ptr<const SymbolicExpr>>>
            offset_; ///< Offset relative to an address.
        std::variant<std::monostate,
                     not_null<const clang::VarDecl *>,
                     not_null<std::unique_ptr<const Address>>,
                     std::pair<not_null<std::shared_ptr<const Structure::Info>>, const size_t>>
            from_; ///< from a VarDecl* means this is a variable's address, from another Address p
                   ///< means this is a value(may with offset) of a pointer variable whose address
                   ///< is p, from {Structure::Info, size_t} means this is a field.
        std::optional<Range> range_;
    };

    struct AddressHash {
        std::size_t operator()(const Address &addr) const noexcept { return addr.hash(); }
    };

    /// @class Symbol value
    /// @brief Symbolic value with unique ID and optional origin.
    /// Origin can't be nullptr, use nullopt.
    class Variable : public SymbolicExpr {
      public:
        Variable(const std::string &name,
                 Type varType,
                 int id,
                 std::variant<std::monostate,
                              std::unique_ptr<const Address>,
                              std::pair<std::shared_ptr<const Structure::Info>, const size_t>> from)
            : SymbolicExpr(ExprType::Variable, varType), name_(name), varType_(varType), id_(id) {
            std::visit(
                [this](auto &&arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, std::monostate>) {
                        from_ = std::monostate{};
                    } else if constexpr (std::is_same_v<T, std::unique_ptr<const Address>>) {
                        from_.emplace<1>(std::move(arg));
                    } else if constexpr (std::is_same_v<
                                             T, std::pair<std::shared_ptr<const Structure::Info>,
                                                          const size_t>>) {
                        from_.emplace<2>(std::move(arg));
                    }
                },
                from);
        }

        Variable(const Variable &other);

        Type getVarType() const { return varType_; }
        void setVarType(Type vt) {
            varType_ = vt;
            setValType(vt);
        }

        const std::string &getName() const { return name_; }
        int getId() const { return id_; }

        std::unique_ptr<SymbolicExpr> clone() const override;
        std::string dump() const override;
        virtual std::string regularForm(std::optional<std::string_view> prefix = std::nullopt,
                                        std::optional<std::string_view> suffix = std::nullopt,
                                        int parentPrec                         = 0,
                                        bool isRightChild = false) const override;
        virtual std::unique_ptr<SymbolicExpr> simplifiedExpr() const override;
        virtual std::size_t hash() const override;
        virtual bool equal(const SymbolicExpr &expr) const override;
        auto getFrom() const -> const auto & { return from_; }
        const clang::VarDecl *getFromRoot() const;

        // StInG: Support functions for affine invariant analysis
        std::unordered_map<unsigned int, std::variant<const Variable *, const Address *>> collectUsedVarsAndAddrs()
            const override;
        bool isLinear() const override { return true; }
        int getMaxDegree() const override { return 1; }
        std::optional<Parma_Polyhedra_Library::Linear_Expression> toLinearExpr(
            const std::unordered_map<std::string, size_t> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr() const override;

      private:
        std::string name_; ///< May be incorrect in pointer-related contexts now, use
                           ///< regularForm or from_ as an alternative.
        Type varType_;     ///< Symbol value's type.
        int id_; ///< unique identifier to distinguish between variables with the same name
        std::variant<std::monostate,
                     not_null<std::unique_ptr<const Address>>,
                     std::pair<not_null<std::shared_ptr<const Structure::Info>>, const size_t>>
            from_; ///< The original Address of the value or the Structure it belongs.
    };

    std::unique_ptr<SymbolicExpr> createLNotExpr(not_null<std::unique_ptr<SymbolicExpr>> expr);
    BinaryOpExpr::Operator getCompoundAssignOp(clang::BinaryOperatorKind compoundAssignOp);
    BinaryOpExpr::Operator getBinaryOp(clang::BinaryOperatorKind op);
    SymbolicExpr::Type deriveVarType(clang::QualType type);
    bool isValidOffsetOrLength(const SymbolicExpr &expr);
    bool isFrom(const Symbolic::Address &addr,
                std::variant<not_null<const Symbolic::Variable *>,
                             not_null<const Symbolic::Address *>,
                             not_null<const Symbolic::Structure::Info *>> symbol);

} // namespace Symbolic

#endif // SYMBOLIC_H