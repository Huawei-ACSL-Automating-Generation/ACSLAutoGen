#include <memory>
#include <sstream>
#include <cstring>
#include <variant>
#include <ranges>
#include <llvm/ADT/TypeSwitch.h>
#include "expr.h"
#include "macros.h"

namespace acslg::analyzer::symbolic {
    namespace {
        template <class... Ts> struct overloaded : Ts... {
            using Ts::operator()...;
        };

        template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

        using Type       = SymbolicExpr::Type;
        using ScalarKind = SymbolicExpr::ScalarKind;

        inline bool isIntLike(ScalarKind k) {
            return k == ScalarKind::Int || k == ScalarKind::UInt || k == ScalarKind::Bool;
        }

        inline uint64_t maskN(unsigned n) {
            return (n >= 64) ? ~uint64_t(0) : ((uint64_t(1) << n) - 1);
        }

        inline Type unify(Type a, Type b) {
            if (!isIntLike(a.kind) || !isIntLike(b.kind))
                return {ScalarKind::Void, 0};
            unsigned bw = std::max(a.bitWidth ? a.bitWidth : 1u, b.bitWidth ? b.bitWidth : 1u);
            if (bw <= 8)
                bw = 8;
            else if (bw <= 16)
                bw = 16;
            else if (bw <= 32)
                bw = 32;
            else
                bw = 64;
            bool uns = (a.kind == ScalarKind::UInt) || (b.kind == ScalarKind::UInt);
            return {uns ? ScalarKind::UInt : ScalarKind::Int, bw};
        }

        inline uint64_t coerceU(unsigned bw, uint64_t x) { return x & maskN(bw ? bw : 64); }
        inline int64_t coerceS(unsigned bw, uint64_t x) {
            x &= maskN(bw ? bw : 64);
            if (bw < 64 && (x & (uint64_t(1) << (bw - 1))))
                x |= ~maskN(bw);
            return (int64_t)x;
        }

        inline std::unique_ptr<LiteralExpr> makeLiteralFromUnifiedType(Type t,
                                                                       bool asBool,
                                                                       uint64_t raw) {
            if (asBool)
                return std::make_unique<LiteralExpr>(asBool);

            unsigned bw = t.bitWidth ? t.bitWidth : 64;
            if (t.kind == ScalarKind::UInt) {
                uint64_t u = coerceU(bw, raw);
                if (bw <= 16)
                    return std::make_unique<LiteralExpr>((unsigned short)u);
                if (bw <= 32)
                    return std::make_unique<LiteralExpr>((unsigned int)u);
                return std::make_unique<LiteralExpr>((uint64_t)u);
            } else { // Int
                int64_t s = coerceS(bw, raw);
                if (bw <= 16)
                    return std::make_unique<LiteralExpr>((short)s);
                if (bw <= 32)
                    return std::make_unique<LiteralExpr>((int)s);
                return std::make_unique<LiteralExpr>((int64_t)s);
            }
        }

        inline uint64_t literalRawU(const LiteralExpr &L) {
            switch (L.getLiteralType()) {
                case LiteralExpr::LiteralType::Boolean: return L.getLiteralValue() != 0 ? 1u : 0u;
                case LiteralExpr::LiteralType::Int: return (uint64_t)(int64_t)L.getLiteralValue();
                case LiteralExpr::LiteralType::UnsignedInt: return (uint64_t)L.getLiteralValue();
                case LiteralExpr::LiteralType::Short: return (uint64_t)(int64_t)L.getLiteralValue();
                case LiteralExpr::LiteralType::UnsignedShort: return (uint64_t)L.getLiteralValue();
                case LiteralExpr::LiteralType::Int64: return (uint64_t)(int64_t)L.getLiteralValue();
                case LiteralExpr::LiteralType::UInt64: return (uint64_t)L.getLiteralValue();
            }
            return 0;
        }
        inline bool literalAsBool(const LiteralExpr &L) { return L.getLiteralValue() != 0; }
    } // namespace

