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
        BinaryOp,
        UnaryOp
    };

    SymbolicExpr(Type type) : type_(type) {}
    virtual ~SymbolicExpr() = default;

    Type getType() const { return type_; }
    virtual std::string toString() const = 0;

  private:
    Type type_;
};

class LiteralExpr : public SymbolicExpr
{
  public:
    LiteralExpr(const std::string &value) : SymbolicExpr(Type::Literal), value_(value) {}

    std::string toString() const override { return value_; }

  private:
    std::string value_;
};

class VariableExpr : public SymbolicExpr
{
  public:
    VariableExpr(const std::string &name) : SymbolicExpr(Type::Variable), name_(name) {}

    std::string toString() const override { return name_; }

  private:
    std::string name_;
};

class BinaryOpExpr : public SymbolicExpr
{
  public:
    BinaryOpExpr(std::unique_ptr<SymbolicExpr> left,
        const std::string &op,
        std::unique_ptr<SymbolicExpr> right)
        : SymbolicExpr(Type::BinaryOp), left_(std::move(left)), op_(op), right_(std::move(right))
    {}

    std::string toString() const override
    {
        return "(" + left_->toString() + " " + op_ + " " + right_->toString() + ")";
    }

  private:
    std::unique_ptr<SymbolicExpr> left_;
    std::string op_;
    std::unique_ptr<SymbolicExpr> right_;
};

class UnaryOpExpr : public SymbolicExpr
{
  public:
    UnaryOpExpr(const std::string &op, std::unique_ptr<SymbolicExpr> expr)
        : SymbolicExpr(Type::UnaryOp), op_(op), expr_(std::move(expr))
    {}

    std::string toString() const override { return "(" + op_ + expr_->toString() + ")"; }

  private:
    std::string op_;
    std::unique_ptr<SymbolicExpr> expr_;
};

#endif // SYMBOLIC_H
