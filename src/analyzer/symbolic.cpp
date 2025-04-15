#include "symbolic.h"
#include <memory>

using namespace std;
unique_ptr<SymbolicExpr> LiteralExpr::clone() const
{
    switch (getLiteralType())
    {
    case LiteralType::Boolean: return make_unique<LiteralExpr>(data.boolValue);
    case LiteralType::Int: return make_unique<LiteralExpr>(data.intValue);
    case LiteralType::UnsignedInt: return make_unique<LiteralExpr>(data.uintValue);
    case LiteralType::Short: return make_unique<LiteralExpr>(data.shortValue);
    case LiteralType::UnsignedShort: return make_unique<LiteralExpr>(data.ushortValue);
    }
    return nullptr;
}

unique_ptr<SymbolicExpr> BinaryOpExpr::clone() const
{
    return make_unique<BinaryOpExpr>(left_->clone(), op_, right_->clone());
}

unique_ptr<SymbolicExpr> UnaryOpExpr::clone() const
{
    return make_unique<UnaryOpExpr>(op_, expr_->clone());
}

unique_ptr<SymbolicExpr> ArrayExpr::clone() const
{
    return make_unique<ArrayExpr>(array_->clone(), index_->clone());
}

unique_ptr<SymbolicExpr> NullExpr::clone() const { return make_unique<NullExpr>(); }

unique_ptr<SymbolicExpr> Variable::clone() const { return make_unique<Variable>(name_, varType_); }

unique_ptr<SymbolicExpr> Address::clone() const { return make_unique<Address>(id_, offset_); }
