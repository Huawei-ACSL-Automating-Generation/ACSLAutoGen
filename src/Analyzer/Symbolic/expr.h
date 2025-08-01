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
            SNULL,
            Structure
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
        virtual ~SymbolicExpr() = default;

        ExprType getType() const { return type_; }
        Type getValType() const { return valueType_; }
        void setValType(Type newType) { valueType_ = newType; }

        /// @brief Create a null symbolic expression.
        /// @return Unique pointer to a SNULL expression.
        static std::unique_ptr<SymbolicExpr> makeNull();

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
        virtual std::size_t hash() const               = 0;

        friend std::ostream &operator<<(std::ostream &os, const SymbolicExpr &expr) {
            return os << expr.dump();
        }

        friend bool operator==(const SymbolicExpr &LHS, const SymbolicExpr &RHS) {
            if (LHS.type_ != RHS.type_)
                return false;
            return LHS.equal(RHS);
        }

        friend bool operator!=(const SymbolicExpr &LHS, const SymbolicExpr &RHS) {
            return !(LHS == RHS);
        }

        /// @brief Get a simplified version of the expression.
        /// @return Simplified expression.
        virtual std::unique_ptr<SymbolicExpr> simplifiedExpr() const = 0;

        /// @brief Collect variables used in the expression.
        /// @return Map from variable ID to Variable pointer.
        virtual std::unordered_map<unsigned int, const Variable *> collectUsedVars() const {
            return std::unordered_map<unsigned int, const Variable *>{};
        };

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
        /// @param varMap Mapping from names to the index of the Cartesian axis.
        /// @return PPL linear expression.
        virtual Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const std::unordered_map<std::string, int> &) const {
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
            : SymbolicExpr(ExprType::Literal, {ScalarKind::Bool, 1}), type(LiteralType::Boolean) {
            data.boolValue = value;
        }

        LiteralExpr(int value)
            : SymbolicExpr(ExprType::Literal, {ScalarKind::Int, 32}), type(LiteralType::Int) {
            data.intValue = value;
        }

        LiteralExpr(unsigned int value)
            : SymbolicExpr(ExprType::Literal, {ScalarKind::UInt, 32}),
              type(LiteralType::UnsignedInt) {
            data.uintValue = value;
        }

        LiteralExpr(short value)
            : SymbolicExpr(ExprType::Literal, {ScalarKind::Int, 16}), type(LiteralType::Short) {
            data.shortValue = value;
        }

        LiteralExpr(unsigned short value)
            : SymbolicExpr(ExprType::Literal, {ScalarKind::UInt, 16}),
              type(LiteralType::UnsignedShort) {
            data.ushortValue = value;
        }

        LiteralExpr(int64_t value)
            : SymbolicExpr(ExprType::Literal, {ScalarKind::Int, 64}), type(LiteralType::Int64) {
            data.int64Value = value;
        }

        LiteralExpr(uint64_t value)
            : SymbolicExpr(ExprType::Literal, {ScalarKind::UInt, 64}), type(LiteralType::UInt64) {
            data.uint64Value = value;
        }

        LiteralType getLiteralType() const { return type; }

        std::unique_ptr<SymbolicExpr> clone() const override;
        std::string dump() const override;
        virtual std::string regularForm(std::optional<std::string_view> prefix = std::nullopt,
                                        std::optional<std::string_view> suffix = std::nullopt,
                                        int parentPrec                         = 0,
                                        bool isRightChild = false) const override;
        virtual std::unique_ptr<SymbolicExpr> simplifiedExpr() const override;
        std::size_t hash() const override;
        virtual bool equal(const SymbolicExpr &expr) const override;

        // StInG: Support functions for affine invariant analysis
        bool isLinear() const override { return true; }
        int getMaxDegree() const override { return 0; }
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const std::unordered_map<std::string, int> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr() const override;
        int64_t getLiteralValue() const;

      private:
        LiteralType type;
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
        } data;
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
        BinaryOpExpr(std::unique_ptr<SymbolicExpr> left,
                     Operator op,
                     std::unique_ptr<SymbolicExpr> right)
            : SymbolicExpr(ExprType::BinaryOp, left->getValType()), left_(std::move(left)), op_(op),
              right_(std::move(right)) {}

        BinaryOpExpr(SymbolicExpr *left, Operator op, SymbolicExpr *right)
            : SymbolicExpr(ExprType::BinaryOp, left->getValType()), left_(left), op_(op),
              right_(right) {}

        const std::unique_ptr<SymbolicExpr> &getLeft() const { return left_; }
        const std::unique_ptr<SymbolicExpr> &getRight() const { return right_; }
        Operator getOperator() const { return op_; }

        std::unique_ptr<SymbolicExpr> clone() const override;
        std::string dump() const override;
        virtual std::string regularForm(std::optional<std::string_view> prefix = std::nullopt,
                                        std::optional<std::string_view> suffix = std::nullopt,
                                        int parentPrec                         = 0,
                                        bool isRightChild = false) const override;
        virtual std::unique_ptr<SymbolicExpr> simplifiedExpr() const override;
        std::size_t hash() const override;
        virtual bool equal(const SymbolicExpr &expr) const override;

        std::unordered_map<unsigned int, const Variable *> collectUsedVars() const override;
        // StInG: Support functions for affine invariant analysis
        bool isLinear() const override;
        int getMaxDegree() const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const std::unordered_map<std::string, int> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr() const override;

      private:
        std::unique_ptr<SymbolicExpr> left_;
        Operator op_;
        std::unique_ptr<SymbolicExpr> right_;
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

        UnaryOpExpr(Operator op, std::unique_ptr<SymbolicExpr> expr)
            : SymbolicExpr(ExprType::UnaryOp, expr->getValType()), op_(op), expr_(std::move(expr)) {
        }

        std::unique_ptr<SymbolicExpr> clone() const override;
        std::string dump() const override;
        virtual std::string regularForm(std::optional<std::string_view> prefix = std::nullopt,
                                        std::optional<std::string_view> suffix = std::nullopt,
                                        int parentPrec                         = 0,
                                        bool isRightChild = false) const override;
        virtual std::unique_ptr<SymbolicExpr> simplifiedExpr() const override;
        std::size_t hash() const override;
        virtual bool equal(const SymbolicExpr &expr) const override;

        std::unordered_map<unsigned int, const Variable *> collectUsedVars() const override;
        // StInG: Support functions for affine invariant analysis
        bool isLinear() const override;
        int getMaxDegree() const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const std::unordered_map<std::string, int> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr() const override;

      private:
        Operator op_;
        std::unique_ptr<SymbolicExpr> expr_;
    };

    /// @class NullExpr
    /// @brief Represents a null symbolic expression.
    class NullExpr : public SymbolicExpr {
      public:
        NullExpr() : SymbolicExpr(ExprType::SNULL, {ScalarKind::UInt, 64}) {}
        ~NullExpr() = default;

        std::unique_ptr<SymbolicExpr> clone() const override;
        std::string dump() const override;
        virtual std::string regularForm(std::optional<std::string_view> prefix = std::nullopt,
                                        std::optional<std::string_view> suffix = std::nullopt,
                                        int parentPrec                         = 0,
                                        bool isRightChild = false) const override;
        virtual std::unique_ptr<SymbolicExpr> simplifiedExpr() const override;
        std::size_t hash() const override;
        virtual bool equal(const SymbolicExpr &expr) const override;

        // StInG: Support functions for affine invariant analysis
        bool isLinear() const override { return false; }
        int getMaxDegree() const override { return 0; }
    };

    class Address;

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
        };

        Structure(const clang::RecordDecl *RD,
                  const clang::ASTRecordLayout &layout,
                  std::variant<std::monostate,
                               std::unique_ptr<const Address>,
                               std::pair<std::shared_ptr<const Structure::Info>, const size_t>> from)
            : SymbolicExpr(ExprType::Structure,
                           Type{ScalarKind::Structure,
                                static_cast<unsigned>(layout.getSize().getQuantity()) *
                                    8 /*By default, char is 8-bit.*/}),
              info_(make_shared<Info>(RD, layout, std::move(from))) {
            fields_.resize(info_->layout_.getFieldCount());
        }

        Structure(const Structure &other) : SymbolicExpr(other), info_(other.info_) {
            fields_.resize(other.fields_.size());
            std::ranges::transform(other.fields_, fields_.begin(), [](auto &field) {
                return field == nullptr ? nullptr : field->clone();
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
        std::size_t hash() const override;
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
        not_null<std::shared_ptr<Info>> info_;
        std::vector<std::unique_ptr<SymbolicExpr>> fields_;
    };

    /// @class Address
    /// @brief Symbolic address with unique ID, optional offset and origin.
    /// Origin can't be nullptr, use monostate or nullopt.
    class Address : public SymbolicExpr {
      public:
        Address(const Address &other)
            : SymbolicExpr(other), id_(other.id_),
              offset_(other.offset_ ? other.offset_->clone() : SymbolicExpr::makeNull()) {
            std::visit(
                [this](auto &&arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, std::monostate>) {
                        from_ = std::monostate{};
                    } else if constexpr (std::is_same_v<T, not_null<const clang::VarDecl *>>) {
                        from_ = arg;
                    } else if constexpr (std::is_same_v<T,
                                                        not_null<std::unique_ptr<const Address>>>) {
                        from_.emplace<2>(std::unique_ptr<Address>{
                            static_cast<Address *>(arg->clone().release())});
                    } else if constexpr (std::is_same_v<T, std::pair<not_null<std::shared_ptr<
                                                                         const Structure::Info>>,
                                                                     const size_t>>) {
                        from_.emplace<3>(arg);
                    }
                },
                other.from_);
        }
        Address &operator=(const Address &other) {
            if (this != &other) {
                SymbolicExpr::operator=(other);
                id_     = other.id_;
                offset_ = other.offset_ ? other.offset_->clone() : SymbolicExpr::makeNull();
                std::visit(
                    [this](auto &&arg) {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, std::monostate>) {
                            from_ = std::monostate{};
                        } else if constexpr (std::is_same_v<T, not_null<const clang::VarDecl *>>) {
                            from_ = arg;
                        } else if constexpr (std::is_same_v<
                                                 T, not_null<std::unique_ptr<const Address>>>) {
                            from_.emplace<2>(std::unique_ptr<Address>{
                                static_cast<Address *>(arg->clone().release())});
                        } else if constexpr (std::is_same_v<T, std::pair<not_null<std::shared_ptr<
                                                                             const Structure::Info>>,
                                                                         const size_t>>) {
                            from_.emplace<3>(arg);
                        }
                    },
                    other.from_);
            }
            return *this;
        }
        Address(Address &&)            = delete;
        Address &operator=(Address &&) = delete;

        Address() = delete;

        Address(unsigned int id,
                std::variant<std::monostate,
                             const clang::VarDecl *,
                             std::unique_ptr<const Address>,
                             std::pair<std::shared_ptr<const Structure::Info>, const size_t>> from)
            : SymbolicExpr(ExprType::SymbolAddress, {ScalarKind::UInt, 64}), id_(id),
              offset_(SymbolicExpr::makeNull()) {
            std::visit(
                [this](auto &&arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, std::monostate>) {
                        from_ = std::monostate{};
                    } else if constexpr (std::is_same_v<T, const clang::VarDecl *>) {
                        from_ = arg;
                    } else if constexpr (std::is_same_v<T, std::unique_ptr<const Address>>) {
                        from_.emplace<2>(std::move(arg));
                    } else if constexpr (std::is_same_v<
                                             T, std::pair<std::shared_ptr<const Structure::Info>,
                                                          const size_t>>) {
                        from_.emplace<3>(std::move(arg));
                    }
                },
                from);
        }

        Address(unsigned int id,
                std::unique_ptr<SymbolicExpr> offset,
                std::variant<std::monostate,
                             const clang::VarDecl *,
                             std::unique_ptr<const Address>,
                             std::pair<std::shared_ptr<const Structure::Info>, const size_t>> from)
            : SymbolicExpr(ExprType::SymbolAddress, {ScalarKind::UInt, 64}), id_(id),
              offset_(offset ? std::move(offset) : SymbolicExpr::makeNull()) {
            std::visit(
                [this](auto &&arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, std::monostate>) {
                        from_ = std::monostate{};
                    } else if constexpr (std::is_same_v<T, const clang::VarDecl *>) {
                        from_ = arg;
                    } else if constexpr (std::is_same_v<T, std::unique_ptr<const Address>>) {
                        from_.emplace<2>(std::move(arg));
                    } else if constexpr (std::is_same_v<
                                             T, std::pair<std::shared_ptr<const Structure::Info>,
                                                          const size_t>>) {
                        from_.emplace<3>(std::move(arg));
                    }
                },
                from);
        }

        std::unique_ptr<SymbolicExpr> clone() const override;
        unsigned int getId() const { return id_; }
        std::string dump() const override;
        virtual std::string regularForm(std::optional<std::string_view> prefix = std::nullopt,
                                        std::optional<std::string_view> suffix = std::nullopt,
                                        int parentPrec                         = 0,
                                        bool isRightChild = false) const override;

        /// @brief Get the value's regular form on this address.
        std::string regularFormOfValue(std::optional<std::string_view> prefix = std::nullopt,
                                       std::optional<std::string_view> suffix = std::nullopt,
                                       int parentPrec                         = 0,
                                       bool isRightChild                      = false) const;
        virtual std::unique_ptr<SymbolicExpr> simplifiedExpr() const override;
        virtual bool equal(const SymbolicExpr &expr) const override;

        SymbolicExpr *getOffset() const { return offset_.get(); }
        auto getFrom() const -> const auto & { return from_; }
        std::size_t hash() const override;

        std::string getBaseName() const;

        void setOffset(std::unique_ptr<SymbolicExpr> offset) { offset_ = std::move(offset); }
        void addOffset(std::unique_ptr<SymbolicExpr> extra);
        bool isOffseted() const { return offset_ && offset_->getType() != ExprType::SNULL; }

        // StInG: Support functions for affine invariant analysis
        bool isLinear() const override { return false; }
        int getMaxDegree() const override { return -1; }

      private:
        unsigned int id_; ///< This ID is unique within its path, but not globally unique across
                          ///< different paths.
        std::unique_ptr<SymbolicExpr> offset_; ///< Offset relative to an address.
        std::variant<std::monostate,
                     not_null<const clang::VarDecl *>,
                     not_null<std::unique_ptr<const Address>>,
                     std::pair<not_null<std::shared_ptr<const Structure::Info>>, const size_t>>
            from_; ///< from a VarDecl* means this is a variable's address, from another Address p
                   ///< means this is a value(may with offset) of a pointer variable whose address
                   ///< is p, from {Structure::Info, size_t} means this is a field.
    };

    struct AddressHash {
        std::size_t operator()(const Address &addr) const noexcept { return addr.hash(); }
    };

    struct AddressEqual {
        bool operator()(const Address &a, const Address &b) const noexcept { return a == b; }
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
        std::size_t hash() const override;
        virtual bool equal(const SymbolicExpr &expr) const override;
        auto getFrom() const -> const auto & { return from_; }

        std::unordered_map<unsigned int, const Variable *> collectUsedVars() const override;
        // StInG: Support functions for affine invariant analysis
        bool isLinear() const override { return true; }
        int getMaxDegree() const override { return 1; }
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const std::unordered_map<std::string, int> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr() const override;

      private:
        std::string name_; ///< May be incorrect in pointer-related contexts now, use regularForm or
                           ///< from_ as an alternative.
        Type varType_;     ///< Symbol value's type.
        int id_; ///< unique identifier to distinguish between variables with the same name
        std::variant<std::monostate,
                     not_null<std::unique_ptr<const Address>>,
                     std::pair<not_null<std::shared_ptr<const Structure::Info>>, const size_t>>
            from_; ///< The original Address of the value or the Structure it belongs.
    };

    class AddressRange : public Address {
      public:
      private:
        std::unique_ptr<SymbolicExpr> length_;
        std::unique_ptr<Variable>
            index_; ///< Vaule of this AddressRange may rely on this ghost variable.
    };

    std::unique_ptr<SymbolicExpr> createLNotExpr(std::unique_ptr<SymbolicExpr> expr);
    BinaryOpExpr::Operator getCompoundAssignOp(clang::BinaryOperatorKind compoundAssignOp);
    BinaryOpExpr::Operator getBinaryOp(clang::BinaryOperatorKind op);
    SymbolicExpr::Type deriveVarType(clang::QualType type);

} // namespace Symbolic

#endif // SYMBOLIC_H