    utils::not_null<std::unique_ptr<UnknownExpr>> UnknownExpr::makeUnknown() {
        return std::make_unique<UnknownExpr>();
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> SymbolicExpr::simplifiedExprIfLinear() const {
        if (!isLinear())
            return clone();
        auto [hashPtrMap, hashIdMap] = collectUsedVarsAndAddrs(*this);

        auto linearExpr = toLinearExpr(hashIdMap);
        std::optional<utils::not_null<std::unique_ptr<SymbolicExpr>>> result{};

        using enum BinaryOpExpr::Operator;
        for (auto [hash, varOrAddr] : hashPtrMap) {
            std::visit(
                [&](auto &&arg) {
                    auto C = linearExpr
                                 .coefficient(Parma_Polyhedra_Library::Variable{hashIdMap.at(hash)})
                                 .get_si();
                    if (C == 0)
                        return;

                    if (result == std::nullopt) {
                        if (C == 1)
                            result = arg->clone();
                        else
                            result = std::make_unique<BinaryOpExpr>(
                                std::make_unique<LiteralExpr>(C), Multiply, arg->clone());
                    } else {
                        unsigned absC = std::abs(C);
                        std::unique_ptr<SymbolicExpr> varExpr{nullptr};
                        if (absC != 1)
                            varExpr = std::make_unique<BinaryOpExpr>(
                                std::make_unique<LiteralExpr>(absC), Multiply, arg->clone());
                        else
                            varExpr = arg->clone().into_underlying();
                        result = std::make_unique<BinaryOpExpr>(std::move(result.value()),
                                                                (C > 0 ? Add : Subtract),
                                                                std::move(varExpr));
                    }
                },
                varOrAddr);
        }
        if (auto inhomo = linearExpr.inhomogeneous_term().get_si();
            inhomo || result == std::nullopt) {
            if (result != std::nullopt) {
                result = std::make_unique<BinaryOpExpr>(
                    std::move(result.value()), (inhomo > 0 ? Add : Subtract),
                    std::make_unique<LiteralExpr>(std::abs(inhomo)));
            } else
                result = std::make_unique<LiteralExpr>(inhomo);
        }
        if (result == std::nullopt) {
            ERROR("Simplified expr is null! Something goes wrong.");
        }
        return std::move(result.value());
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> LiteralExpr::clone() const {
        switch (getLiteralType()) {
            case LiteralType::Boolean: return std::make_unique<LiteralExpr>(data_.boolValue);
            case LiteralType::Int: return std::make_unique<LiteralExpr>(data_.intValue);
            case LiteralType::UnsignedInt: return std::make_unique<LiteralExpr>(data_.uintValue);
            case LiteralType::Short: return std::make_unique<LiteralExpr>(data_.shortValue);
            case LiteralType::UnsignedShort:
                return std::make_unique<LiteralExpr>(data_.ushortValue);
            case LiteralType::Int64: return std::make_unique<LiteralExpr>(data_.int64Value);
            case LiteralType::UInt64: return std::make_unique<LiteralExpr>(data_.uint64Value);
        }

        UNREACHABLE();
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> BinaryOpExpr::clone() const {
        return std::make_unique<BinaryOpExpr>(left_->clone(), op_, right_->clone());
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> UnaryOpExpr::clone() const {
        return std::make_unique<UnaryOpExpr>(op_, expr_->clone());
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> UnknownExpr::clone() const {
        return std::make_unique<UnknownExpr>();
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> SymbolValue::clone() const {
        return std::make_unique<SymbolValue>(*this);
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> SymbolAddress::clone() const {
        return std::make_unique<SymbolAddress>(*this);
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> VariableAddress::clone() const {
        return std::make_unique<VariableAddress>(*this);
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> FieldAddress::clone() const {
        return std::make_unique<FieldAddress>(*this);
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> Structure::clone() const {
        return std::make_unique<Structure>(*this);
    }

    utils::not_null<std::unique_ptr<Address>> SymbolAddress::addressClone() const {
        return std::make_unique<SymbolAddress>(*this);
    }

    utils::not_null<std::unique_ptr<Address>> VariableAddress::addressClone() const {
        return std::make_unique<VariableAddress>(*this);
    }

    utils::not_null<std::unique_ptr<Address>> FieldAddress::addressClone() const {
        return std::make_unique<FieldAddress>(*this);
    }

    int64_t LiteralExpr::getLiteralValue() const {
        switch (getLiteralType()) {
            case LiteralType::Boolean: return data_.boolValue;
            case LiteralType::Int: return data_.intValue;
            case LiteralType::UnsignedInt: return data_.uintValue;
            case LiteralType::Short: return data_.shortValue;
            case LiteralType::UnsignedShort: return data_.ushortValue;
            case LiteralType::Int64: return data_.int64Value;
            case LiteralType::UInt64: return data_.uint64Value;
        }

        UNREACHABLE();
        return 0;
    }

    size_t LiteralExpr::hash() const {
        size_t seed = utils::hash_val(getType(), type_);

        switch (type_) {
            using enum LiteralType;
            case Boolean: return utils::hash_val(seed, data_.boolValue);
            case Int: return utils::hash_val(seed, data_.intValue);
            case UnsignedInt: return utils::hash_val(seed, data_.uintValue);
            case Short: return utils::hash_val(seed, data_.shortValue);
            case UnsignedShort: return utils::hash_val(seed, data_.ushortValue);
            case Int64: return utils::hash_val(seed, data_.int64Value);
            case UInt64: return utils::hash_val(seed, data_.uint64Value);
            default: ERROR("Wrong type.");
        }
    }

    size_t SymbolValue::hash() const {
        size_t seed = utils::hash_val(getType(), fromAddr_->hash(), fromPoint_.hash());
        return seed;
    }

    size_t UnaryOpExpr::hash() const {
        return utils::hash_val(getType(), static_cast<size_t>(op_), expr_->hash());
    }

    size_t BinaryOpExpr::hash() const {
        return utils::hash_val(getType(), static_cast<size_t>(op_), left_->hash(), right_->hash());
    }

    size_t SymbolAddress::hash() const {
        size_t seed = utils::hash_val(getType(), fromPoint_.hash(), offset_->hash(),
                                      range_ ? range_.value().len_->hash() : 0);

        std::visit(
            [&](auto &&arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    return;
                } else if constexpr (std::is_same_v<
                                         T, utils::not_null<std::unique_ptr<const Address>>>) {
                    seed = utils::hash_val(seed, arg->hash());
                }
            },
            fromAddr_);
        return seed;
    }

    size_t VariableAddress::hash() const { return utils::hash_val(getType(), from_.get()); }

    size_t FieldAddress::hash() const {
        return utils::hash_val(getType(), from_.first->hash(), from_.second);
    }

    size_t Structure::hash() const {
        auto seed = utils::hash_val(getType(), info_.definition_.get());
        for (auto &field : fields_)
            seed = utils::hash_val(seed, field->hash());
        return seed;
    }

    size_t UnknownExpr::hash() const { return utils::hash_val(getType()); }

    std::string LiteralExpr::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;

        switch (getLiteralType()) {
            case LiteralType::Boolean:
                oss << type("Boolean") << "(" << lit(data_.boolValue ? "true" : "false") << ")";
                break;
            case LiteralType::Int:
                oss << type("Int") << "(" << lit(std::to_string(data_.intValue)) << ")";
                break;
            case LiteralType::UnsignedInt:
                oss << type("UnsignedInt") << "(" << lit(std::to_string(data_.uintValue)) << ")";
                break;
            case LiteralType::Short:
                oss << type("Short") << "(" << lit(std::to_string(data_.shortValue)) << ")";
                break;
            case LiteralType::UnsignedShort:
                oss << type("UnsignedShort") << "(" << lit(std::to_string(data_.ushortValue))
                    << ")";
                break;
            case LiteralType::Int64:
                oss << type("Int64") << "(" << lit(std::to_string(data_.int64Value)) << ")";
                break;
            case LiteralType::UInt64:
                oss << type("UInt64") << "(" << lit(std::to_string(data_.uint64Value)) << ")";
                break;
        }
        return oss.str();
    }

    std::string BinaryOpExpr::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        std::string opStr;
        switch (op_) {
            case Operator::Multiply: opStr = "*"; break;
            case Operator::Divide: opStr = "/"; break;
            case Operator::Remainder: opStr = "%"; break;
            case Operator::Add: opStr = "+"; break;
            case Operator::Subtract: opStr = "-"; break;
            case Operator::ShiftLeft: opStr = "<<"; break;
            case Operator::ShiftRight: opStr = ">>"; break;
            case Operator::LessThan: opStr = "<"; break;
            case Operator::GreaterThan: opStr = ">"; break;
            case Operator::LessEqual: opStr = "<="; break;
            case Operator::GreaterEqual: opStr = ">="; break;
            case Operator::Equal: opStr = "=="; break;
            case Operator::NotEqual: opStr = "!="; break;
            case Operator::BitAnd: opStr = "&"; break;
            case Operator::BitXor: opStr = "^"; break;
            case Operator::BitOr: opStr = "|"; break;
            case Operator::LogicalAnd: opStr = "&&"; break;
            case Operator::LogicalOr: opStr = "||"; break;
            default: opStr = "?"; break;
        }
        oss << "(" << left_->dump() << " " << op(opStr) << " " << right_->dump() << ")";
        return oss.str();
    }

    std::string UnaryOpExpr::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        std::string opStr;
        switch (op_) {
            case Operator::Plus: opStr = "+"; break;
            case Operator::Minus: opStr = "-"; break;
            case Operator::LogicalNot: opStr = "!"; break;
            case Operator::BitwiseNot: opStr = "~"; break;
            case Operator::PreInc: opStr = "++"; break;
            case Operator::PreDec: opStr = "--"; break;
            case Operator::PostInc: opStr = "++"; break;
            case Operator::PostDec: opStr = "--"; break;
            case Operator::AddrOf: opStr = "&"; break;
            case Operator::Dereference: opStr = "*"; break;
            default: opStr = "?"; break;
        }
        oss << op(opStr) << "(" << expr_->dump() << ")";
        return oss.str();
    }

    std::string UnknownExpr::dump() const { return utils::dump_fmt::hint("{unknown}"); }

    template <class FromVariant>
    static inline void dump_from(std::ostringstream &oss, const FromVariant &from) {
        using namespace utils::dump_fmt;
        std::visit(overloaded{[&](std::monostate) { oss << hint("none"); },
                              [&](utils::not_null<const clang::VarDecl *> d) {
                                  const clang::Decl *decl = d.get();
                                  if (auto *nd = llvm::dyn_cast<clang::NamedDecl>(decl))
                                      oss << key("decl") << ":" << nd->getDeclKindName() << " "
                                          << path(nd->getQualifiedNameAsString());
                                  else
                                      oss << key("decl") << ":" << decl->getDeclKindName();
                              },
                              [&](const utils::not_null<std::unique_ptr<const Address>> &p) {
                                  oss << key("addr") << ":" << p->dump();
                              },
                              [&](const std::pair<utils::not_null<std::unique_ptr<const Address>>,
                                                  const size_t> &s) {
                                  oss << key("field of") << ":" << s.first.get()->dump() << "["
                                      << utils::dump_fmt::lit(std::to_string(s.second)) << "]";
                              }},
                   from);
    }

    std::string SymbolValue::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        const auto &t = getValType();

        oss << type("Var") << "(";
        switch (t.kind) {
            case ScalarKind::Int: oss << "int"; break;
            case ScalarKind::UInt: oss << "uint"; break;
            case ScalarKind::Bool: oss << "bool"; break;
            case ScalarKind::Void: oss << "void"; break;
            case ScalarKind::Structure: ERROR("SymbolValue's ScalarKind should not be Structure");
        }
        oss << lit(std::to_string(t.bitWidth)) << ")";

        oss << " {" << key("from address") << "=";
        oss << key("addr") << ":" << fromAddr_->dump();
        oss << "}, ";

        oss << "{" << key("from point") << "=" << path(fromPoint_.dump()) << "}";
        return oss.str();
    }

    std::string SymbolAddress::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        oss << type("SymbolAddress");

        auto off = getOffset();
        if (range_ == std::nullopt)
            oss << "[" << off->dump() << "]";
        else
            oss << "[" << off->dump() << " " << hint("... +") << range_.value().len_->dump() << "]";

        oss << " {" << key("from") << "=";
        dump_from(oss, fromAddr_);
        oss << "}, "
            << "{" << key("from point") << "=" << path(fromPoint_.dump()) << "}";
        return oss.str();
    }

    std::string VariableAddress::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        oss << type("VariableAddress") << " {" << key("from") << "=";
        if (auto *nd = llvm::dyn_cast<clang::NamedDecl>(from_.get()))
            oss << key("decl") << ":" << nd->getDeclKindName() << " "
                << path(nd->getQualifiedNameAsString());
        else
            oss << key("decl") << ":" << from_->getDeclKindName();
        oss << "}";
        return oss.str();
    }

    std::string FieldAddress::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        oss << type("FieldAddress") << " {" << key("from") << "=";
        oss << key("field of") << ":" << from_.first.get()->dump() << "["
            << utils::dump_fmt::lit(std::to_string(from_.second)) << "]";
        oss << "}";
        return oss.str();
    }

    std::string Structure::Info::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        std::string structName = definition_->getNameAsString();
        uint64_t sizeBits      = static_cast<uint64_t>(layout_.getSize().getQuantity()) * 8;

        oss << type("Struct") << "(" << accent(structName) << ", " << key("size") << "="
            << lit(std::to_string(sizeBits)) << " " << hint("bits") << ")";
        return oss.str();
    }

    std::string Structure::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;

        oss << info_.dump();
        oss << ", " << key("fields") << "=[";
        for (size_t i = 0; i < fields_.size(); ++i) {
            oss << fields_[i]->dump();
            if (i + 1 < fields_.size())
                oss << ", ";
        }
        oss << "]";
        return oss.str();
    }

