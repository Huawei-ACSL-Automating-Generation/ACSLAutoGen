#ifndef SYMBOLIC_H
#define SYMBOLIC_H

#include <string>
#include "macros.h"
#include <memory>
#include "ppl.hh"
#include <clang/AST/Decl.h>
#include <variant>

namespace Symbolic {
    class Variable;

    // Base class for symbolic expressions.
    class SymbolicExpr {
      public:
        enum class ExprType {
            Literal,
            Variable,
            SymbolAddress,
            BinaryOp,
            UnaryOp,
            SNULL
        };

        enum class ScalarKind {
            Int,
            UInt,
            Bool,
            Void,
        };

        struct Type {
            ScalarKind kind;
            unsigned bitWidth;
            friend bool operator==(const Type &LHS, const Type &RHS) {
                return LHS.kind == RHS.kind && LHS.bitWidth == RHS.bitWidth;
            }
        };

        SymbolicExpr(ExprType type, Type valueType) : type_(type), valueType_(valueType) {}
        virtual ~SymbolicExpr() = default;

        ExprType getType() const { return type_; }
        Type getValType() const { return valueType_; }
        void setValType(Type newType) { valueType_ = newType; }
        static std::unique_ptr<SymbolicExpr> makeNull();
        virtual std::unique_ptr<SymbolicExpr> clone() const = 0;
        virtual std::string dump() const                    = 0;
        /// @brief emit symbolic expressions in regular form compliant with ACSL
        /// @param old variables will be enclosed in \old()
        /// @return string that can be directly output in ACSL
        virtual std::string regularForm(bool old) const = 0;
        virtual bool equal(const SymbolicExpr &) const  = 0;
        virtual std::size_t hash() const                = 0;

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

        virtual void collectUsedVars(std::vector<Variable *> &) const {}

        //===----------------------------------------------------------------------===//
        // StInG Interface Utilities - Symbolic Expression Adapter
        //
        // This section defines support for converting internal symbolic expressions
        // into a form consumable by the StInG (Static Invariant Generator) tool,
        // which synthesizes affine invariants via constraint solving.
        //===----------------------------------------------------------------------===//

        // Returns true if the expression is linear (affine).
        virtual bool isLinear() const = 0;

        // Returns the degree of the polynomial represented by this expression.
        // Constants and variables are degree 0 and 1 respectively.
        // Nonlinear terms (e.g., x*y) have degree >= 2.
        virtual int getMaxDegree() const = 0;

        // Convert this symbolic expression into a PPL Linear_Expression.
        // Only valid for expressions that are affine (i.e., linear w.r.t. variables).
        // Throws or fails if the expression is not representable in linear form.
        virtual Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const std::unordered_map<std::string, int> &) const {
            ERROR("not implemented for expression type: ");
        }

        virtual Parma_Polyhedra_Library::Linear_Expression toLinearExpr() const {
            ERROR("not implemented for expression type: ");
        }

