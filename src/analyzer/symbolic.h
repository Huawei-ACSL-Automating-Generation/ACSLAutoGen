#ifndef SYMBOLIC_H
#define SYMBOLIC_H

#include <string>
#include "macros.h"
#include <memory>

// Base class for symbolic expressions.
class SymbolicExpr
{
  public:
    enum class ExprType
    {
        Literal,
        Variable,
        SymbolAddress,
        BinaryOp,
        UnaryOp,
        SNULL
    };

    enum class ScalarKind
    {
        Int,
        UInt,
        Bool
    };

    struct Type
    {
        ScalarKind kind;
        unsigned bitWidth;
        friend bool operator==(const Type &LHS, const Type &RHS)
        {
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
    virtual bool equal(const SymbolicExpr &) const      = 0;
    virtual std::size_t hash() const                    = 0;

    friend std::ostream &operator<<(std::ostream &os, const SymbolicExpr &expr)
    {
        return os << expr.dump();
    }

    friend bool operator==(const SymbolicExpr &LHS, const SymbolicExpr &RHS)
    {
        if (LHS.type_ != RHS.type_)
            return false;
        return LHS.equal(RHS);
    }

    friend bool operator!=(const SymbolicExpr &LHS, const SymbolicExpr &RHS)
    {
        return !(LHS == RHS);
    }

  private:
    ExprType type_;
    Type valueType_;
};

std::ostream &operator<<(std::ostream &os, SymbolicExpr::ExprType t);

class LiteralExpr : public SymbolicExpr
{
  public:
    enum class LiteralType
    {
        Boolean,
        Int,
        UnsignedInt,
        Short,
        UnsignedShort,
        Int64,
        UInt64
    };

    LiteralExpr(bool value)
        : SymbolicExpr(ExprType::Literal, {ScalarKind::Bool, 1}), type(LiteralType::Boolean)
    {
        data.boolValue = value;
    }

    LiteralExpr(int value)
        : SymbolicExpr(ExprType::Literal, {ScalarKind::Int, 32}), type(LiteralType::Int)
    {
        data.intValue = value;
    }

    LiteralExpr(unsigned int value)
        : SymbolicExpr(ExprType::Literal, {ScalarKind::UInt, 32}), type(LiteralType::UnsignedInt)
    {
        data.uintValue = value;
    }

    LiteralExpr(short value)
        : SymbolicExpr(ExprType::Literal, {ScalarKind::Int, 16}), type(LiteralType::Short)
    {
        data.shortValue = value;
    }

    LiteralExpr(unsigned short value)
        : SymbolicExpr(ExprType::Literal, {ScalarKind::UInt, 16}), type(LiteralType::UnsignedShort)
    {
        data.ushortValue = value;
    }

    LiteralExpr(int64_t value)
        : SymbolicExpr(ExprType::Literal, {ScalarKind::Int, 64}), type(LiteralType::Int64)
    {
        data.int64Value = value;
    }

    LiteralExpr(uint64_t value)
        : SymbolicExpr(ExprType::Literal, {ScalarKind::UInt, 64}), type(LiteralType::UInt64)
    {
        data.uint64Value = value;
    }

    LiteralType getLiteralType() const { return type; }

    std::unique_ptr<SymbolicExpr> clone() const override;
    std::string dump() const override;
    std::size_t hash() const;
    virtual bool equal(const SymbolicExpr &expr) const override;

  private:
    LiteralType type;
    union Data
    {
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

class BinaryOpExpr : public SymbolicExpr
{
  public:
    enum class Operator
    {
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

    BinaryOpExpr(
        std::unique_ptr<SymbolicExpr> left, Operator op, std::unique_ptr<SymbolicExpr> right)
        : SymbolicExpr(ExprType::BinaryOp, left->getValType()), left_(std::move(left)), op_(op),
          right_(std::move(right))
    {}

    BinaryOpExpr(SymbolicExpr *left, Operator op, SymbolicExpr *right)
        : SymbolicExpr(ExprType::BinaryOp, left->getValType()), left_(left), op_(op), right_(right)
    {}

    std::unique_ptr<SymbolicExpr> clone() const override;
    std::string dump() const override;
    std::size_t hash() const;
    virtual bool equal(const SymbolicExpr &expr) const override;