    std::string SourcePoint::dump() const {
        using namespace utils::dump_fmt;
        if (loc_.isInvalid())
            ERROR("Invalid SourcePoint.");

        auto ploc = SM_.getPresumedLoc(loc_);
        if (ploc.isInvalid())
            ERROR("Invalid presumed SourcePoint.");

        std::ostringstream oss;
        oss << path(ploc.getFilename()) << ":" << lit(std::to_string(ploc.getLine())) << ":"
            << lit(std::to_string(ploc.getColumn()));
        return oss.str();
    }

    std::optional<std::string> LiteralExpr::regularForm(std::optional<std::string_view>,
                                                        std::optional<std::string_view>,
                                                        int,
                                                        bool) const {
        std::ostringstream oss;
        switch (getLiteralType()) {
            case LiteralType::Boolean: oss << (data_.boolValue ? "true" : "false"); break;
            case LiteralType::Int: oss << data_.intValue; break;
            case LiteralType::UnsignedInt: oss << data_.uintValue; break;
            case LiteralType::Short: oss << data_.shortValue; break;
            case LiteralType::UnsignedShort: oss << data_.ushortValue; break;
            case LiteralType::Int64: oss << data_.int64Value; break;
            case LiteralType::UInt64: oss << data_.uint64Value; break;
        }
        return oss.str();
    }

    std::optional<std::string> BinaryOpExpr::regularForm(std::optional<std::string_view> prefix,
                                                         std::optional<std::string_view> suffix,
                                                         int parentPrec,
                                                         bool isRightChild) const {
        std::ostringstream oss;
        std::string opStr;
        switch (op_) {
#define BIN_OP(name, tok, prec, isRight)                                                           \
    case Operator::name: opStr = tok; break;
#include "operators.def"
            default: opStr = "?"; break;
        }

        int myPrec      = getPrecedence(op_);
        bool needParens = (myPrec < parentPrec) ||
                          (myPrec == parentPrec && isRightChild && !isRightAssociative(op_));

        auto leftStr = left_->regularForm(prefix, suffix, myPrec, false);
        if (leftStr == std::nullopt)
            return std::nullopt;
        auto rightStr = right_->regularForm(prefix, suffix, myPrec, true);
        if (rightStr == std::nullopt)
            return std::nullopt;
        oss << (needParens ? "(" : "") << leftStr.value() << " " << opStr << " " << rightStr.value()
            << (needParens ? ")" : "");
        return oss.str();
    }

    std::optional<std::string> UnaryOpExpr::regularForm(std::optional<std::string_view> prefix,
                                                        std::optional<std::string_view> suffix,
                                                        int parentPrec,
                                                        bool) const {
        std::ostringstream oss;
        std::string opStr;
        switch (op_) {
#define UN_OP(name, tok, prec, isRight)                                                            \
    case Operator::name: opStr = tok; break;
#include "operators.def"
            default: opStr = "?"; break;
        }

        int myPrec      = getPrecedence(op_);
        bool needParens = myPrec < parentPrec;

        if (op_ == Operator::PostInc || op_ == Operator::PostDec) {
            auto subStr = expr_->regularForm(prefix, suffix, myPrec, false);
            if (subStr == std::nullopt)
                return std::nullopt;
            oss << (needParens ? "(" : "") << subStr.value() << opStr << (needParens ? ")" : "");
        } else {
            auto subStr = expr_->regularForm(prefix, suffix, myPrec, true);
            if (subStr == std::nullopt)
                return std::nullopt;
            oss << (needParens ? "(" : "") << opStr << subStr.value() << (needParens ? ")" : "");
        }
        return oss.str();
    }

    std::optional<std::string> UnknownExpr::regularForm(std::optional<std::string_view>,
                                                        std::optional<std::string_view>,
                                                        int,
                                                        bool) const {
        WARN("Output UnknownExpr's regular form, something may go wrong.");
        return "{unknown}";
    }

    std::optional<std::string> SymbolValue::regularForm(std::optional<std::string_view> prefix,
                                                        std::optional<std::string_view> suffix,
                                                        int,
                                                        bool) const {
        auto addr = fromAddr_->regularForm();
        if (addr == std::nullopt)
            return std::nullopt;
        if (addr.value().length() == 0) {
            ERROR("Empty regular from.");
        }
        if (addr.value()[0] == '&')
            return std::string{prefix.value_or("")} + addr.value().substr(1) +
                   std::string{suffix.value_or("")};
        return std::string{prefix.value_or("(")} + "*" + addr.value() +
               std::string{suffix.value_or(")")};
    }