      private:
        ExprType type_;
        Type valueType_;
    };

    std::ostream &operator<<(std::ostream &os, SymbolicExpr::ExprType t);

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
        virtual std::string regularForm(bool old) const override;
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

    class BinaryOpExpr : public SymbolicExpr {
      public:
        enum class Operator {
            Multiply,
            Divide,
            Remainder,
            Add,
            Subtract,
            ShiftLeft,
            ShiftRight,
            LessThan,
            GreaterThan,
            LessEqual,
            GreaterEqual,
            Equal,
            NotEqual,
            BitAnd,
            BitXor,
            BitOr,
            LogicalAnd,
            LogicalOr,
        };

        // Constructor accepting unique_ptr for both operands

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
        std::string regularForm(bool old) const override;
        std::size_t hash() const override;
        virtual bool equal(const SymbolicExpr &expr) const override;

        void collectUsedVars(std::vector<Variable *> &vars) const override;
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

    class UnaryOpExpr : public SymbolicExpr {
      public:
        enum class Operator {
            Plus,       // +
            Minus,      // -
            LogicalNot, // !
            BitwiseNot, // ~
            PreInc,     // ++x
            PreDec,     // --x
            PostInc,    // x++
            PostDec,    // x--
            AddrOf,     // &
            Dereference // *
        };

        UnaryOpExpr(Operator op, std::unique_ptr<SymbolicExpr> expr)
            : SymbolicExpr(ExprType::UnaryOp, expr->getValType()), op_(op), expr_(std::move(expr)) {
        }

        std::unique_ptr<SymbolicExpr> clone() const override;
        std::string dump() const override;
        std::string regularForm(bool old) const override;
        std::size_t hash() const override;
        virtual bool equal(const SymbolicExpr &expr) const override;

        void collectUsedVars(std::vector<Variable *> &vars) const override;
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

    class NullExpr : public SymbolicExpr {
      public:
        NullExpr() : SymbolicExpr(ExprType::SNULL, {ScalarKind::UInt, 64}) {}
        ~NullExpr() = default;

        std::unique_ptr<SymbolicExpr> clone() const override;
        std::string dump() const override;
        std::string regularForm(bool old) const override;
        std::size_t hash() const override;
        virtual bool equal(const SymbolicExpr &expr) const override;

        // StInG: Support functions for affine invariant analysis
        bool isLinear() const override { return false; }
        int getMaxDegree() const override { return 0; }
    };

    class Address;
    class Variable : public SymbolicExpr {
      public:
        Variable(const std::string &name, Type varType, int id, std::unique_ptr<Address> from)
            : SymbolicExpr(ExprType::Variable, varType), name_(name), varType_(varType), id_(id),
              from_(std::move(from)) {}

        Type getVarType() const { return varType_; }
        void setVarType(Type vt) {
            varType_ = vt;
            setValType(vt);
        }

        const std::string &getName() const { return name_; }
        int getId() const { return id_; }

        std::unique_ptr<SymbolicExpr> clone() const override;
        std::string dump() const override;
        std::string regularForm(bool old) const override;
        std::size_t hash() const override;
        virtual bool equal(const SymbolicExpr &expr) const override;
        auto getFrom() const -> const auto & { return from_; }

        void collectUsedVars(std::vector<Variable *> &vars) const override;
        // StInG: Support functions for affine invariant analysis
        bool isLinear() const override { return true; }
        int getMaxDegree() const override { return 1; }
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr(
            const std::unordered_map<std::string, int> &) const override;
        Parma_Polyhedra_Library::Linear_Expression toLinearExpr() const override;

      private:
        std::string name_;
        Type varType_;
        int id_; // unique identifier to distinguish between variables with the same name
        std::unique_ptr<Address> from_;
    };

    class Address : public SymbolicExpr {
      public:
        Address(const Address &other)
            : SymbolicExpr(other), id_(other.id_),
              offset_(other.offset_ ? other.offset_->clone() : SymbolicExpr::makeNull()) {
            if (auto decl = std::get_if<const clang::VarDecl *>(&other.from_))
                from_ = *decl;
            else if (auto addr = std::get_if<std::unique_ptr<Address>>(&other.from_)) {
                from_ =
                    std::unique_ptr<Address>((static_cast<Address *>((*addr)->clone().release())));
            } else {
                from_ = nullptr;
            }
        }
        Address &operator=(const Address &other) {
            if (this != &other) {
                SymbolicExpr::operator=(other);
                id_     = other.id_;
                offset_ = other.offset_ ? other.offset_->clone() : SymbolicExpr::makeNull();
                if (auto decl = std::get_if<const clang::VarDecl *>(&other.from_))
                    from_ = *decl;
                else if (auto addr = std::get_if<std::unique_ptr<Address>>(&other.from_)) {
                    from_ = std::unique_ptr<Address>(
                        (static_cast<Address *>((*addr)->clone().release())));
                } else {
                    from_ = nullptr;
                }
            }
            return *this;
        }
        Address(Address &&)            = delete;
        Address &operator=(Address &&) = delete;

        Address() = delete;

        Address(unsigned int id,
                std::variant<const clang::VarDecl *, std::unique_ptr<Address>> from)
            : SymbolicExpr(ExprType::SymbolAddress, {ScalarKind::UInt, 64}), id_(id),
              offset_(SymbolicExpr::makeNull()), from_(std::move(from)) {}

        Address(unsigned int id,
                std::unique_ptr<SymbolicExpr> offset,
                std::variant<const clang::VarDecl *, std::unique_ptr<Address>> from)
            : SymbolicExpr(ExprType::SymbolAddress, {ScalarKind::UInt, 64}), id_(id),
              offset_(offset ? std::move(offset) : SymbolicExpr::makeNull()),
              from_(std::move(from)) {}

        std::unique_ptr<SymbolicExpr> clone() const override;
        unsigned int getId() const { return id_; }
        std::string dump() const override;
        std::string regularForm(bool old) const override;
        virtual bool equal(const SymbolicExpr &expr) const override;

        SymbolicExpr *getOffset() const { return offset_.get(); }
        auto getFrom() const -> const auto & { return from_; }
        std::size_t hash() const override;

        std::string getBaseName() const;

        void setOffset(std::unique_ptr<SymbolicExpr> offset) { offset_ = std::move(offset); }
        std::unique_ptr<Address> addOffset(std::unique_ptr<SymbolicExpr> extra) const;
        bool isOffseted() const { return offset_ && offset_->getType() != ExprType::SNULL; }

        // StInG: Support functions for affine invariant analysis
        bool isLinear() const override { return false; }
        int getMaxDegree() const override { return -1; }

      private:
        unsigned int id_;
        std::unique_ptr<SymbolicExpr> offset_;
        std::variant<const clang::VarDecl *, std::unique_ptr<Address>> from_;
    };

    struct AddressHash {
        std::size_t operator()(const Address &addr) const noexcept { return addr.hash(); }
    };

    struct AddressEqual {
        bool operator()(const Address &a, const Address &b) const noexcept { return a == b; }
    };

} // namespace Symbolic

#endif // SYMBOLIC_H