  private:
    std::unique_ptr<SymbolicExpr> left_;
    Operator op_;
    std::unique_ptr<SymbolicExpr> right_;
};

class UnaryOpExpr : public SymbolicExpr
{
  public:
    enum class Operator
    {
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
        : SymbolicExpr(ExprType::UnaryOp, expr->getValType()), op_(op), expr_(std::move(expr))
    {}

    UnaryOpExpr(Operator op, SymbolicExpr *expr)
        : SymbolicExpr(ExprType::UnaryOp, expr->getValType()), op_(op), expr_(expr)
    {}

    std::unique_ptr<SymbolicExpr> clone() const override;
    std::string dump() const override;
    std::size_t hash() const;
    virtual bool equal(const SymbolicExpr &expr) const override;

  private:
    Operator op_;
    std::unique_ptr<SymbolicExpr> expr_;
};

class NullExpr : public SymbolicExpr
{
  public:
    NullExpr() : SymbolicExpr(ExprType::SNULL, {ScalarKind::UInt, 64}) {}
    ~NullExpr() = default;

    std::unique_ptr<SymbolicExpr> clone() const override;
    std::string dump() const override;
    std::size_t hash() const;
    virtual bool equal(const SymbolicExpr &expr) const override;
};

class Variable : public SymbolicExpr
{
  public:
    Variable(const std::string &name, Type varType)
        : SymbolicExpr(ExprType::Variable, varType), name_(name), varType_(varType)
    {}

    Type getVarType() const { return varType_; }
    void setVarType(Type vt)
    {
        varType_ = vt;
        setValType(vt);
    }
    const std::string &getName() const { return name_; }

    std::unique_ptr<SymbolicExpr> clone() const override;
    std::string dump() const override;
    std::size_t hash() const;
    virtual bool equal(const SymbolicExpr &expr) const override;

  private:
    std::string name_;
    Type varType_;
};

class Address : public SymbolicExpr
{
  public:
    Address(const Address &other)
        : SymbolicExpr(other), id_(other.id_),
          offset_(other.offset_ ? other.offset_->clone() : SymbolicExpr::makeNull())
    {}

    Address &operator=(const Address &other)
    {
        if (this != &other)
        {
            SymbolicExpr::operator=(other);
            id_     = other.id_;
            offset_ = other.offset_ ? other.offset_->clone() : SymbolicExpr::makeNull();
        }
        return *this;
    }
    Address(Address &&)            = delete;
    Address &operator=(Address &&) = delete;

    Address()
        : SymbolicExpr(ExprType::SymbolAddress, {ScalarKind::UInt, 64}), id_(0),
          offset_(SymbolicExpr::makeNull())
    {}

    Address(unsigned int id)
        : SymbolicExpr(ExprType::SymbolAddress, {ScalarKind::UInt, 64}), id_(id),
          offset_(SymbolicExpr::makeNull())
    {}

    Address(unsigned int id, std::unique_ptr<SymbolicExpr> offset)
        : SymbolicExpr(ExprType::SymbolAddress, {ScalarKind::UInt, 64}), id_(id),
          offset_(offset ? std::move(offset) : SymbolicExpr::makeNull())
    {}

    bool operator==(const Address &o) const noexcept
    {
        return id_ == o.id_ &&
               ((offset_ && o.offset_ && offset_->equal(*o.offset_)) || (!offset_ && !o.offset_));
    }

    std::unique_ptr<SymbolicExpr> clone() const override;
    unsigned int getId() const { return id_; }
    std::string dump() const override;
    virtual bool equal(const SymbolicExpr &expr) const override;

    SymbolicExpr *getOffset() const { return offset_.get(); }
    std::size_t hash() const;
    void setOffset(std::unique_ptr<SymbolicExpr> offset) { offset_ = std::move(offset); }
    std::unique_ptr<Address> addOffset(std::unique_ptr<SymbolicExpr> extra) const;
    bool isOffseted() const { return offset_ && offset_->getType() != ExprType::SNULL; }

  private:
    unsigned int id_;
    std::unique_ptr<SymbolicExpr> offset_;
};

struct AddressHash
{
    std::size_t operator()(const Address &addr) const noexcept { return addr.hash(); }
};

struct AddressEqual
{
    bool operator()(const Address &a, const Address &b) const noexcept { return a == b; }
};
#endif // SYMBOLIC_H
