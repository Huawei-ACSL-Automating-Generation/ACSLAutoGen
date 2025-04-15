#ifndef SYMBOLIC_H
#define SYMBOLIC_H

#include <string>
#include <memory>

// Base class for symbolic expressions.
class SymbolicExpr
{
  public:
    enum class Type
    {
        Literal,
        Variable,
        SymbolAddress,
        BinaryOp,
        UnaryOp,
        ArraySubscript,
        SNULL
    };

    SymbolicExpr(Type type) : type_(type) {}
    virtual ~SymbolicExpr() = default;

    Type getType() const { return type_; }
    virtual std::unique_ptr<SymbolicExpr> clone() const = 0;

  private:
    Type type_;
};

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
    };

    LiteralExpr(bool value) : SymbolicExpr(Type::Literal), type(LiteralType::Boolean)
    {
        data.boolValue = value;
    }

    LiteralExpr(int value) : SymbolicExpr(Type::Literal), type(LiteralType::Int)
    {
        data.intValue = value;
    }

    LiteralExpr(unsigned int value) : SymbolicExpr(Type::Literal), type(LiteralType::UnsignedInt)
    {
        data.uintValue = value;
    }

    LiteralExpr(short value) : SymbolicExpr(Type::Literal), type(LiteralType::Short)
    {
        data.shortValue = value;
    }

    LiteralExpr(unsigned short value)
        : SymbolicExpr(Type::Literal), type(LiteralType::UnsignedShort)
    {
        data.ushortValue = value;
    }

    LiteralType getLiteralType() const { return type; }

    std::unique_ptr<SymbolicExpr> clone() const override;

  private:
    LiteralType type;
    union Data
    {
        bool boolValue;
        int intValue;
        unsigned int uintValue;
        short shortValue;
        unsigned short ushortValue;

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
        : SymbolicExpr(Type::BinaryOp), left_(std::move(left)), op_(op), right_(std::move(right))
    {}

    // Constructor accepting raw pointers for both operands
    BinaryOpExpr(SymbolicExpr *left, Operator op, SymbolicExpr *right)
        : SymbolicExpr(Type::BinaryOp), left_(left), op_(op), right_(right)
    {}

    std::unique_ptr<SymbolicExpr> clone() const override;

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

    // Constructor accepting unique_ptr for the operand.
    UnaryOpExpr(Operator op, std::unique_ptr<SymbolicExpr> expr)
        : SymbolicExpr(Type::UnaryOp), op_(op), expr_(std::move(expr))
    {}

    // Constructor accepting a raw pointer for the operand.
    UnaryOpExpr(Operator op, SymbolicExpr *expr) : SymbolicExpr(Type::UnaryOp), op_(op), expr_(expr)
    {}

    std::unique_ptr<SymbolicExpr> clone() const override;

  private:
    Operator op_;
    std::unique_ptr<SymbolicExpr> expr_;
};

class ArrayExpr : public SymbolicExpr
{
  public:
    ArrayExpr(std::unique_ptr<SymbolicExpr> array, std::unique_ptr<SymbolicExpr> index)
        : SymbolicExpr(Type::ArraySubscript), array_(std::move(array)), index_(std::move(index))
    {}

    ArrayExpr(SymbolicExpr *array, SymbolicExpr *index)
        : SymbolicExpr(Type::ArraySubscript), array_(array), index_(index)
    {}

    std::unique_ptr<SymbolicExpr> clone() const override;

  private:
    std::unique_ptr<SymbolicExpr> array_;
    std::unique_ptr<SymbolicExpr> index_;
};

class NullExpr : public SymbolicExpr
{
  public:
    NullExpr() : SymbolicExpr(Type::SNULL) {}
    ~NullExpr() = default;

    std::unique_ptr<SymbolicExpr> clone() const override;
};

class Variable : public SymbolicExpr
{
  public:
    enum class VarType
    {
        Int,
        UInt,
        Bool,
    };

    Variable(const std::string &name, VarType varType)
        : SymbolicExpr(Type::Variable), name_(name), varType_(varType)
    {}

    VarType getVarType() const { return varType_; }
    void setVarType(VarType varType) { varType_ = varType; }

    std::unique_ptr<SymbolicExpr> clone() const override;

  private:
    std::string name_;
    VarType varType_;
};

class Address : public SymbolicExpr
{
  public:
    Address() : SymbolicExpr(Type::SymbolAddress), id_(0), offset_(false) {}
    Address(unsigned int id) : SymbolicExpr(Type::SymbolAddress), id_(id) {}
    Address(unsigned int id, bool offset)
        : SymbolicExpr(Type::SymbolAddress), id_(id), offset_(offset)
    {}
    std::unique_ptr<SymbolicExpr> clone() const override;
    bool operator==(const Address &other) const
    {
        return id_ == other.id_ && offset_ == other.offset_;
    }
    unsigned int getId() const { return id_; }
    bool getOffset() const { return offset_; }

  private:
    unsigned int id_;
    bool offset_ = false;
};

struct AddressHash
{
    std::size_t operator()(const Address &addr) const noexcept
    {
        std::size_t h1 = std::hash<unsigned int>{}(addr.getId());
        std::size_t h2 = std::hash<bool>{}(addr.getOffset());
        return h1 ^ (h2 << 1);
    }
};
#endif // SYMBOLIC_H
