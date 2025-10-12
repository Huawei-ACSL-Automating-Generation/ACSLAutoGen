#include <memory>
#include <sstream>
#include <cstring>
#include <variant>
#include <ranges>
#include <llvm/ADT/TypeSwitch.h>
#include "expr.h"
#include "macros.h"

using namespace std;
using namespace clang;
using namespace llvm;

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
            unsigned bw = max(a.bitWidth ? a.bitWidth : 1u, b.bitWidth ? b.bitWidth : 1u);
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

        inline unique_ptr<LiteralExpr> makeLiteralFromUnifiedType(Type t,
                                                                  bool asBool,
                                                                  uint64_t raw) {
            if (asBool)
                return make_unique<LiteralExpr>(asBool);

            unsigned bw = t.bitWidth ? t.bitWidth : 64;
            if (t.kind == ScalarKind::UInt) {
                uint64_t u = coerceU(bw, raw);
                if (bw <= 16)
                    return make_unique<LiteralExpr>((unsigned short)u);
                if (bw <= 32)
                    return make_unique<LiteralExpr>((unsigned int)u);
                return make_unique<LiteralExpr>((uint64_t)u);
            } else { // Int
                int64_t s = coerceS(bw, raw);
                if (bw <= 16)
                    return make_unique<LiteralExpr>((short)s);
                if (bw <= 32)
                    return make_unique<LiteralExpr>((int)s);
                return make_unique<LiteralExpr>((int64_t)s);
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

    using namespace utils;
    not_null<unique_ptr<UnknownExpr>> UnknownExpr::makeUnknown() {
        return make_unique<UnknownExpr>();
    }

    not_null<unique_ptr<SymbolicExpr>> SymbolicExpr::simplifiedExprIfLinear() const {
        if (!isLinear())
            return clone();
        auto [hashPtrMap, hashIdMap] = collectUsedVarsAndAddrs(*this);

        auto linearExpr = toLinearExpr(hashIdMap);
        optional<not_null<unique_ptr<SymbolicExpr>>> result{};

        using enum BinaryOpExpr::Operator;
        for (auto [hash, varOrAddr] : hashPtrMap) {
            visit(
                [&](auto &&arg) {
                    auto C = linearExpr
                                 .coefficient(Parma_Polyhedra_Library::Variable{hashIdMap.at(hash)})
                                 .get_si();
                    if (C == 0)
                        return;

                    if (result == nullopt) {
                        if (C == 1)
                            result = arg->clone();
                        else
                            result = make_unique<BinaryOpExpr>(make_unique<LiteralExpr>(C),
                                                               Multiply, arg->clone());
                    } else {
                        unsigned absC = abs(C);
                        unique_ptr<SymbolicExpr> varExpr{nullptr};
                        if (absC != 1)
                            varExpr = make_unique<BinaryOpExpr>(make_unique<LiteralExpr>(absC),
                                                                Multiply, arg->clone());
                        else
                            varExpr = arg->clone().into_underlying();
                        result =
                            make_unique<BinaryOpExpr>(std::move(result.value()),
                                                      (C > 0 ? Add : Subtract), std::move(varExpr));
                    }
                },
                varOrAddr);
        }
        if (auto inhomo = linearExpr.inhomogeneous_term().get_si(); inhomo || result == nullopt) {
            if (result != nullopt) {
                result = make_unique<BinaryOpExpr>(std::move(result.value()),
                                                   (inhomo > 0 ? Add : Subtract),
                                                   make_unique<LiteralExpr>(abs(inhomo)));
            } else
                result = make_unique<LiteralExpr>(inhomo);
        }
        if (result == nullopt) {
            ERROR("Simplified expr is null! Something goes wrong.");
        }
        return std::move(result.value());
    }

    not_null<unique_ptr<SymbolicExpr>> LiteralExpr::clone() const {
        switch (getLiteralType()) {
            case LiteralType::Boolean: return make_unique<LiteralExpr>(data_.boolValue);
            case LiteralType::Int: return make_unique<LiteralExpr>(data_.intValue);
            case LiteralType::UnsignedInt: return make_unique<LiteralExpr>(data_.uintValue);
            case LiteralType::Short: return make_unique<LiteralExpr>(data_.shortValue);
            case LiteralType::UnsignedShort: return make_unique<LiteralExpr>(data_.ushortValue);
            case LiteralType::Int64: return make_unique<LiteralExpr>(data_.int64Value);
            case LiteralType::UInt64: return make_unique<LiteralExpr>(data_.uint64Value);
        }

        UNREACHABLE();
    }

    not_null<unique_ptr<SymbolicExpr>> BinaryOpExpr::clone() const {
        return make_unique<BinaryOpExpr>(left_->clone(), op_, right_->clone());
    }

    not_null<unique_ptr<SymbolicExpr>> UnaryOpExpr::clone() const {
        return make_unique<UnaryOpExpr>(op_, expr_->clone());
    }

    not_null<unique_ptr<SymbolicExpr>> UnknownExpr::clone() const {
        return make_unique<UnknownExpr>();
    }

    not_null<unique_ptr<SymbolicExpr>> Variable::clone() const {
        return make_unique<Variable>(*this);
    }

    not_null<unique_ptr<SymbolicExpr>> SymbolAddress::clone() const {
        return make_unique<SymbolAddress>(*this);
    }

    not_null<unique_ptr<SymbolicExpr>> VariableAddress::clone() const {
        return make_unique<VariableAddress>(*this);
    }

    not_null<unique_ptr<SymbolicExpr>> FieldAddress::clone() const {
        return make_unique<FieldAddress>(*this);
    }

    not_null<unique_ptr<SymbolicExpr>> Structure::clone() const {
        return make_unique<Structure>(*this);
    }

    not_null<unique_ptr<Address>> SymbolAddress::addressClone() const {
        return make_unique<SymbolAddress>(*this);
    }

    not_null<unique_ptr<Address>> VariableAddress::addressClone() const {
        return make_unique<VariableAddress>(*this);
    }

    not_null<unique_ptr<Address>> FieldAddress::addressClone() const {
        return make_unique<FieldAddress>(*this);
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
        size_t seed = hash_val(getType(), type_);

        switch (type_) {
            using enum LiteralType;
            case Boolean: return hash_val(seed, data_.boolValue);
            case Int: return hash_val(seed, data_.intValue);
            case UnsignedInt: return hash_val(seed, data_.uintValue);
            case Short: return hash_val(seed, data_.shortValue);
            case UnsignedShort: return hash_val(seed, data_.ushortValue);
            case Int64: return hash_val(seed, data_.int64Value);
            case UInt64: return hash_val(seed, data_.uint64Value);
            default: ERROR("Wrong type.");
        }
    }

    size_t Variable::hash() const {
        size_t seed = hash_val(getType(), fromPoint_.hash());
        visit(
            [&](auto &&arg) {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    /* do nothing */
                } else if constexpr (is_same_v<T, not_null<unique_ptr<const Address>>>) {
                    seed = hash_val(seed, arg->hash());
                }
            },
            from_);
        return seed;
    }

    size_t UnaryOpExpr::hash() const {
        return hash_val(getType(), static_cast<size_t>(op_), expr_->hash());
    }

    size_t BinaryOpExpr::hash() const {
        return hash_val(getType(), static_cast<size_t>(op_), left_->hash(), right_->hash());
    }

    size_t SymbolAddress::hash() const {
        size_t seed = hash_val(getAddressType(), fromPoint_.hash(), offset_->hash(),
                               range_ ? range_.value().len_->hash() : 0);

        visit(
            [&](auto &&arg) {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    return;
                } else if constexpr (is_same_v<T, not_null<unique_ptr<const Address>>>) {
                    seed = hash_val(seed, arg->hash());
                }
            },
            from_);
        return seed;
    }

    size_t VariableAddress::hash() const {
        size_t seed = hash_val(getAddressType());

        visit(
            [&](auto &&arg) {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    return;
                } else if constexpr (is_same_v<T, not_null<const VarDecl *>>) {
                    seed = hash_val(seed, arg.get());
                }
            },
            from_);
        return seed;
    }

    size_t FieldAddress::hash() const {
        size_t seed = hash_val(getAddressType());

        visit(
            [&](auto &&arg) {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    return;
                } else if constexpr (is_same_v<T, pair<not_null<unique_ptr<const Address>>,
                                                       const size_t>>) {
                    auto &[fromAddr, id] = arg;
                    seed                 = hash_val(seed, fromAddr->hash(), id);
                }
            },
            from_);
        return seed;
    }

    size_t Structure::hash() const {
        auto seed = hash_val(getType(), info_.definition_.get());
        for (auto &field : fields_)
            seed = hash_val(seed, field->hash());
        return seed;
    }

    size_t UnknownExpr::hash() const { return hash_val(getType()); }

    string LiteralExpr::dump() const {
        ostringstream oss;
        switch (getLiteralType()) {
            case LiteralType::Boolean:
                oss << "Boolean(" << (data_.boolValue ? "true" : "false") << ")";
                break;
            case LiteralType::Int: oss << "Int(" << data_.intValue << ")"; break;
            case LiteralType::UnsignedInt: oss << "UnsignedInt(" << data_.uintValue << ")"; break;
            case LiteralType::Short: oss << "Short(" << data_.shortValue << ")"; break;
            case LiteralType::UnsignedShort:
                oss << "UnsignedShort(" << data_.ushortValue << ")";
                break;
            case LiteralType::Int64: oss << "Int64(" << data_.int64Value << ")"; break;
            case LiteralType::UInt64: oss << "Uint64(" << data_.uint64Value << ")"; break;
        }

        return oss.str();
    }

    string BinaryOpExpr::dump() const {
        ostringstream oss;
        string opStr;
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
        oss << "(" << left_->dump() << " " << opStr << " " << right_->dump() << ")";
        return oss.str();
    }

    string UnaryOpExpr::dump() const {
        ostringstream oss;
        string opStr;
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
        oss << opStr << "(" << expr_->dump() << ")";
        return oss.str();
    }

    string UnknownExpr::dump() const { return "{unknown}"; }

    template <class FromVariant>
    static inline void dump_from(ostringstream &oss, const FromVariant &from) {
        visit(overloaded{[&](monostate) { oss << "none"; },
                         [&](not_null<const VarDecl *> d) {
                             const Decl *decl = d.get();
                             if (auto *nd = dyn_cast<NamedDecl>(decl))
                                 oss << "decl:" << decl->getDeclKindName() << " "
                                     << nd->getQualifiedNameAsString();
                             else
                                 oss << "decl:" << decl->getDeclKindName();
                         },
                         [&](const not_null<unique_ptr<const Address>> &p) {
                             const Address *base = p.get().get();
                             oss << "addr:" << (base ? base->dump() : string("<null>"));
                         },
                         [&](const pair<not_null<unique_ptr<const Address>>, const size_t> &s) {
                             oss << "field of:" << s.first.get()->dump() << "[" << s.second << "]";
                         }},
              from);
    }

    string Variable::dump() const {
        ostringstream oss;
        const auto &t = getValType();

        oss << "Var(";
        switch (t.kind) {
            case ScalarKind::Int: oss << "int"; break;
            case ScalarKind::UInt: oss << "uint"; break;
            case ScalarKind::Bool: oss << "bool"; break;
            case ScalarKind::Void: oss << "void"; break;
            case ScalarKind::Structure: ERROR("Variable's ScalarKind should not be Structure");
        }
        oss << t.bitWidth << ")";

        oss << "{from=";
        dump_from(oss, from_);
        oss << "}, ";

        oss << "{from point=";
        oss << fromPoint_.dump();
        oss << "}";
        return oss.str();
    }

    string SymbolAddress::dump() const {
        ostringstream oss;
        oss << "SymbolAddress";
        auto off = getOffset();
        if (range_ == nullopt)
            oss << "[" << off->dump() << "]";
        else
            oss << "[" << off->dump() << "..." << range_.value().len_->dump() << "]";
        oss << "{from=";
        dump_from(oss, from_);
        oss << "}, ";
        oss << "{from point=";
        oss << fromPoint_.dump();
        oss << "}";
        return oss.str();
    }

    string VariableAddress::dump() const {
        ostringstream oss;
        oss << "VariableAddress";
        oss << "{from=";
        dump_from(oss, from_);
        oss << "}";
        return oss.str();
    }

    string FieldAddress::dump() const {
        ostringstream oss;
        oss << "FieldAddress";
        oss << "{from=";
        dump_from(oss, from_);
        oss << "}";
        return oss.str();
    }

    string Structure::Info::dump() const {
        ostringstream oss;
        string structName = definition_->getNameAsString();
        uint64_t sizeBits = static_cast<uint64_t>(layout_.getSize().getQuantity()) * 8;

        oss << "Struct(" << structName << ", size=" << sizeBits << " bits";
        oss << ")";

        return oss.str();
    }

    string Structure::dump() const {
        ostringstream oss;

        oss << info_.dump();
        oss << ", fields=[";
        for (size_t i = 0; i < fields_.size(); ++i) {
            oss << fields_[i]->dump();
            if (i + 1 < fields_.size()) {
                oss << ", ";
            }
        }
        oss << "]";

        return oss.str();
    }

    optional<string> LiteralExpr::regularForm(optional<string_view>,
                                              optional<string_view>,
                                              int,
                                              bool) const {
        ostringstream oss;
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

    optional<string> BinaryOpExpr::regularForm(optional<string_view> prefix,
                                               optional<string_view> suffix,
                                               int parentPrec,
                                               bool isRightChild) const {
        ostringstream oss;
        string opStr;
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
        if (leftStr == nullopt)
            return nullopt;
        auto rightStr = right_->regularForm(prefix, suffix, myPrec, true);
        if (rightStr == nullopt)
            return nullopt;
        oss << (needParens ? "(" : "") << leftStr.value() << " " << opStr << " " << rightStr.value()
            << (needParens ? ")" : "");
        return oss.str();
    }

    optional<string> UnaryOpExpr::regularForm(optional<string_view> prefix,
                                              optional<string_view> suffix,
                                              int parentPrec,
                                              bool) const {
        ostringstream oss;
        string opStr;
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
            if (subStr == nullopt)
                return nullopt;
            oss << (needParens ? "(" : "") << subStr.value() << opStr << (needParens ? ")" : "");
        } else {
            auto subStr = expr_->regularForm(prefix, suffix, myPrec, true);
            if (subStr == nullopt)
                return nullopt;
            oss << (needParens ? "(" : "") << opStr << subStr.value() << (needParens ? ")" : "");
        }
        return oss.str();
    }

    optional<string> UnknownExpr::regularForm(optional<string_view>,
                                              optional<string_view>,
                                              int,
                                              bool) const {
        WARN("Output UnknownExpr's regular form, something may go wrong.");
        return "{unknown}";
    }

    optional<string> Variable::regularForm(optional<string_view> prefix,
                                           optional<string_view> suffix,
                                           int,
                                           bool) const {
        return visit(
            [&](auto &&arg) -> optional<string> {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    ERROR("Trying to get regular form of Variable with nullptr from_.");
                } else if constexpr (is_same_v<T, not_null<unique_ptr<const Address>>>) {
                    auto addr = arg->regularForm();
                    if (addr == nullopt)
                        return nullopt;
                    if (addr.value().length() == 0) {
                        ERROR("Empty regular from.");
                    }
                    if (addr.value()[0] == '&')
                        return string{prefix.value_or("")} + addr.value().substr(1) +
                               string{suffix.value_or("")};
                    return string{prefix.value_or("(")} + "*" + addr.value() +
                           string{suffix.value_or(")")};
                }
            },
            from_);
    }

    optional<string> SymbolAddress::regularForm(optional<string_view> prefix,
                                                optional<string_view> suffix,
                                                int,
                                                bool) const {
        if (isRange())
            ERROR("Address range has no regularForm but regularFormOfValue.");
        return visit(
            [&, this](auto &&arg) -> optional<string> {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    ERROR("Trying to get regular form of address without from_.");
                } else if constexpr (is_same_v<T, not_null<unique_ptr<const Address>>>) {
                    auto nameStr = arg->regularFormOfValue(prefix, suffix);
                    if (nameStr == nullopt)
                        return nullopt;
                    auto offsetStr = offset_->regularForm(prefix, suffix);
                    if (offsetStr == nullopt)
                        return nullopt;

                    if (offsetStr.value() == "0")
                        offsetStr.value() = "";

                    if (!offsetStr.value().empty())
                        return "(" + string{prefix.value_or("")} + nameStr.value() +
                               string{suffix.value_or("")} + "+" + offsetStr.value() + ")";
                    else
                        return nameStr;
                }
            },
            from_);
    }

    optional<string> VariableAddress::regularForm(optional<string_view> prefix,
                                                  optional<string_view> suffix,
                                                  int,
                                                  bool) const {
        return visit(
            [&](auto &&arg) -> string {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    ERROR("Trying to get regular form of address without from_.");
                } else if constexpr (is_same_v<T, not_null<const VarDecl *>>) {
                    return "&" + (prefix ? (string)*prefix : "") + arg->getNameAsString() +
                           (suffix ? (string)*suffix : "");
                }
            },
            from_);
    }

    optional<string> FieldAddress::regularForm(optional<string_view> prefix,
                                               optional<string_view> suffix,
                                               int,
                                               bool) const {
        return visit(
            [&, this](auto &&arg) -> optional<string> {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    ERROR("Trying to get regular form of address without from_.");
                } else if constexpr (is_same_v<T, pair<not_null<unique_ptr<const Address>>,
                                                       const size_t>>) {
                    auto &[fromAddr, index] = arg;
                    auto baseStr            = fromAddr->regularForm();
                    if (baseStr == nullopt)
                        return nullopt;
                    if (baseStr.value().empty())
                        ERROR("Empty base string");
                    auto fields = definition_->fields();
                    auto it     = ranges::next(fields.begin(), index, fields.end());
                    if (it == fields.end())
                        ERROR("Out-of-bounds access");
                    auto fieldStr = it->getNameAsString();

                    string concatenatedStr;
                    if (baseStr.value().at(0) == '&') {
                        concatenatedStr = baseStr.value().substr(1) + "." + fieldStr;
                    } else {
                        concatenatedStr = baseStr.value() + "->" + fieldStr;
                    }
                    return "&" + string{prefix.value_or("")} + concatenatedStr +
                           string{suffix.value_or("")};
                }
            },
            from_);
    }

    optional<string> SymbolAddress::regularFormOfValue(optional<string_view> prefix,
                                                       optional<string_view> suffix,
                                                       int,
                                                       bool) const {
        if (!isRange()) {
            return visit(
                [&, this](auto &&arg) -> optional<string> {
                    using T = decay_t<decltype(arg)>;
                    if constexpr (is_same_v<T, monostate>) {
                        ERROR("Trying to get regular form of address without from_.");
                    } else if constexpr (is_same_v<T, not_null<unique_ptr<const Address>>>) {
                        auto nameStr = arg->regularFormOfValue(prefix, suffix);
                        if (nameStr == nullopt)
                            return nullopt;
                        auto offsetStr = offset_->regularForm(prefix, suffix);
                        if (offsetStr == nullopt)
                            return nullopt;

                        return nameStr.value() + "[" + offsetStr.value() + "]";
                    }
                },
                from_);
        } else {
            return visit(
                [&, this](auto &&arg) -> optional<string> {
                    using T = decay_t<decltype(arg)>;
                    if constexpr (is_same_v<T, monostate>) {
                        ERROR("Trying to get regular form of address range without from_.");
                    } else if constexpr (is_same_v<T, not_null<unique_ptr<const Address>>>) {
                        auto nameStr = arg->regularFormOfValue(prefix, suffix);
                        if (nameStr == nullopt)
                            return nullopt;
                        auto offsetStr = getOffset()->regularForm(prefix, suffix);
                        if (offsetStr == nullopt)
                            return nullopt;

                        auto rangeStr =
                            make_unique<BinaryOpExpr>(
                                make_unique<BinaryOpExpr>(getOffset()->clone(),
                                                          BinaryOpExpr::Operator::Add,
                                                          range_.value().len_->clone()),
                                BinaryOpExpr::Operator::Subtract, make_unique<LiteralExpr>(1))
                                ->simplifiedExpr()
                                ->regularForm(prefix, suffix);
                        if (rangeStr == nullopt)
                            return nullopt;

                        return nameStr.value() + "[" + offsetStr.value() + ".." + rangeStr.value() +
                               "]";
                    }
                },
                getFromAddr());
        }
    }

    optional<string> VariableAddress::regularFormOfValue(optional<string_view> prefix,
                                                         optional<string_view> suffix,
                                                         int,
                                                         bool) const {
        return visit(
            [&](auto &&arg) -> string {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    ERROR("Trying to get regular form of address range without from_.");
                } else if constexpr (is_same_v<T, not_null<const VarDecl *>>) {
                    return (prefix ? (string)*prefix : "") + arg->getNameAsString() +
                           (suffix ? (string)*suffix : "");
                }
            },
            getFrom());
    }

    optional<string> FieldAddress::regularFormOfValue(optional<string_view> prefix,
                                                      optional<string_view> suffix,
                                                      int,
                                                      bool) const {
        return visit(
            [&, this](auto &&arg) -> optional<string> {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    ERROR("Trying to get regular form of address range without from_.");
                } else if constexpr (is_same_v<T, pair<not_null<unique_ptr<const Address>>,
                                                       const size_t>>) {
                    auto addrStr = regularForm(prefix, suffix);
                    if (addrStr == nullopt)
                        return nullopt;
                    if (addrStr.value().empty())
                        ERROR("Empty address string");
                    if (addrStr.value().at(0) == '&')
                        return addrStr.value().substr(1);
                    else
                        return "*" + addrStr.value();
                }
            },
            getFrom());
    }

    optional<string> Structure::regularForm(optional<string_view> prefix,
                                            optional<string_view> suffix,
                                            int,
                                            bool) const {
        return visit(
            [&](auto &&arg) -> optional<string> {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    return nullopt;
                } else if constexpr (is_same_v<T, not_null<unique_ptr<const Address>>>) {
                    auto addr = arg->regularForm();
                    if (addr == nullopt)
                        return nullopt;
                    if (addr.value().length() == 0) {
                        ERROR("Empty regular from.");
                    }
                    if (addr.value()[0] == '&')
                        return string{prefix.value_or("")} + addr.value().substr(1) +
                               string{suffix.value_or("")};
                    return string{prefix.value_or("(")} + "*" + addr.value() +
                           string{suffix.value_or(")")};
                }
            },
            getFromAddr());
    }

    not_null<unique_ptr<SymbolicExpr>> LiteralExpr::simplifiedExpr() const {
        return simplifiedExprIfLinear();
    }

    not_null<unique_ptr<SymbolicExpr>> BinaryOpExpr::simplifiedExpr() const {
        if (isUnknown())
            return UnknownExpr::makeUnknown().into_underlying();
        if (isLinear())
            return simplifiedExprIfLinear();
        auto LHS = left_->simplifiedExpr();
        auto RHS = right_->simplifiedExpr();
        return make_unique<BinaryOpExpr>(std::move(LHS), op_, std::move(RHS));
    }

    not_null<unique_ptr<SymbolicExpr>> UnaryOpExpr::simplifiedExpr() const {
        if (isUnknown())
            return UnknownExpr::makeUnknown().into_underlying();
        if (isLinear())
            return simplifiedExprIfLinear();
        auto subExpr = expr_->simplifiedExpr();
        return make_unique<UnaryOpExpr>(op_, std::move(subExpr));
    }

    not_null<unique_ptr<SymbolicExpr>> UnknownExpr::simplifiedExpr() const {
        return makeUnknown().into_underlying();
    }

    not_null<unique_ptr<SymbolicExpr>> Variable::simplifiedExpr() const {
        return simplifiedExprIfLinear();
    }

    not_null<unique_ptr<SymbolicExpr>> SymbolAddress::simplifiedExpr() const {
        if (isRange())
            ERROR("Address range is solely for address representation and should not be "
                  "used as an expression.");
        return clone();
    }
    not_null<unique_ptr<SymbolicExpr>> Structure::simplifiedExpr() const { return clone(); }

    unique_ptr<LiteralExpr> LiteralExpr::evalToConstExpr() const {
        switch (type_) {
            case LiteralType::Boolean: return make_unique<LiteralExpr>(data_.boolValue);
            case LiteralType::Int: return make_unique<LiteralExpr>(data_.intValue);
            case LiteralType::UnsignedInt: return make_unique<LiteralExpr>(data_.uintValue);
            case LiteralType::Short: return make_unique<LiteralExpr>(data_.shortValue);
            case LiteralType::UnsignedShort: return make_unique<LiteralExpr>(data_.ushortValue);
            case LiteralType::Int64: return make_unique<LiteralExpr>(data_.int64Value);
            case LiteralType::UInt64: return make_unique<LiteralExpr>(data_.uint64Value);
        }
        return nullptr;
    }

    unique_ptr<LiteralExpr> UnaryOpExpr::evalToConstExpr() const {
        auto C = expr_->evalToConstExpr();
        if (!C)
            return nullptr;

        using Op    = UnaryOpExpr::Operator;
        auto vt     = expr_->getValType();
        unsigned bw = max(vt.bitWidth ? vt.bitWidth : 32u, 32u);

        switch (op_) {
            case Op::LogicalNot: {
                bool r = !literalAsBool(*C);
                return make_unique<LiteralExpr>(r);
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

    unique_ptr<LiteralExpr> BinaryOpExpr::evalToConstExpr() const {
        using BO = BinaryOpExpr::Operator;

        auto Lc = left_->evalToConstExpr();
        if (!Lc)
            return nullptr;

        if (op_ == BO::LogicalAnd) {
            if (!literalAsBool(*Lc))
                return make_unique<LiteralExpr>(false);
            auto Rc = right_->evalToConstExpr();
            if (!Rc)
                return nullptr;
            return make_unique<LiteralExpr>(literalAsBool(*Rc));
        }
        if (op_ == BO::LogicalOr) {
            if (literalAsBool(*Lc))
                return make_unique<LiteralExpr>(true);
            auto Rc = right_->evalToConstExpr();
            if (!Rc)
                return nullptr;
            return make_unique<LiteralExpr>(literalAsBool(*Rc));
        }

        auto Rc = right_->evalToConstExpr();
        if (!Rc)
            return nullptr;

        auto tgt = unify(left_->getValType(), right_->getValType());
        if (tgt.kind == ScalarKind::Void)
            return nullptr;
        unsigned bw = tgt.bitWidth ? tgt.bitWidth : 64;

        auto emitBool = [](bool b) { return make_unique<LiteralExpr>(b); };

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
        const auto liter = dynamic_cast<const LiteralExpr *>(&expr);
        if (!liter)
            return false;

        return type_ == liter->type_ && getLiteralValue() == liter->getLiteralValue();
    }

    bool BinaryOpExpr::equal(const SymbolicExpr &expr) const {
        const auto binary = dynamic_cast<const BinaryOpExpr *>(&expr);
        if (!binary)
            return false;

        return *left_ == *(binary->left_) && op_ == binary->op_ && *right_ == *(binary->right_);
    }

    bool UnaryOpExpr::equal(const SymbolicExpr &expr) const {
        const auto unary = dynamic_cast<const UnaryOpExpr *>(&expr);
        if (!unary)
            return false;

        return op_ == unary->op_ && *expr_ == *(unary->expr_);
    }

    bool UnknownExpr::equal(const SymbolicExpr &) const { return false; }

    bool Variable::equal(const SymbolicExpr &expr) const {
        const auto var = dynamic_cast<const Variable *>(&expr);
        if (!var)
            return false;

        if (fromPoint_ != var->fromPoint_)
            return false;

        return visit(
            [&](auto &&arg) {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    return false;
                } else if constexpr (is_same_v<T, not_null<unique_ptr<const Address>>>) {
                    if (auto addr = get_if<not_null<unique_ptr<const Address>>>(&var->from_))
                        return *arg == **addr;
                    else
                        return false;
                }
            },
            from_);
    }

    bool SymbolAddress::equal(const SymbolicExpr &expr) const {
        auto other = dynamic_cast<const SymbolAddress *>(&expr);
        if (!other)
            return false;

        bool flag = false;
        // compare base
        visit(
            [&](auto &&arg) {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    TODO();
                } else if constexpr (is_same_v<T, not_null<unique_ptr<const Address>>>) {
                    if (auto ptrAddr = get_if<not_null<unique_ptr<const Address>>>(&other->from_);
                        ptrAddr != nullptr && *arg == **ptrAddr) {
                        flag = true;
                    }
                }
            },
            from_);
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
        if (range_ != nullopt && other->range_ != nullopt) {
            if (*range_.value().len_ != *(other->range_.value().len_))
                return false;
        } else if ((range_ == nullopt) ^ (other->range_ == nullopt)) {
            return false;
        }
        return true;
    }

    bool VariableAddress::equal(const SymbolicExpr &expr) const {
        auto other = dynamic_cast<const VariableAddress *>(&expr);
        if (!other)
            return false;

        return visit(
            [&](auto &&arg) -> bool {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    TODO();
                } else if constexpr (is_same_v<T, not_null<const VarDecl *>>) {
                    if (auto varDeclPtr = get_if<not_null<const VarDecl *>>(&other->from_);
                        varDeclPtr != nullptr && arg == *varDeclPtr) {
                        return true;
                    }
                    return false;
                }
            },
            from_);
    }

    bool FieldAddress::equal(const SymbolicExpr &expr) const {
        auto other = dynamic_cast<const FieldAddress *>(&expr);
        if (!other)
            return false;

        return visit(
            [&](auto &&arg) -> bool {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    TODO();
                } else if constexpr (is_same_v<T, pair<not_null<unique_ptr<const Address>>,
                                                       const size_t>>) {
                    auto &[addrStr, index] = arg;
                    if (auto pairPtr =
                            get_if<pair<not_null<unique_ptr<const Address>>, const size_t>>(
                                &other->from_)) {
                        auto &[otherAddrStr, otherIndex] = *pairPtr;
                        if (*addrStr == *otherAddrStr && index == otherIndex)
                            return true;
                    }
                    return false;
                }
            },
            from_);
    }

    optional<not_null<const VarDecl *>> SymbolAddress::getFromRoot() const {
        return visit(
            [](auto &&arg) -> optional<not_null<const VarDecl *>> {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    return nullopt;
                } else if constexpr (is_same_v<T, not_null<unique_ptr<const Address>>>) {
                    return arg->getFromRoot();
                }
            },
            from_);
    }

    optional<not_null<const VarDecl *>> SymbolAddress::BaseInfo::getFromRoot() const {
        return visit(
            [](auto &&arg) -> optional<not_null<const VarDecl *>> {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    return nullopt;
                } else if constexpr (is_same_v<T, not_null<unique_ptr<const Address>>>) {
                    return arg->getFromRoot();
                }
            },
            from_);
    }

    optional<not_null<const VarDecl *>> VariableAddress::getFromRoot() const {
        return visit(
            [](auto &&arg) -> optional<not_null<const VarDecl *>> {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    return nullopt;
                } else if constexpr (is_same_v<T, not_null<const VarDecl *>>) {
                    return arg;
                }
            },
            from_);
    }

    optional<not_null<const VarDecl *>> FieldAddress::getFromRoot() const {
        return visit(
            [](auto &&arg) -> optional<not_null<const VarDecl *>> {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    return nullopt;
                } else if constexpr (is_same_v<T, pair<not_null<unique_ptr<const Address>>,
                                                       const size_t>>) {
                    return arg.first->getFromRoot();
                }
            },
            from_);
    }

    optional<not_null<const VarDecl *>> Variable::getFromRoot() const {
        return visit(
            [&](auto &&arg) -> optional<not_null<const VarDecl *>> {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    return nullopt;
                } else if constexpr (is_same_v<T, not_null<unique_ptr<const Address>>>) {
                    return arg->getFromRoot();
                }
            },
            from_);
    }

    bool Structure::Info::equal(const Structure::Info &other) const {
        if (definition_ != other.definition_)
            return false;
        return true;
    }

    bool Structure::Info::operator==(const Info &other) const { return equal(other); }

    bool Structure::equal(const SymbolicExpr &expr) const {
        const auto st = dynamic_cast<const Structure *>(&expr);
        if (!st)
            return false;
        if (!info_.equal(st->info_))
            return false;
        return ranges::equal(fields_, st->fields_,
                             [](auto &lhs, auto &rhs) { return *lhs == *rhs; });
    }

    optional<not_null<unique_ptr<SymbolAddress>>> BinaryOpExpr::doTryEvalAsSymbolAddr() const {
        auto lhs = callTryEvalAsAddr(*left_), rhs = callTryEvalAsAddr(*right_);
        if (lhs && rhs)
            return nullopt;
        if (lhs == nullopt && rhs == nullopt)
            return nullopt;

        unique_ptr<SymbolAddress> addr;
        if (lhs) {
            addr = std::move(lhs).value().into_underlying();
            if (!isValidOffsetOrLength(*right_))
                return nullopt;
            auto expr = right_->clone();
            switch (op_) {
                using enum Operator;
                case Add: addr->addOffset(std::move(expr)); break;
                case Subtract: addr->subOffset(std::move(expr)); break;

                default: return nullopt;
            }
        } else {
            addr = std::move(rhs).value().into_underlying();
            if (!isValidOffsetOrLength(*left_))
                return nullopt;
            auto expr = left_->clone();
            switch (op_) {
                using enum Operator;
                case Add: addr->addOffset(std::move(expr)); break;
                case Subtract: return nullopt;
                default: return nullopt;
            }
        }
        return addr;
    }

    optional<not_null<unique_ptr<SymbolAddress>>> SymbolAddress::doTryEvalAsSymbolAddr() const {
        auto result = make_unique<SymbolAddress>(*this);
        return result;
    }

    SymbolicExpr::UsedMap Variable::collectUsedVarsAndAddrs() const { return {{hash(), this}}; }

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
        visit(
            [this](auto &&arg) {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    from_ = monostate{};
                } else if constexpr (is_same_v<T, not_null<unique_ptr<const Address>>>) {
                    from_.emplace<1>(arg->addressClone().into_underlying());
                }
            },
            other.from_);
    }

    SymbolAddress::BaseInfo::BaseInfo(const SymbolAddress::BaseInfo &other)
        : fromPoint_(other.fromPoint_) {
        visit(
            [this](auto &&arg) {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    from_ = monostate{};
                } else if constexpr (is_same_v<T, not_null<unique_ptr<const Address>>>) {
                    from_.emplace<1>(arg->addressClone().into_underlying());
                }
            },
            other.from_);
    }

    SymbolAddress::SymbolAddress(variant<monostate, not_null<unique_ptr<const Address>>> from,
                                 SourcePoint fromPoint,
                                 optional<not_null<unique_ptr<const SymbolicExpr>>> offset,
                                 optional<not_null<unique_ptr<const SymbolicExpr>>> length)
        : Address(AddressType::SymbolAddr), offset_(make_unique<LiteralExpr>(ZERO_OFFSET)),
          from_(std::move(from)), fromPoint_(fromPoint) {
        if (offset != nullopt)
            offset_ = std::move(offset.value());
        if (length) {
            // Address Range.
            range_ = Range{std::move(length.value())};
        }
    }

    void SymbolAddress::setOffset(not_null<unique_ptr<SymbolicExpr>> offset) {
        if (!isValidOffsetOrLength(*offset))
            ERROR("Invalid offset.");
        offset_ = std::move(offset).into_underlying();
    }

    void SymbolAddress::addOffset(not_null<unique_ptr<SymbolicExpr>> extra) {
        if (!isValidOffsetOrLength(*extra))
            ERROR("Invalid offset.");
        offset_ = make_unique<BinaryOpExpr>(offset_->clone().into_underlying(),
                                            BinaryOpExpr::Operator::Add, std::move(extra))
                      ->simplifiedExpr()
                      .into_underlying();
    }

    void SymbolAddress::subOffset(not_null<unique_ptr<SymbolicExpr>> extra) {
        if (!isValidOffsetOrLength(*extra))
            ERROR("Invalid offset.");
        offset_ = make_unique<BinaryOpExpr>(offset_->clone(), BinaryOpExpr::Operator::Subtract,
                                            std::move(extra))
                      ->simplifiedExpr()
                      .into_underlying();
    }

    void SymbolAddress::setLength(not_null<unique_ptr<SymbolicExpr>> len) {
        if (!isValidOffsetOrLength(*len))
            ERROR("Invalid Length.");
        if (range_ == nullopt) {
            range_.emplace(std::move(len).into_underlying());
            return;
        }
        range_.value().len_ = std::move(len).into_underlying();
    }

    size_t SymbolAddress::BaseInfo::hash() const {
        auto seed = hash_val(fromPoint_.hash());
        return visit(
            [&](auto &&arg) -> size_t {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    return seed;
                } else if constexpr (is_same_v<T, not_null<unique_ptr<const Address>>>) {
                    return hash_val(seed, arg->hash());
                }
            },
            from_);
    }

    SymbolAddress::BaseInfo SymbolAddress::getBaseInfo() const {
        return visit(
            [this](auto &&arg) -> BaseInfo {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    return BaseInfo{monostate{}, fromPoint_};
                } else if constexpr (is_same_v<T, not_null<unique_ptr<const Address>>>) {
                    return BaseInfo{arg->addressClone().into_underlying(), fromPoint_};
                }
            },
            from_);
    }

    int SymbolAddress::getDimension() const {
        return visit(
            [](auto &&arg) {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    return -1;
                } else if constexpr (is_same_v<T, not_null<unique_ptr<const Address>>>) {
                    if (auto dim = arg->getDimension(); dim >= 0)
                        return dim + 1;
                    return -1;
                }
            },
            from_);
    }

    int VariableAddress::getDimension() const {
        return visit(
            [](auto &&arg) {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    return -1;
                } else if constexpr (is_same_v<T, not_null<const VarDecl *>>) {
                    return 0;
                }
            },
            from_);
    }

    int FieldAddress::getDimension() const {
        return visit(
            [](auto &&arg) {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    return -1;
                } else if constexpr (is_same_v<T, pair<not_null<unique_ptr<const Address>>,
                                                       const size_t>>) {
                    return arg.first->getDimension();
                }
            },
            from_);
    }

    VariableAddress::VariableAddress(const VariableAddress &other) : Address(other) {
        visit(
            [this](auto &&arg) {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    from_ = monostate{};
                } else if constexpr (is_same_v<T, not_null<const VarDecl *>>) {
                    from_ = arg;
                }
            },
            other.from_);
    }

    VariableAddress &VariableAddress::operator=(const VariableAddress &other) {
        if (this != &other) {
            Address::operator=(other);
            visit(
                [this](auto &&arg) {
                    using T = decay_t<decltype(arg)>;
                    if constexpr (is_same_v<T, monostate>) {
                        from_ = monostate{};
                    } else if constexpr (is_same_v<T, not_null<const VarDecl *>>) {
                        from_ = arg;
                    }
                },
                other.from_);
        }
        return *this;
    }

    FieldAddress::FieldAddress(const FieldAddress &other)
        : Address(other), definition_(other.definition_) {
        visit(
            [this](auto &&arg) {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    from_ = monostate{};
                } else if constexpr (is_same_v<T, pair<not_null<unique_ptr<const Address>>,
                                                       const size_t>>) {
                    from_.emplace<1>(pair<not_null<unique_ptr<const Address>>, const size_t>{
                        arg.first->addressClone().into_underlying(), arg.second});
                }
            },
            other.from_);
    }

    FieldAddress &FieldAddress::operator=(const FieldAddress &other) {
        if (this != &other) {
            Address::operator=(other);
            definition_ = other.definition_;
            visit(
                [this](auto &&arg) {
                    using T = decay_t<decltype(arg)>;
                    if constexpr (is_same_v<T, monostate>) {
                        from_ = monostate{};
                    } else if constexpr (is_same_v<T, pair<not_null<unique_ptr<const Address>>,
                                                           const size_t>>) {
                        from_.emplace<1>(pair<not_null<unique_ptr<const Address>>, const size_t>{
                            arg.first->addressClone().into_underlying(), arg.second});
                    }
                },
                other.from_);
        }
        return *this;
    }

    void Structure::setFieldValue(size_t index, not_null<unique_ptr<SymbolicExpr>> expr) {
        if (index >= fields_.size())
            ERROR("Out-of-bounds access");
        fields_[index] = std::move(expr);
    }

    optional<string> Structure::regularFormOfField(size_t index,
                                                   optional<string_view> prefix,
                                                   optional<string_view> suffix,
                                                   int,
                                                   bool) const {
        if (index >= fields_.size())
            ERROR("Out-of-bounds access");

        return fields_[index]->regularForm(prefix, suffix);
    }

    Structure::Structure(const RecordDecl *RD,
                         const ASTRecordLayout &layout,
                         variant<monostate, not_null<unique_ptr<const Address>>> from,
                         SourcePoint fromPoint)
        : SymbolicExpr(
              ExprType::Structure,
              Type{ScalarKind::Structure, static_cast<unsigned>(layout.getSize().getQuantity()) *
                                              8 /*By default, char is 8-bit.*/}),
          info_(Info{RD, layout}) {
        fields_.reserve(info_.layout_.getFieldCount());
        for (auto field : info_.definition_->fields()) {
            auto index    = field->getFieldIndex();
            auto addrFrom = visit(
                [&](auto &&arg)
                    -> variant<monostate, pair<not_null<unique_ptr<const Address>>, const size_t>> {
                    using T = decay_t<decltype(arg)>;
                    if constexpr (is_same_v<T, monostate>) {
                        return monostate{};
                    } else if constexpr (is_same_v<T, not_null<unique_ptr<const Address>>>) {
                        return pair{arg->addressClone().into_underlying(), index};
                    }
                },
                from);
            auto fieldAddr = make_unique<FieldAddress>(info_.definition_, std::move(addrFrom));

            QualType fty = field->getType();

            if (fty->isStructureType()) {
                auto nestedRD = fty->getAsRecordDecl();
                if (!nestedRD || !nestedRD->isCompleteDefinition())
                    ERROR("Incomplete nested struct definition");
                nestedRD           = nestedRD->getDefinition();
                auto &nestedLayout = nestedRD->getASTContext().getASTRecordLayout(nestedRD);
                auto nested =
                    make_unique<Structure>(nestedRD, nestedLayout, std::move(fieldAddr), fromPoint);
                fields_.emplace_back(std::move(nested));
            } else if (fty->isPointerType()) {
                auto addr = make_unique<SymbolAddress>(std::move(fieldAddr), fromPoint);
                fields_.emplace_back(std::move(addr));
            } else if (fty->isArrayType()) {
                TODO();
            } else {
                auto vty = deriveVarType(fty);
                auto var = make_unique<Variable>(vty, std::move(fieldAddr), fromPoint);
                fields_.emplace_back(std::move(var));
            }
        }
        if (fields_.size() != info_.layout_.getFieldCount())
            UNREACHABLE();
    }

    Variable::Variable(const Variable &other)
        : SymbolicExpr(other), varType_(other.varType_), fromPoint_(other.fromPoint_) {
        visit(
            [this](auto &&arg) {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    from_ = monostate{};
                } else if constexpr (is_same_v<T, not_null<unique_ptr<const Address>>>) {
                    from_.emplace<1>(arg->addressClone().into_underlying());
                }
            },
            other.from_);
    }

    ostream &operator<<(ostream &os, SymbolicExpr::ExprType t) {
        switch (t) {
            using enum SymbolicExpr::ExprType;
            case Literal: os << "Literal"; break;
            case Variable: os << "Variable"; break;
            case Address: os << "Address"; break;
            case BinaryOp: os << "BinaryOp"; break;
            case UnaryOp: os << "UnaryOp"; break;
            case Structure: os << "Structure"; break;
            case Unknown: os << "Unknown"; break;
        }
        return os;
    }

    Structure::From Structure::getFrom() const {
        // This structure has a fixed 'from' only if every member is from the same `FieldAddress`
        // **and** same `SourcePoint`.
        optional<not_null<unique_ptr<const Address>>> commonBaseAddr{};
        optional<SourcePoint> commonBasePoint{};
        for (size_t index = 0; index < fields_.size(); ++index) {
            auto &field = fields_.at(index);
            auto symbol = dynamic_cast<const Symbol *>(field.get().get());
            if (symbol == nullptr)
                return From{monostate{}, nullopt};
            auto fromAddr = symbol->getFromAddr();
            optional<not_null<unique_ptr<const Address>>> baseAddr{};
            visit(
                [&](auto &&arg) {
                    using T = decay_t<decltype(arg)>;
                    if constexpr (is_same_v<T, monostate>) {
                        return;
                    } else if constexpr (is_same_v<T, not_null<unique_ptr<const Address>>>) {
                        if (arg->getAddressType() != Address::AddressType::FieldAddr)
                            return;
                        auto fieldAddr = dynamic_cast<const FieldAddress &>(*arg);
                        visit(
                            [&](auto &&fieldFrom) {
                                using NT = decay_t<decltype(fieldFrom)>;
                                if constexpr (is_same_v<NT, monostate>) {
                                    return;
                                } else if constexpr (is_same_v<
                                                         NT,
                                                         pair<not_null<unique_ptr<const Address>>,
                                                              const size_t>>) {
                                    auto &[base, fieldId] = fieldFrom;
                                    if (fieldId != index)
                                        return;
                                    baseAddr.emplace(base->addressClone().into_underlying());
                                }
                            },
                            fieldAddr.getFrom());
                    }
                },
                fromAddr);
            if (baseAddr == nullopt)
                return From{monostate{}, nullopt};
            if (commonBaseAddr == nullopt)
                commonBaseAddr = std::move(baseAddr);

            auto fromPoint = symbol->getFromPoint();
            if (fromPoint == nullopt)
                return From{monostate{}, nullopt};
            if (commonBasePoint == nullopt)
                commonBasePoint.emplace(fromPoint.value());

            if (*commonBaseAddr.value() != *baseAddr.value() ||
                commonBasePoint.value() != fromPoint.value())
                return From{monostate{}, nullopt};
        }
        if (commonBaseAddr == nullopt || commonBasePoint == nullopt)
            UNREACHABLE();

        return From{std::move(commonBaseAddr).value(), std::move(commonBasePoint).value()};
    }

    variant<monostate, not_null<unique_ptr<const Address>>> Structure::getFromAddr() const {
        return getFrom().first;
    }

    optional<SourcePoint> Structure::getFromPoint() const { return getFrom().second; }

    SourcePoint &SourcePoint::operator=(const SourcePoint &other) {
        if (this == &other)
            return *this;
        if (&SM_ != &other.SM_)
            ERROR("SourcePoint from different SourceManager.");
        loc_ = other.loc_;
        return *this;
    }

    SourcePoint &SourcePoint::operator=(SourcePoint &&other) {
        if (this == &other)
            return *this;
        if (&SM_ != &other.SM_)
            ERROR("SourcePoint from different SourceManager.");
        loc_ = std::move(other.loc_);
        return *this;
    }

    SourcePoint SourcePoint::fromFuncDeclBefore(const FunctionDecl *FD,
                                                const SourceManager &SM,
                                                const LangOptions &LO) {
        SourcePoint p{SM};
        if (FD) {
            auto BL = FD->getBeginLoc();
            if (BL.isInvalid())
                ERROR("Location before FunctionDecl: {" +
                      Lexer::getSourceText(CharSourceRange::getTokenRange(FD->getSourceRange()), SM,
                                           LO)
                          .str() +
                      "} is invalid.");
            p.loc_ = SM.getExpansionLoc(BL);
        } else {
            ERROR("S is nullptr.");
        }
        return p;
    }

    SourcePoint SourcePoint::fromStmtBefore(const Stmt *S,
                                            const SourceManager &SM,
                                            const LangOptions &LO) {
        SourcePoint p{SM};
        if (S) {
            auto BL = S->getBeginLoc();
            if (BL.isInvalid())
                ERROR("Location before Stmt: {" +
                      Lexer::getSourceText(CharSourceRange::getTokenRange(S->getSourceRange()), SM,
                                           LO)
                          .str() +
                      "} is invalid.");
            p.loc_ = SM.getExpansionLoc(BL);
        } else {
            ERROR("S is nullptr.");
        }
        return p;
    }

    SourcePoint SourcePoint::fromStmtAfter(const Stmt *S,
                                           const SourceManager &SM,
                                           const LangOptions &LO) {
        SourcePoint p{SM};
        if (S) {
            auto EL = S->getEndLoc();

            auto AL = Lexer::getLocForEndOfToken(EL, /*Offset*/ 0, SM, LO);
            if (AL.isInvalid())
                ERROR("Location after Stmt: {" +
                      Lexer::getSourceText(CharSourceRange::getTokenRange(S->getSourceRange()), SM,
                                           LO)
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
            ERROR("SourcePoint from different SourceManager.");
        if (loc_.isInvalid() || other.loc_.isInvalid())
            UNREACHABLE();
        return SM_.isBeforeInTranslationUnit(loc_, other.loc_);
    }

    bool SourcePoint::operator==(const SourcePoint &other) const {
        if (&SM_ != &other.SM_) {
            WARN("SourcePoint from different SourceManager.");
            return false;
        }
        if (loc_.isInvalid() || other.loc_.isInvalid())
            UNREACHABLE();
        return !SM_.isBeforeInTranslationUnit(loc_, other.loc_) &&
               !SM_.isBeforeInTranslationUnit(other.loc_, loc_);
    }

    string SourcePoint::dump() const {
        if (loc_.isInvalid())
            ERROR("Invalid SourcePoint.");

        auto ploc = SM_.getPresumedLoc(loc_);
        if (ploc.isInvalid())
            ERROR("Invalid presumed SourcePoint.");

        ostringstream oss;
        oss << ploc.getFilename() << ":" << ploc.getLine() << ":" << ploc.getColumn();
        return oss.str();
    }
    unique_ptr<SymbolicExpr> createLNotExpr(not_null<unique_ptr<SymbolicExpr>> expr) {
        return make_unique<UnaryOpExpr>(UnaryOpExpr::Operator::LogicalNot, std::move(expr));
    }

    BinaryOpExpr::Operator getCompoundAssignOp(BinaryOperatorKind compoundAssignOp) {
        switch (compoundAssignOp) {
            case BO_MulAssign: return BinaryOpExpr::Operator::Multiply;
            case BO_DivAssign: return BinaryOpExpr::Operator::Divide;
            case BO_RemAssign: return BinaryOpExpr::Operator::Remainder;
            case BO_AddAssign: return BinaryOpExpr::Operator::Add;
            case BO_SubAssign: return BinaryOpExpr::Operator::Subtract;
            case BO_ShlAssign: return BinaryOpExpr::Operator::ShiftLeft;
            case BO_ShrAssign: return BinaryOpExpr::Operator::ShiftRight;
            case BO_AndAssign: return BinaryOpExpr::Operator::BitAnd;
            case BO_XorAssign: return BinaryOpExpr::Operator::BitXor;
            case BO_OrAssign: return BinaryOpExpr::Operator::BitOr;
            default: UNREACHABLE();
        }
    }

    // No AssignOp Here.
    BinaryOpExpr::Operator getBinaryOp(BinaryOperatorKind op) {
        switch (op) {
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

    SymbolicExpr::Type deriveVarType(QualType type) {
        if (auto ptr = type->getAs<PointerType>())
            return deriveVarType(ptr->getPointeeType());
        return TypeSwitch<QualType, SymbolicExpr::Type>(type.getCanonicalType())
            .Case([](const BuiltinType *BT) -> SymbolicExpr::Type {
                using Kind = SymbolicExpr::ScalarKind;

                switch (BT->getKind()) {
                    case BuiltinType::Bool: return {Kind::Bool, 1};
                    case BuiltinType::Char_S:
                    case BuiltinType::SChar: return {Kind::Int, 8};
                    case BuiltinType::Char_U:
                    case BuiltinType::UChar: return {Kind::UInt, 8};

                    case BuiltinType::Short: return {Kind::Int, 16};
                    case BuiltinType::UShort: return {Kind::UInt, 16};

                    case BuiltinType::Int: return {Kind::Int, 32};
                    case BuiltinType::UInt: return {Kind::UInt, 32};

                    case BuiltinType::Long: return {Kind::Int, 64};
                    case BuiltinType::ULong: return {Kind::UInt, 64};

                    case BuiltinType::LongLong: return {Kind::Int, 64};
                    case BuiltinType::ULongLong: return {Kind::UInt, 64};
                    case BuiltinType::Void: return {Kind::Void, 0};

                    default:
                        LangOptions langOpts;
                        PrintingPolicy pp(langOpts);
                        UNIMPLEMENT("Unsupported builtin type: " << BT->getName(pp).str());
                }
            })
            .Default([&](QualType QT) -> SymbolicExpr::Type {
                if (QT->isStructureType()) {
                    return {SymbolicExpr::ScalarKind::Structure, 0};
                }
                UNIMPLEMENT("Unsupported non-builtin type: " << QT.getAsString());
            });
    }

    bool isValidOffsetOrLength(const SymbolicExpr &expr) {
        if (expr.isUnknown())
            return true;
        if (!expr.isLinear())
            return false;
        if (expr.tryEvalAsSymbolAddr())
            return false;
        return true;
    }

    bool is_symbol_addr(const Address &a) noexcept {
        return a.getAddressType() == Address::AddressType::SymbolAddr;
    }

    bool isFrom(const SymbolicExpr &expr, const Address &fromAddr, SourcePoint fromPoint) {
        auto symbol = dynamic_cast<const Symbol *>(&expr);
        if (symbol == nullptr)
            return false;

        if (symbol->getFromPoint() == nullopt || symbol->getFromPoint().value() != fromPoint)
            return false;
        return visit(
            [&](auto &&arg) {
                using T = decay_t<decltype(arg)>;
                if constexpr (is_same_v<T, monostate>) {
                    return false;
                } else if constexpr (is_same_v<T, not_null<unique_ptr<const Address>>>) {
                    return fromAddr == *arg;
                }
            },
            symbol->getFromAddr());
    }

    not_null<unique_ptr<SymbolicExpr>> getSymbol(
        QualType type,
        variant<monostate, not_null<unique_ptr<const Address>>> from,
        SourcePoint fromPoint) {
        if (type->isPointerType()) {
            return make_unique<SymbolAddress>(std::move(from), std::move(fromPoint));
        } else if (type->isArrayType()) {
            TODO();
        } else if (type->isStructureType()) {
            auto *RD = type->getAsRecordDecl();
            if (!RD || !RD->isCompleteDefinition())
                ERROR("Incomplete struct definition");
            RD           = RD->getDefinition();
            auto &layout = RD->getASTContext().getASTRecordLayout(RD);
            return make_unique<Structure>(RD, layout, std::move(from), std::move(fromPoint));

        } else {
            SymbolicExpr::Type vty = deriveVarType(type);
            return make_unique<Variable>(vty, std::move(from), std::move(fromPoint));
        }
    }
} // namespace acslg::analyzer::symbolic