    std::optional<std::string> SymbolAddress::regularForm(std::optional<std::string_view> prefix,
                                                          std::optional<std::string_view> suffix,
                                                          int,
                                                          bool) const {
        if (isRange())
            ERROR("Address range has no regularForm but regularFormOfValue.");
        return std::visit(
            [&, this](auto &&arg) -> std::optional<std::string> {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    // ERROR("Trying to get regular form of address without from_.");
                    // here too.
                    return std::nullopt;
                } else if constexpr (std::is_same_v<
                                         T, utils::not_null<std::unique_ptr<const Address>>>) {
                    auto nameStr = arg->regularFormOfValue(prefix, suffix);
                    if (nameStr == std::nullopt)
                        return std::nullopt;
                    auto offsetStr = offset_->regularForm(prefix, suffix);
                    if (offsetStr == std::nullopt)
                        return std::nullopt;

                    if (offsetStr.value() == "0")
                        offsetStr.value() = "";

                    if (!offsetStr.value().empty())
                        return "(" + std::string{prefix.value_or("")} + nameStr.value() +
                               std::string{suffix.value_or("")} + "+" + offsetStr.value() + ")";
                    else
                        return nameStr;
                }
            },
            fromAddr_);
    }

    std::optional<std::string> VariableAddress::regularForm(std::optional<std::string_view> prefix,
                                                            std::optional<std::string_view> suffix,
                                                            int,
                                                            bool) const {
        return "&" + (prefix ? (std::string)*prefix : "") + from_->getNameAsString() +
               (suffix ? (std::string)*suffix : "");
    }

    std::optional<std::string> FieldAddress::regularForm(std::optional<std::string_view> prefix,
                                                         std::optional<std::string_view> suffix,
                                                         int,
                                                         bool) const {
        auto &[fromAddr, index] = from_;
        auto baseStr            = fromAddr->regularForm();
        if (baseStr == std::nullopt)
            return std::nullopt;
        if (baseStr.value().empty())
            ERROR("Empty base std::string");
        auto fields = definition_->fields();
        auto it     = std::ranges::next(fields.begin(), index, fields.end());
        if (it == fields.end())
            ERROR("Out-of-bounds access");
        auto fieldStr = it->getNameAsString();

        std::string concatenatedStr;
        if (baseStr.value().at(0) == '&') {
            concatenatedStr = baseStr.value().substr(1) + "." + fieldStr;
        } else {
            concatenatedStr = baseStr.value() + "->" + fieldStr;
        }
        return "&" + std::string{prefix.value_or("")} + concatenatedStr +
               std::string{suffix.value_or("")};
    }

    std::optional<std::string> SymbolAddress::regularFormOfValue(
        std::optional<std::string_view> prefix,
        std::optional<std::string_view> suffix,
        int,
        bool) const {
        if (!isRange()) {
            return std::visit(
                [&, this](auto &&arg) -> std::optional<std::string> {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, std::monostate>) {
                        ERROR("Trying to get regular form of address without from_.");
                    } else if constexpr (std::is_same_v<
                                             T, utils::not_null<std::unique_ptr<const Address>>>) {
                        auto nameStr = arg->regularFormOfValue(prefix, suffix);
                        if (nameStr == std::nullopt)
                            return std::nullopt;
                        auto offsetStr = offset_->regularForm(prefix, suffix);
                        if (offsetStr == std::nullopt)
                            return std::nullopt;

                        return nameStr.value() + "[" + offsetStr.value() + "]";
                    }
                },
                fromAddr_);
        } else {
            return std::visit(
                [&, this](auto &&arg) -> std::optional<std::string> {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, std::monostate>) {
                        // here also.
                        return std::nullopt;
                    } else if constexpr (std::is_same_v<
                                             T, utils::not_null<std::unique_ptr<const Address>>>) {
                        auto nameStr = arg->regularFormOfValue(prefix, suffix);
                        if (nameStr == std::nullopt)
                            return std::nullopt;
                        auto offsetStr = getOffset()->regularForm(prefix, suffix);
                        if (offsetStr == std::nullopt)
                            return std::nullopt;

                        auto rangeStr =
                            std::make_unique<BinaryOpExpr>(
                                std::make_unique<BinaryOpExpr>(getOffset()->clone(),
                                                               BinaryOpExpr::Operator::Add,
                                                               range_.value().len_->clone()),
                                BinaryOpExpr::Operator::Subtract, std::make_unique<LiteralExpr>(1))
                                ->simplifiedExpr()
                                ->regularForm(prefix, suffix);
                        if (rangeStr == std::nullopt)
                            return std::nullopt;

                        return nameStr.value() + "[" + offsetStr.value() + ".." + rangeStr.value() +
                               "]";
                    }
                },
                getFromAddr());
        }
    }

    std::optional<std::string> VariableAddress::regularFormOfValue(
        std::optional<std::string_view> prefix,
        std::optional<std::string_view> suffix,
        int,
        bool) const {
        return (prefix ? (std::string)*prefix : "") + from_->getNameAsString() +
               (suffix ? (std::string)*suffix : "");
    }

    std::optional<std::string> FieldAddress::regularFormOfValue(
        std::optional<std::string_view> prefix,
        std::optional<std::string_view> suffix,
        int,
        bool) const {
        auto addrStr = regularForm(prefix, suffix);
        if (addrStr == std::nullopt)
            return std::nullopt;
        if (addrStr.value().empty())
            ERROR("Empty address std::string");
        if (addrStr.value().at(0) == '&')
            return addrStr.value().substr(1);
        else
            return "*" + addrStr.value();
    }

    std::optional<std::string> Structure::regularForm(std::optional<std::string_view> prefix,
                                                      std::optional<std::string_view> suffix,
                                                      int,
                                                      bool) const {
        return std::visit(
            [&](auto &&arg) -> std::optional<std::string> {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    return std::nullopt;
                } else if constexpr (std::is_same_v<
                                         T, utils::not_null<std::unique_ptr<const Address>>>) {
                    auto addr = arg->regularForm();
                    if (addr == std::nullopt)
                        return std::nullopt;
                    if (addr.value().length() == 0) {
                        ERROR("Empty regular from.");
                    }
                    if (addr.value()[0] == '&')
                        return std::string{prefix.value_or("")} + addr.value().substr(1) +
                               std::string{suffix.value_or("")};
                    return std::string{prefix.value_or("(")} + "*" + addr.value() +
                           std::string{suffix.value_or(")")};
                }
            },
            getFromAddr());
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> LiteralExpr::simplifiedExpr() const {
        return simplifiedExprIfLinear();
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> BinaryOpExpr::simplifiedExpr() const {
        if (isUnknown())
            return UnknownExpr::makeUnknown().into_underlying();
        if (isLinear())
            return simplifiedExprIfLinear();
        auto LHS = left_->simplifiedExpr();
        auto RHS = right_->simplifiedExpr();
        return std::make_unique<BinaryOpExpr>(std::move(LHS), op_, std::move(RHS));
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> UnaryOpExpr::simplifiedExpr() const {
        if (isUnknown())
            return UnknownExpr::makeUnknown().into_underlying();
        if (isLinear())
            return simplifiedExprIfLinear();
        auto subExpr = expr_->simplifiedExpr();
        return std::make_unique<UnaryOpExpr>(op_, std::move(subExpr));
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> UnknownExpr::simplifiedExpr() const {
        return makeUnknown().into_underlying();
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> SymbolValue::simplifiedExpr() const {
        return simplifiedExprIfLinear();
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> SymbolAddress::simplifiedExpr() const {
        if (isRange())
            ERROR("Address range is solely for address representation and should not be "
                  "used as an expression.");
        return clone();
    }
    utils::not_null<std::unique_ptr<SymbolicExpr>> Structure::simplifiedExpr() const {
        return clone();
    }

    std::unique_ptr<LiteralExpr> LiteralExpr::evalToConstExpr() const {
        switch (type_) {
            case LiteralType::Boolean: return std::make_unique<LiteralExpr>(data_.boolValue);
            case LiteralType::Int: return std::make_unique<LiteralExpr>(data_.intValue);
            case LiteralType::UnsignedInt: return std::make_unique<LiteralExpr>(data_.uintValue);
            case LiteralType::Short: return std::make_unique<LiteralExpr>(data_.shortValue);
            case LiteralType::UnsignedShort:
                return std::make_unique<LiteralExpr>(data_.ushortValue);
            case LiteralType::Int64: return std::make_unique<LiteralExpr>(data_.int64Value);
            case LiteralType::UInt64: return std::make_unique<LiteralExpr>(data_.uint64Value);
        }
        return nullptr;
    }

    std::unique_ptr<LiteralExpr> UnaryOpExpr::evalToConstExpr() const {
        auto C = expr_->evalToConstExpr();
        if (!C)
            return nullptr;

        using Op    = UnaryOpExpr::Operator;
        auto vt     = expr_->getValType();
        unsigned bw = std::max(vt.bitWidth ? vt.bitWidth : 32u, 32u);

        switch (op_) {
            case Op::LogicalNot: {
                bool r = !literalAsBool(*C);
                return std::make_unique<LiteralExpr>(r);
            }
            case Op::BitwiseNot: {
                uint64_t x = coerceU(bw, literalRawU(*C));
                return makeLiteralFromUnifiedType({ScalarKind::UInt, bw}, false, (~x) & maskN(bw));
            }
            case Op::Plus: {
                if (vt.kind == ScalarKind::UInt)
                    return makeLiteralFromUnifiedType({ScalarKind::UInt, bw}, false,
                                                      coerceU(bw, literalRawU(*C)));
                else
                    return makeLiteralFromUnifiedType({ScalarKind::Int, bw}, false,
                                                      (uint64_t)coerceS(bw, literalRawU(*C)));
            }
            case Op::Minus: {
                int64_t s = -coerceS(bw, literalRawU(*C));
                return makeLiteralFromUnifiedType({ScalarKind::Int, bw}, false, (uint64_t)s);
            }
            case Op::PreInc:
            case Op::PreDec:
            case Op::PostInc:
            case Op::PostDec:
            case Op::AddrOf:
            case Op::Dereference:
            default: return nullptr;
        }
    }

    std::unique_ptr<LiteralExpr> BinaryOpExpr::evalToConstExpr() const {
        using BO = BinaryOpExpr::Operator;

        auto Lc = left_->evalToConstExpr();
        if (!Lc)
            return nullptr;

        if (op_ == BO::LogicalAnd) {
            if (!literalAsBool(*Lc))
                return std::make_unique<LiteralExpr>(false);
            auto Rc = right_->evalToConstExpr();
            if (!Rc)
                return nullptr;
            return std::make_unique<LiteralExpr>(literalAsBool(*Rc));
        }
        if (op_ == BO::LogicalOr) {
            if (literalAsBool(*Lc))
                return std::make_unique<LiteralExpr>(true);
            auto Rc = right_->evalToConstExpr();
            if (!Rc)
                return nullptr;
            return std::make_unique<LiteralExpr>(literalAsBool(*Rc));
        }

        auto Rc = right_->evalToConstExpr();
        if (!Rc)
            return nullptr;

        auto tgt = unify(left_->getValType(), right_->getValType());
        if (tgt.kind == ScalarKind::Void)
            return nullptr;
        unsigned bw = tgt.bitWidth ? tgt.bitWidth : 64;

        auto emitBool = [](bool b) { return std::make_unique<LiteralExpr>(b); };

        if (tgt.kind == ScalarKind::UInt) {
            uint64_t L = coerceU(bw, literalRawU(*Lc));
            uint64_t R = coerceU(bw, literalRawU(*Rc));

            switch (op_) {
                case BO::Equal: return emitBool(L == R);
                case BO::NotEqual: return emitBool(L != R);
                case BO::LessThan: return emitBool(L < R);
                case BO::LessEqual: return emitBool(L <= R);
                case BO::GreaterThan: return emitBool(L > R);
                case BO::GreaterEqual: return emitBool(L >= R);

                case BO::Add: return makeLiteralFromUnifiedType(tgt, false, (L + R) & maskN(bw));
                case BO::Subtract:
                    return makeLiteralFromUnifiedType(tgt, false, (L - R) & maskN(bw));
                case BO::Multiply:
                    return makeLiteralFromUnifiedType(tgt, false, (L * R) & maskN(bw));
                case BO::Divide:
                    if (R == 0)
                        return nullptr;
                    return makeLiteralFromUnifiedType(tgt, false, L / R);
                case BO::Remainder:
                    if (R == 0)
                        return nullptr;
                    return makeLiteralFromUnifiedType(tgt, false, L % R);

                case BO::ShiftLeft:
                    if (R >= 64)
                        return nullptr;
                    return makeLiteralFromUnifiedType(tgt, false, (L << (unsigned)R) & maskN(bw));
                case BO::ShiftRight:
                    if (R >= 64)
                        return nullptr;
                    return makeLiteralFromUnifiedType(tgt, false, (L >> (unsigned)R));

                case BO::BitAnd: return makeLiteralFromUnifiedType(tgt, false, L & R);
                case BO::BitOr: return makeLiteralFromUnifiedType(tgt, false, L | R);
                case BO::BitXor: return makeLiteralFromUnifiedType(tgt, false, L ^ R);

                default: return nullptr;
            }
        } else {
            int64_t L = coerceS(bw, literalRawU(*Lc));
            int64_t R = coerceS(bw, literalRawU(*Rc));

            switch (op_) {
                case BO::Equal: return emitBool(L == R);
                case BO::NotEqual: return emitBool(L != R);
                case BO::LessThan: return emitBool(L < R);
                case BO::LessEqual: return emitBool(L <= R);
                case BO::GreaterThan: return emitBool(L > R);
                case BO::GreaterEqual: return emitBool(L >= R);

                case BO::Add: return makeLiteralFromUnifiedType(tgt, false, (uint64_t)(L + R));
                case BO::Subtract: return makeLiteralFromUnifiedType(tgt, false, (uint64_t)(L - R));
                case BO::Multiply: return makeLiteralFromUnifiedType(tgt, false, (uint64_t)(L * R));
                case BO::Divide:
                    if (R == 0)
                        return nullptr;
                    return makeLiteralFromUnifiedType(tgt, false, (uint64_t)(L / R));
                case BO::Remainder:
                    if (R == 0)
                        return nullptr;
                    return makeLiteralFromUnifiedType(tgt, false, (uint64_t)(L % R));

                case BO::ShiftLeft:
                    if ((uint64_t)R >= 64)
                        return nullptr;
                    return makeLiteralFromUnifiedType(tgt, false, (uint64_t(L) << (unsigned)R));
                case BO::ShiftRight:
                    if ((uint64_t)R >= 64)
                        return nullptr;
                    return makeLiteralFromUnifiedType(tgt, false, (uint64_t)(L >> (unsigned)R));

                case BO::BitAnd:
                    return makeLiteralFromUnifiedType(tgt, false, uint64_t(L) & uint64_t(R));
                case BO::BitOr:
                    return makeLiteralFromUnifiedType(tgt, false, uint64_t(L) | uint64_t(R));
                case BO::BitXor:
                    return makeLiteralFromUnifiedType(tgt, false, uint64_t(L) ^ uint64_t(R));

                default: return nullptr;
            }
        }
    }

    bool LiteralExpr::equal(const SymbolicExpr &expr) const {
        const auto liter = llvm::dyn_cast<const LiteralExpr>(&expr);
        if (!liter)
            return false;

        return type_ == liter->type_ && getLiteralValue() == liter->getLiteralValue();
    }

    bool BinaryOpExpr::equal(const SymbolicExpr &expr) const {
        const auto binary = llvm::dyn_cast<const BinaryOpExpr>(&expr);
        if (!binary)
            return false;

        return *left_ == *(binary->left_) && op_ == binary->op_ && *right_ == *(binary->right_);
    }

    bool UnaryOpExpr::equal(const SymbolicExpr &expr) const {
        const auto unary = llvm::dyn_cast<const UnaryOpExpr>(&expr);
        if (!unary)
            return false;

        return op_ == unary->op_ && *expr_ == *(unary->expr_);
    }

    bool UnknownExpr::equal(const SymbolicExpr &expr) const { return expr.isUnknown(); }

    bool SymbolValue::equal(const SymbolicExpr &expr) const {
        const auto symbolValue = llvm::dyn_cast<const SymbolValue>(&expr);
        if (!symbolValue)
            return false;

        if (fromPoint_ != symbolValue->fromPoint_)
            return false;

        return *fromAddr_ == *symbolValue->fromAddr_;
    }

    bool SymbolAddress::equal(const SymbolicExpr &expr) const {
        auto other = llvm::dyn_cast<const SymbolAddress>(&expr);
        if (!other)
            return false;

        bool flag = false;
        // compare base
        std::visit(
            [&](auto &&arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    // check here too.
                    flag = std::holds_alternative<std::monostate>(other->fromAddr_);
                } else if constexpr (std::is_same_v<
                                         T, utils::not_null<std::unique_ptr<const Address>>>) {
                    if (auto ptrAddr = std::get_if<utils::not_null<std::unique_ptr<const Address>>>(
                            &other->fromAddr_);
                        ptrAddr != nullptr && *arg == **ptrAddr) {
                        flag = true;
                    }
                }
            },
            fromAddr_);
        if (!flag) {
            return false;
        };

        if (fromPoint_ != other->fromPoint_)
            return false;

        // compare offset
        // todo: Need a `offsetEqual`, here is not correct now.
        if (*offset_->simplifiedExpr() != *other->getOffset()->simplifiedExpr()) {
            return false;
        }

        // compare range
        if (range_ != std::nullopt && other->range_ != std::nullopt) {
            if (*range_.value().len_ != *(other->range_.value().len_))
                return false;
        } else if ((range_ == std::nullopt) ^ (other->range_ == std::nullopt)) {
            return false;
        }
        return true;
    }

    bool VariableAddress::equal(const SymbolicExpr &expr) const {
        auto other = llvm::dyn_cast<const VariableAddress>(&expr);
        if (!other)
            return false;

        return from_ == other->from_;
    }

    bool FieldAddress::equal(const SymbolicExpr &expr) const {
        auto other = llvm::dyn_cast<const FieldAddress>(&expr);
        if (!other)
            return false;

        auto &[addrStr, index]           = from_;
        auto &[otherAddrStr, otherIndex] = other->from_;
        return *addrStr == *otherAddrStr && index == otherIndex;
    }

    std::optional<utils::not_null<const clang::VarDecl *>> SymbolAddress::getFromRoot() const {
        return std::visit(
            [](auto &&arg) -> std::optional<utils::not_null<const clang::VarDecl *>> {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    return std::nullopt;
                } else if constexpr (std::is_same_v<
                                         T, utils::not_null<std::unique_ptr<const Address>>>) {
                    return arg->getFromRoot();
                }
            },
            fromAddr_);
    }

    std::optional<utils::not_null<const clang::VarDecl *>> SymbolAddress::BaseInfo::getFromRoot()
        const {
        return std::visit(
            [](auto &&arg) -> std::optional<utils::not_null<const clang::VarDecl *>> {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    return std::nullopt;
                } else if constexpr (std::is_same_v<
                                         T, utils::not_null<std::unique_ptr<const Address>>>) {
                    return arg->getFromRoot();
                }
            },
            from_);
    }

    std::optional<utils::not_null<const clang::VarDecl *>> VariableAddress::getFromRoot() const {
        return from_;
    }

    std::optional<utils::not_null<const clang::VarDecl *>> FieldAddress::getFromRoot() const {
        return from_.first->getFromRoot();
    }

    std::optional<utils::not_null<const clang::VarDecl *>> SymbolValue::getFromRoot() const {
        return fromAddr_->getFromRoot();
    }

    bool Structure::Info::equal(const Structure::Info &other) const {
        if (definition_ != other.definition_)
            return false;
        return true;
    }

    bool Structure::Info::operator==(const Info &other) const { return equal(other); }

    bool Structure::equal(const SymbolicExpr &expr) const {
        const auto st = llvm::dyn_cast<const Structure>(&expr);
        if (!st)
            return false;
        if (!info_.equal(st->info_))
            return false;
        return std::ranges::equal(fields_, st->fields_,
                                  [](auto &lhs, auto &rhs) { return *lhs == *rhs; });
    }

    std::optional<utils::not_null<std::unique_ptr<SymbolAddress>>> BinaryOpExpr::
        doTryEvalAsSymbolAddr() const {
        auto lhs = callTryEvalAsAddr(*left_), rhs = callTryEvalAsAddr(*right_);
        if (lhs && rhs)
            return std::nullopt;
        if (lhs == std::nullopt && rhs == std::nullopt)
            return std::nullopt;

        std::unique_ptr<SymbolAddress> addr;
        if (lhs) {
            addr = std::move(lhs).value().into_underlying();
            if (!isValidOffsetOrLength(*right_))
                return std::nullopt;
            auto expr = right_->clone();
            switch (op_) {
                using enum Operator;
                case Add: addr->addOffset(std::move(expr)); break;
                case Subtract: addr->subOffset(std::move(expr)); break;

                default: return std::nullopt;
            }
        } else {
            addr = std::move(rhs).value().into_underlying();
            if (!isValidOffsetOrLength(*left_))
                return std::nullopt;
            auto expr = left_->clone();
            switch (op_) {
                using enum Operator;
                case Add: addr->addOffset(std::move(expr)); break;
                case Subtract: return std::nullopt;
                default: return std::nullopt;
            }
        }
        return addr;
    }

    std::optional<utils::not_null<std::unique_ptr<SymbolAddress>>> SymbolAddress::
        doTryEvalAsSymbolAddr() const {
        auto result = std::make_unique<SymbolAddress>(*this);
        return result;
    }

    SymbolicExpr::UsedMap SymbolValue::collectUsedVarsAndAddrs() const { return {{hash(), this}}; }

    SymbolicExpr::UsedMap BinaryOpExpr::collectUsedVarsAndAddrs() const {
        auto lmap = left_->collectUsedVarsAndAddrs();
        auto rmap = right_->collectUsedVarsAndAddrs();
        lmap.insert(make_move_iterator(rmap.begin()), make_move_iterator(rmap.end()));
        return lmap;
    }

    SymbolicExpr::UsedMap UnaryOpExpr::collectUsedVarsAndAddrs() const {
        return expr_->collectUsedVarsAndAddrs();
    }

    SymbolicExpr::UsedMap SymbolAddress::collectUsedVarsAndAddrs() const {
        if (isRange())
            ERROR("Address range is solely for address representation and should not be "
                  "used as an expression.");
        return {{hash(), this}};
    }

    SymbolAddress::SymbolAddress(const SymbolAddress &other)
        : Address(other), Symbol(other), offset_(other.offset_->clone().into_underlying()),
          fromPoint_(other.fromPoint_), range_(other.range_) {
        std::visit(
            [this](auto &&arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    fromAddr_ = std::monostate{};
                } else if constexpr (std::is_same_v<
                                         T, utils::not_null<std::unique_ptr<const Address>>>) {
                    fromAddr_.emplace<1>(arg->addressClone().into_underlying());
                }
            },
            other.fromAddr_);
    }

    SymbolAddress::BaseInfo::BaseInfo(const SymbolAddress::BaseInfo &other)
        : fromPoint_(other.fromPoint_) {
        std::visit(
            [this](auto &&arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    from_ = std::monostate{};
                } else if constexpr (std::is_same_v<
                                         T, utils::not_null<std::unique_ptr<const Address>>>) {
                    from_.emplace<1>(arg->addressClone().into_underlying());
                }
            },
            other.from_);
    }

    SymbolAddress::SymbolAddress(
        std::variant<std::monostate, utils::not_null<std::unique_ptr<const Address>>> from,
        SourcePoint fromPoint,
        std::optional<utils::not_null<std::unique_ptr<const SymbolicExpr>>> offset,
        std::optional<utils::not_null<std::unique_ptr<const SymbolicExpr>>> length)
        : Address(SymbolicExpr::ExprType::SymbolAddr,
                  SymbolicExpr::Type{SymbolicExpr::ScalarKind::UInt, 64},
                  T_Symbol),
          offset_(std::make_unique<LiteralExpr>(ZERO_OFFSET)), fromAddr_(std::move(from)),
          fromPoint_(fromPoint) {
        if (offset != std::nullopt)
            offset_ = std::move(offset.value());
        if (length) {
            // Address Range.
            range_ = Range{std::move(length.value())};
        }
    }

    void SymbolAddress::setOffset(utils::not_null<std::unique_ptr<SymbolicExpr>> offset) {
        if (!isValidOffsetOrLength(*offset))
            ERROR("Invalid offset.");
        offset_ = std::move(offset).into_underlying();
    }

    void SymbolAddress::addOffset(utils::not_null<std::unique_ptr<SymbolicExpr>> extra) {
        if (!isValidOffsetOrLength(*extra))
            ERROR("Invalid offset.");
        offset_ = std::make_unique<BinaryOpExpr>(offset_->clone().into_underlying(),
                                                 BinaryOpExpr::Operator::Add, std::move(extra))
                      ->simplifiedExpr()
                      .into_underlying();
    }

    void SymbolAddress::subOffset(utils::not_null<std::unique_ptr<SymbolicExpr>> extra) {
        if (!isValidOffsetOrLength(*extra))
            ERROR("Invalid offset.");
        offset_ = std::make_unique<BinaryOpExpr>(offset_->clone(), BinaryOpExpr::Operator::Subtract,
                                                 std::move(extra))
                      ->simplifiedExpr()
                      .into_underlying();
    }

    void SymbolAddress::setLength(utils::not_null<std::unique_ptr<SymbolicExpr>> len) {
        if (!isValidOffsetOrLength(*len))
            ERROR("Invalid Length.");
        if (range_ == std::nullopt) {
            range_.emplace(std::move(len).into_underlying());
            return;
        }
        range_.value().len_ = std::move(len).into_underlying();
    }

    void SymbolAddress::addLength(utils::not_null<std::unique_ptr<SymbolicExpr>> extra) {
        if (!isValidOffsetOrLength(*extra))
            ERROR("Invalid offset.");
        if (range_ == std::nullopt) {
            range_.emplace(std::make_unique<BinaryOpExpr>(std::make_unique<LiteralExpr>(1),
                                                          BinaryOpExpr::Operator::Add,
                                                          std::move(extra))
                               ->simplifiedExpr()
                               .into_underlying());
            return;
        }
        range_.value().len_ =
            std::make_unique<BinaryOpExpr>(range_.value().len_->clone().into_underlying(),
                                           BinaryOpExpr::Operator::Add, std::move(extra))
                ->simplifiedExpr()
                .into_underlying();
    }

    size_t SymbolAddress::BaseInfo::hash() const {
        auto seed = utils::hash_val(fromPoint_.hash());
        return std::visit(
            [&](auto &&arg) -> size_t {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    return seed;
                } else if constexpr (std::is_same_v<
                                         T, utils::not_null<std::unique_ptr<const Address>>>) {
                    return utils::hash_val(seed, arg->hash());
                }
            },
            from_);
    }

    SymbolAddress::BaseInfo SymbolAddress::getBaseInfo() const {
        return std::visit(
            [this](auto &&arg) -> BaseInfo {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    return BaseInfo{std::monostate{}, fromPoint_};
                } else if constexpr (std::is_same_v<
                                         T, utils::not_null<std::unique_ptr<const Address>>>) {
                    return BaseInfo{arg->addressClone().into_underlying(), fromPoint_};
                }
            },
            fromAddr_);
    }

    int SymbolAddress::getDimension() const {
        return std::visit(
            [](auto &&arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    return -1;
                } else if constexpr (std::is_same_v<
                                         T, utils::not_null<std::unique_ptr<const Address>>>) {
                    if (auto dim = arg->getDimension(); dim >= 0)
                        return dim + 1;
                    return -1;
                }
            },
            fromAddr_);
    }

    int VariableAddress::getDimension() const { return 0; }

    int FieldAddress::getDimension() const { return from_.first->getDimension(); }

    VariableAddress::VariableAddress(const VariableAddress &other)
        : Address(other), from_(other.from_) {}

    VariableAddress &VariableAddress::operator=(const VariableAddress &other) {
        if (this == &other)
            return *this;
        Address::operator=(other);
        from_ = other.from_;
        return *this;
    }

    FieldAddress::FieldAddress(const FieldAddress &other)
        : Address(other), definition_(other.definition_),
          from_(std::pair<utils::not_null<std::unique_ptr<const Address>>, size_t>{
              other.from_.first->addressClone().into_underlying(), other.from_.second}) {}

    FieldAddress &FieldAddress::operator=(const FieldAddress &other) {
        if (this == &other)
            return *this;
        Address::operator=(other);
        definition_ = other.definition_;
        from_       = std::pair<utils::not_null<std::unique_ptr<const Address>>, size_t>{
            other.from_.first->addressClone().into_underlying(), other.from_.second};
        return *this;
    }

    void Structure::setFieldValue(size_t index,
                                  utils::not_null<std::unique_ptr<SymbolicExpr>> expr) {
        if (index >= fields_.size())
            ERROR("Out-of-bounds access");
        fields_[index] = std::move(expr);
    }

    std::optional<std::string> Structure::regularFormOfField(size_t index,
                                                             std::optional<std::string_view> prefix,
                                                             std::optional<std::string_view> suffix,
                                                             int,
                                                             bool) const {
        if (index >= fields_.size())
            ERROR("Out-of-bounds access");

        return fields_[index]->regularForm(prefix, suffix);
    }

    Structure::Structure(const clang::RecordDecl *RD,
                         const clang::ASTRecordLayout &layout,
                         utils::not_null<std::unique_ptr<const Address>> from,
                         SourcePoint fromPoint)
        : SymbolicExpr(
              ExprType::Structure,
              Type{ScalarKind::Structure, static_cast<unsigned>(layout.getSize().getQuantity()) *
                                              8 /*By default, char is 8-bit.*/},
              T_Symbol),
          info_(Info{RD, layout}) {
        fields_.reserve(info_.layout_.getFieldCount());
        for (auto field : info_.definition_->fields()) {
            auto index     = field->getFieldIndex();
            auto fieldAddr = std::make_unique<FieldAddress>(
                info_.definition_,
                std::pair<utils::not_null<std::unique_ptr<const Address>>, size_t>{
                    from->addressClone().into_underlying(), index});

            clang::QualType fty = field->getType();

            if (fty->isStructureType()) {
                auto nestedRD = fty->getAsRecordDecl();
                if (!nestedRD || !nestedRD->isCompleteDefinition())
                    ERROR("Incomplete nested struct definition");
                nestedRD           = nestedRD->getDefinition();
                auto &nestedLayout = nestedRD->getASTContext().getASTRecordLayout(nestedRD);
                auto nested        = std::make_unique<Structure>(nestedRD, nestedLayout,
                                                                 std::move(fieldAddr), fromPoint);
                fields_.emplace_back(std::move(nested));
            } else if (fty->isPointerType()) {
                auto addr = std::make_unique<SymbolAddress>(std::move(fieldAddr), fromPoint);
                fields_.emplace_back(std::move(addr));
            } else if (fty->isArrayType()) {
                TODO();
            } else {
                auto vty = deriveVarType(fty);
                auto symbolValue =
                    std::make_unique<SymbolValue>(vty, std::move(fieldAddr), fromPoint);
                fields_.emplace_back(std::move(symbolValue));
            }
        }
        if (fields_.size() != info_.layout_.getFieldCount())
            UNREACHABLE();
    }

    SymbolValue::SymbolValue(const SymbolValue &other)
        : SymbolicExpr(other), fromAddr_(other.fromAddr_->addressClone().into_underlying()),
          fromPoint_(other.fromPoint_) {}

    std::ostream &operator<<(std::ostream &os, SymbolicExpr::ExprType t) {
        switch (t) {
            using enum SymbolicExpr::ExprType;
            case Literal: os << "Literal"; break;
            case SymbolValue: os << "SymbolValue"; break;
            case SymbolAddr: os << "SymbolAddr"; break;
            case VariableAddr: os << "VariableAddr"; break;
            case FieldAddr: os << "FieldAddr"; break;
            case BinaryOp: os << "BinaryOp"; break;
            case UnaryOp: os << "UnaryOp"; break;
            case Structure: os << "Structure"; break;
            case Unknown: os << "Unknown"; break;
            default: UNREACHABLE();
        }
        return os;
    }

    Structure::From Structure::getFrom() const {
        // This structure has a fixed 'from' only if every member is from the same `FieldAddress`
        // **and** same `SourcePoint`.
        std::optional<utils::not_null<std::unique_ptr<const Address>>> commonBaseAddr{};
        std::optional<SourcePoint> commonBasePoint{};
        for (size_t index = 0; index < fields_.size(); ++index) {
            auto &field = fields_.at(index);
            auto symbol = llvm::dyn_cast<const Symbol>(field.get().get());
            if (symbol == nullptr)
                return From{std::monostate{}, std::nullopt};
            auto fromAddr = symbol->getFromAddr();
            std::optional<utils::not_null<std::unique_ptr<const Address>>> baseAddr{};
            std::visit(
                [&](auto &&arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, std::monostate>) {
                        return;
                    } else if constexpr (std::is_same_v<
                                             T, utils::not_null<std::unique_ptr<const Address>>>) {
                        auto fieldAddr = llvm::dyn_cast<const FieldAddress>(arg.get().get());
                        if (fieldAddr == nullptr)
                            return;

                        auto &[base, fieldId] = fieldAddr->getFrom();
                        if (fieldId != index)
                            return;
                        baseAddr.emplace(base->addressClone().into_underlying());
                    }
                },
                fromAddr);
            if (baseAddr == std::nullopt)
                return From{std::monostate{}, std::nullopt};
            if (commonBaseAddr == std::nullopt)
                commonBaseAddr = std::move(baseAddr);

            auto fromPoint = symbol->getFromPoint();
            if (fromPoint == std::nullopt)
                return From{std::monostate{}, std::nullopt};
            if (commonBasePoint == std::nullopt)
                commonBasePoint.emplace(fromPoint.value());

            if (*commonBaseAddr.value() != *baseAddr.value() ||
                commonBasePoint.value() != fromPoint.value())
                return From{std::monostate{}, std::nullopt};
        }
        if (commonBaseAddr == std::nullopt || commonBasePoint == std::nullopt)
            UNREACHABLE();

        return From{std::move(commonBaseAddr).value(), std::move(commonBasePoint).value()};
    }

    std::variant<std::monostate, utils::not_null<std::unique_ptr<const Address>>> Structure::
        getFromAddr() const {
        return getFrom().first;
    }

    std::optional<SourcePoint> Structure::getFromPoint() const { return getFrom().second; }

    SourcePoint &SourcePoint::operator=(const SourcePoint &other) {
        if (this == &other)
            return *this;
        if (&SM_ != &other.SM_)
            ERROR("SourcePoint from different clang::SourceManager.");
        loc_ = other.loc_;
        return *this;
    }

    SourcePoint &SourcePoint::operator=(SourcePoint &&other) {
        if (this == &other)
            return *this;
        if (&SM_ != &other.SM_)
            ERROR("SourcePoint from different clang::SourceManager.");
        loc_ = std::move(other.loc_);
        return *this;
    }

    SourcePoint SourcePoint::fromFuncDeclBefore(const clang::FunctionDecl *FD,
                                                const clang::SourceManager &SM,
                                                const clang::LangOptions &LO) {
        SourcePoint p{SM};
        if (FD) {
            auto BL = FD->getBeginLoc();
            if (BL.isInvalid())
                ERROR("Location before clang::FunctionDecl: {" +
                      clang::Lexer::getSourceText(
                          clang::CharSourceRange::getTokenRange(FD->getSourceRange()), SM, LO)
                          .str() +
                      "} is invalid.");
            p.loc_ = SM.getExpansionLoc(BL);
        } else {
            ERROR("S is nullptr.");
        }
        return p;
    }

    SourcePoint SourcePoint::fromStmtBefore(const clang::Stmt *S,
                                            const clang::SourceManager &SM,
                                            const clang::LangOptions &LO) {
        SourcePoint p{SM};
        if (S) {
            auto BL = S->getBeginLoc();
            if (BL.isInvalid())
                ERROR("Location before clang::Stmt: {" +
                      clang::Lexer::getSourceText(
                          clang::CharSourceRange::getTokenRange(S->getSourceRange()), SM, LO)
                          .str() +
                      "} is invalid.");
            p.loc_ = SM.getExpansionLoc(BL);
        } else {
            ERROR("S is nullptr.");
        }
        return p;
    }

    SourcePoint SourcePoint::fromStmtAfter(const clang::Stmt *S,
                                           const clang::SourceManager &SM,
                                           const clang::LangOptions &LO) {
        SourcePoint p{SM};
        if (S) {
            auto EL = S->getEndLoc();

            auto AL = clang::Lexer::getLocForEndOfToken(EL, /*Offset*/ 0, SM, LO);
            if (AL.isInvalid())
                ERROR("Location after clang::Stmt: {" +
                      clang::Lexer::getSourceText(
                          clang::CharSourceRange::getTokenRange(S->getSourceRange()), SM, LO)
                          .str() +
                      "} is invalid.");
            p.loc_ = SM.getExpansionLoc(AL);
        } else {
            ERROR("S is nullptr.");
        }
        return p;
    }

    bool SourcePoint::operator<(const SourcePoint &other) const {
        if (&SM_ != &other.SM_)
            ERROR("SourcePoint from different clang::SourceManager.");
        if (loc_.isInvalid() || other.loc_.isInvalid())
            UNREACHABLE();
        return SM_.isBeforeInTranslationUnit(loc_, other.loc_);
    }

    bool SourcePoint::operator==(const SourcePoint &other) const {
        if (&SM_ != &other.SM_) {
            // It's an error now.
            ERROR("SourcePoint from different clang::SourceManager.");
            // WARN("SourcePoint from different clang::SourceManager.");
            // return false;
        }
        if (loc_.isInvalid() || other.loc_.isInvalid())
            UNREACHABLE();
        return !SM_.isBeforeInTranslationUnit(loc_, other.loc_) &&
               !SM_.isBeforeInTranslationUnit(other.loc_, loc_);
    }

    std::unique_ptr<SymbolicExpr> createLNotExpr(
        utils::not_null<std::unique_ptr<SymbolicExpr>> expr) {
        return std::make_unique<UnaryOpExpr>(UnaryOpExpr::Operator::LogicalNot, std::move(expr));
    }

    BinaryOpExpr::Operator getCompoundAssignOp(clang::BinaryOperatorKind compoundAssignOp) {
        switch (compoundAssignOp) {
            using enum clang::BinaryOperatorKind;
            using enum BinaryOpExpr::Operator;
            case BO_MulAssign: return Multiply;
            case BO_DivAssign: return Divide;
            case BO_RemAssign: return Remainder;
            case BO_AddAssign: return Add;
            case BO_SubAssign: return Subtract;
            case BO_ShlAssign: return ShiftLeft;
            case BO_ShrAssign: return ShiftRight;
            case BO_AndAssign: return BitAnd;
            case BO_XorAssign: return BitXor;
            case BO_OrAssign: return BitOr;
            default: UNREACHABLE();
        }
    }

    // No AssignOp Here.
    BinaryOpExpr::Operator getBinaryOp(clang::BinaryOperatorKind op) {
        switch (op) {
            using enum clang::BinaryOperatorKind;
            case BO_Mul: return BinaryOpExpr::Operator::Multiply;
            case BO_Div: return BinaryOpExpr::Operator::Divide;
            case BO_Rem: return BinaryOpExpr::Operator::Remainder;
            case BO_Add: return BinaryOpExpr::Operator::Add;
            case BO_Sub: return BinaryOpExpr::Operator::Subtract;
            case BO_Shl: return BinaryOpExpr::Operator::ShiftLeft;
            case BO_Shr: return BinaryOpExpr::Operator::ShiftRight;
            case BO_LT: return BinaryOpExpr::Operator::LessThan;
            case BO_GT: return BinaryOpExpr::Operator::GreaterThan;
            case BO_LE: return BinaryOpExpr::Operator::LessEqual;
            case BO_GE: return BinaryOpExpr::Operator::GreaterEqual;
            case BO_EQ: return BinaryOpExpr::Operator::Equal;
            case BO_NE: return BinaryOpExpr::Operator::NotEqual;
            case BO_And: return BinaryOpExpr::Operator::BitAnd;
            case BO_Xor: return BinaryOpExpr::Operator::BitXor;
            case BO_Or: return BinaryOpExpr::Operator::BitOr;
            case BO_LAnd: return BinaryOpExpr::Operator::LogicalAnd;
            case BO_LOr: return BinaryOpExpr::Operator::LogicalOr;
            case BO_Assign:
            case BO_AddAssign:
            case BO_SubAssign:
            case BO_MulAssign:
            case BO_DivAssign:
            case BO_RemAssign:
            case BO_ShlAssign:
            case BO_ShrAssign:
            case BO_AndAssign:
            case BO_XorAssign:
            case BO_OrAssign: UNREACHABLE();
            default:
                UNIMPLEMENT("Unsupported binary operator: " << op);
                return BinaryOpExpr::Operator::Add;
        }
    }

    SymbolicExpr::Type deriveVarType(clang::QualType type) {
        if (auto ptr = type->getAs<clang::PointerType>())
            return deriveVarType(ptr->getPointeeType());
        return llvm::TypeSwitch<clang::QualType, SymbolicExpr::Type>(type.getCanonicalType())
            .Case([](const clang::BuiltinType *BT) -> SymbolicExpr::Type {
                using Kind = SymbolicExpr::ScalarKind;
                using enum clang::BuiltinType::Kind;

                switch (BT->getKind()) {
                    case Bool: return {Kind::Bool, 1};
                    case Char_S:
                    case SChar: return {Kind::Int, 8};
                    case Char_U:
                    case UChar: return {Kind::UInt, 8};

                    case Short: return {Kind::Int, 16};
                    case UShort: return {Kind::UInt, 16};

                    case Int: return {Kind::Int, 32};
                    case UInt: return {Kind::UInt, 32};

                    case Long: return {Kind::Int, 64};
                    case ULong: return {Kind::UInt, 64};

                    case LongLong: return {Kind::Int, 64};
                    case ULongLong: return {Kind::UInt, 64};
                    case Void: return {Kind::Void, 0};

                    default:
                        clang::LangOptions langOpts;
                        clang::PrintingPolicy pp(langOpts);
                        UNIMPLEMENT("Unsupported builtin type: " << BT->getName(pp).str());
                }
            })
            .Default([&](clang::QualType QT) -> SymbolicExpr::Type {
                if (QT->isStructureType()) {
                    return {SymbolicExpr::ScalarKind::Structure, 0};
                }
                UNIMPLEMENT("Unsupported non-builtin type: " << QT.getAsString());
            });
    }

    bool isValidOffsetOrLength(const SymbolicExpr &expr) {
        if (expr.isUnknown())
            return true;
        if (expr.tryEvalAsSymbolAddr())
            return false;
        return true;
    }

    bool is_symbol_addr(const Address &a) noexcept { return llvm::isa<SymbolAddress>(a); }

    bool isFrom(const SymbolicExpr &expr, const Address &fromAddr, SourcePoint fromPoint) {
        auto symbol = llvm::dyn_cast<const Symbol>(&expr);
        if (symbol == nullptr)
            return false;

        if (symbol->getFromPoint() == std::nullopt || symbol->getFromPoint().value() != fromPoint)
            return false;
        return std::visit(
            [&](auto &&arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    return false;
                } else if constexpr (std::is_same_v<
                                         T, utils::not_null<std::unique_ptr<const Address>>>) {
                    return fromAddr == *arg;
                }
            },
            symbol->getFromAddr());
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> getSymbol(
        clang::QualType type,
        std::optional<utils::not_null<std::unique_ptr<const Address>>> from,
        SourcePoint fromPoint) {
        if (type->isPointerType()) {
            if (from)
                return std::make_unique<SymbolAddress>(std::move(from.value()),
                                                       std::move(fromPoint));
            return std::make_unique<SymbolAddress>(std::monostate{}, std::move(fromPoint));
        } else if (type->isArrayType()) {
            TODO();
        } else if (type->isStructureType()) {
            if (from == std::nullopt)
                ERROR("Structure should *from* an `Address`.");
            auto *RD = type->getAsRecordDecl();
            if (!RD || !RD->isCompleteDefinition())
                ERROR("Incomplete struct definition");
            RD           = RD->getDefinition();
            auto &layout = RD->getASTContext().getASTRecordLayout(RD);
            return std::make_unique<Structure>(RD, layout, std::move(from.value()),
                                               std::move(fromPoint));

        } else {
            if (from == std::nullopt)
                ERROR("SymbolValue should *from* an `Address`.");
            SymbolicExpr::Type vty = deriveVarType(type);
            return std::make_unique<SymbolValue>(vty, std::move(from.value()),
                                                 std::move(fromPoint));
        }
    }

    Symbol *Symbol::toThis(SymbolicExpr *e) {
        if (!Symbol::classof(e))
            return nullptr;
        switch (e->getType()) {
            case SymbolicExpr::ExprType::SymbolValue: return static_cast<SymbolValue *>(e);
            case SymbolicExpr::ExprType::Structure: return static_cast<Structure *>(e);
            case SymbolicExpr::ExprType::SymbolAddr: return static_cast<SymbolAddress *>(e);
            default: UNREACHABLE();
        }
    }

    const Symbol *Symbol::toThis(const SymbolicExpr *e) {
        return toThis(const_cast<SymbolicExpr *>(e));
    }

} // namespace acslg::analyzer::symbolic