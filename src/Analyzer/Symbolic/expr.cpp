#include "expr.h"

#include <memory>
#include <sstream>
#include <cstring>
#include <variant>
#include <ranges>
#include <llvm/ADT/TypeSwitch.h>
#include <llvm/Support/Casting.h>

#include "aggregateExpr.h" // IWYU pragma: keep
#include "macros.h"
#include "utils.h"
#include "Analyzer/state.h"

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
        size_t seed = utils::hash_val(getKind(), type_);

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
        size_t seed = utils::hash_val(getKind(), fromAddr_->hash(), fromPoint_.hash());
        return seed;
    }

    size_t UnaryOpExpr::hash() const {
        return utils::hash_val(getKind(), static_cast<size_t>(op_), expr_->hash());
    }

    size_t BinaryOpExpr::hash() const {
        return utils::hash_val(getKind(), static_cast<size_t>(op_), left_->hash(), right_->hash());
    }

    size_t SymbolAddress::hash() const {
        size_t seed = utils::hash_val(getKind(), fromPoint_.hash(), offset_->hash(),
                                      length_ ? length_.value()->hash() : 0);

        seed = utils::hash_val(seed, fromAddr_ ? fromAddr_.value()->hash() : 0);
        return seed;
    }

    size_t VariableAddress::hash() const { return utils::hash_val(getKind(), from_.get()); }

    size_t FieldAddress::hash() const {
        return utils::hash_val(getKind(), baseAddr_->hash(), fieldIndex_);
    }

    size_t Structure::hash() const {
        auto seed = utils::hash_val(getKind(), info_.definition_.get());
        for (auto &field : fields_)
            seed = utils::hash_val(seed, field->hash());
        return seed;
    }

    size_t UnknownExpr::hash() const { return utils::hash_val(getKind()); }

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

    std::string SymbolValue::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        const auto &t = getValType();

        oss << type("SymbolValue") << "(";
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
        if (length_ == std::nullopt)
            oss << "[" << off->dump() << "]";
        else
            oss << "[" << off->dump() << " " << hint("... +") << length_.value()->dump() << "]";

        oss << " {" << key("from") << "=";
        if (fromAddr_)
            oss << key("addr") << ":" << fromAddr_.value()->dump();
        else
            oss << hint("none");
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
        oss << key("field of") << ":" << baseAddr_.get()->dump() << "["
            << utils::dump_fmt::lit(std::to_string(fieldIndex_)) << "]";
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

    utils::expected<std::string, SymbolicExpr::GetACSLError> LiteralExpr::doGetACSL(
        const SymbolicExpr::GetACSLConfig &,
        std::unordered_set<SourcePoint> &,
        std::optional<SourcePoint>,
        unsigned,
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

    utils::expected<std::string, SymbolicExpr::GetACSLError> BinaryOpExpr::doGetACSL(
        const SymbolicExpr::GetACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned parentPrec,
        bool isRightChild) const {
        std::ostringstream oss;
        std::string opStr;
        switch (op_) {
#define BIN_OP(name, tok, prec, isRight)                                                           \
    case Operator::name: opStr = tok; break;
#include "operators.def"
            default: UNREACHABLE();
        }

        auto myPrec     = getPrecedence(op_);
        bool needParens = (myPrec < parentPrec) ||
                          (myPrec == parentPrec && isRightChild && !isRightAssociative(op_));

        auto leftStr = callGetACSL(*left_, config, usedPoints, currentPoint, myPrec, false);
        if (!leftStr)
            return leftStr.error();
        auto rightStr = callGetACSL(*right_, config, usedPoints, currentPoint, myPrec, true);
        if (!rightStr)
            return rightStr.error();
        oss << (needParens ? "(" : "") << leftStr.value() << " " << opStr << " " << rightStr.value()
            << (needParens ? ")" : "");
        return oss.str();
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> UnaryOpExpr::doGetACSL(
        const SymbolicExpr::GetACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned parentPrec,
        bool isRightChild) const {
        std::ostringstream oss;
        std::string opStr;
        switch (op_) {
#define UN_OP(name, tok, prec, isRight)                                                            \
    case Operator::name: opStr = tok; break;
#include "operators.def"
            default: UNREACHABLE();
        }

        auto myPrec     = getPrecedence(op_);
        bool needParens = (myPrec < parentPrec) ||
                          (myPrec == parentPrec && isRightChild && !isRightAssociative(op_));

        if (op_ == Operator::PostInc || op_ == Operator::PostDec) {
            auto subStr = callGetACSL(*expr_, config, usedPoints, currentPoint, myPrec, false);
            if (!subStr)
                return subStr.error();
            oss << (needParens ? "(" : "") << subStr.value() << opStr << (needParens ? ")" : "");
        } else {
            auto subStr = callGetACSL(*expr_, config, usedPoints, currentPoint, myPrec, true);
            if (!subStr)
                return subStr.error();
            oss << (needParens ? "(" : "") << opStr << subStr.value() << (needParens ? ")" : "");
        }
        return oss.str();
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> UnknownExpr::doGetACSL(
        const SymbolicExpr::GetACSLConfig &config,
        std::unordered_set<SourcePoint> &,
        std::optional<SourcePoint>,
        unsigned,
        bool) const {
        if (!config.UnknownExprAsError) {
            WARN("Output UnknownExpr's ACSL, something may go wrong.");
            return std::string{"{Unknown}"};
        }
        return SymbolicExpr::GetACSLError::UnknownExpr;
    }

    namespace {
        std::pair<std::string, std::string> getPrefixSuffixAndUpdateMap(
            const SymbolicExpr::GetACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            const SourcePoint &myPoint) {
            if (!config.noStateLabelFunctionAt && myPoint != currentPoint) {
                if (auto it = config.predefinedLabels.find(myPoint);
                    it != config.predefinedLabels.end()) {
                    return std::pair{"\\at(", ", " + it->second + ")"};
                } else {
                    auto label = myPoint.getLabel();
                    usedPoints.insert(myPoint);
                    return std::pair{"\\at(", ", " + label + ")"};
                }
            }
            return {};
        }

        bool isNeedParens(Operator myOp, unsigned parentPrec, bool isRightChild) {
            auto myPrec = getPrecedence(myOp);
            return (myPrec < parentPrec) ||
                   (myPrec == parentPrec && isRightChild && !isRightAssociative(myOp));
        }
    } // namespace

    utils::expected<std::string, SymbolicExpr::GetACSLError> SymbolValue::doGetACSL(
        const SymbolicExpr::GetACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned parentPrec,
        bool isRightChild) const {
        auto [prefix, suffix] =
            getPrefixSuffixAndUpdateMap(config, usedPoints, currentPoint, fromPoint_);
        bool hasAt    = !(prefix.empty() || suffix.empty());
        auto valueStr = callGetACSLOfValueProxy(*fromAddr_, config, usedPoints, fromPoint_,
                                                hasAt ? /* enclosed in \\at() */ 0 : parentPrec,
                                                hasAt ? false : isRightChild);
        if (!valueStr)
            return valueStr.error();

        return prefix + std::move(valueStr.value()) + suffix;
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> SymbolAddress::doGetACSL(
        const SymbolicExpr::GetACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned parentPrec,
        bool isRightChild) const {
        if (length_)
            ERROR("Address range has no regularForm but regularFormOfValue.");
        if (fromAddr_ == std::nullopt)
            return SymbolicExpr::GetACSLError::HeapAddress;

        auto offsetStr = callGetACSL(*offset_, config, usedPoints, fromPoint_,
                                     getPrecedence(Operator::Add), true);
        if (!offsetStr)
            return offsetStr.error();

        if (offsetStr.value() == "0")
            offsetStr.value().clear();

        auto [prefix, suffix] =
            getPrefixSuffixAndUpdateMap(config, usedPoints, currentPoint, fromPoint_);
        bool hasAt     = !(prefix.empty() || suffix.empty());
        bool hasOffset = !offsetStr.value().empty();

        auto nameStrExpected = [&, this]() {
            if (hasAt)
                return callGetACSLOfValue(*fromAddr_.value(), config, usedPoints, fromPoint_,
                                          /* enclosed in \\at() */ 0, false);
            if (hasOffset)
                return callGetACSLOfValue(*fromAddr_.value(), config, usedPoints, fromPoint_,
                                          getPrecedence(Operator::Add), false);
            return callGetACSLOfValue(*fromAddr_.value(), config, usedPoints, fromPoint_,
                                      parentPrec, isRightChild);
        }();
        if (!nameStrExpected)
            return nameStrExpected.error();

        auto nameStr = prefix + std::move(nameStrExpected.value()) + suffix;

        auto offsetedAddrStr = hasOffset ? std::move(nameStr) + " + " + std::move(offsetStr.value())
                                         : std::move(nameStr);

        if (hasOffset) {
            auto myPrec     = getPrecedence(Operator::Add);
            bool needParens = (myPrec < parentPrec) || (myPrec == parentPrec && isRightChild);
            return (needParens ? "(" : "") + std::move(offsetedAddrStr) + (needParens ? ")" : "");
        }

        return offsetedAddrStr;
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> VariableAddress::doGetACSL(
        const SymbolicExpr::GetACSLConfig &,
        std::unordered_set<SourcePoint> &,
        std::optional<SourcePoint>,
        unsigned parentPrec,
        bool isRightChild) const {
        bool needParens = isNeedParens(Operator::AddrOf, parentPrec, isRightChild);
        return (needParens ? "(" : "") + std::string{"&"} + from_->getNameAsString() +
               (needParens ? ")" : "");
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> FieldAddress::doGetACSL(
        const SymbolicExpr::GetACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned parentPrec,
        bool isRightChild) const {
        auto valueStr = doGetACSLOfValue(config, usedPoints, currentPoint,
                                         getPrecedence(Operator::AddrOf), true);
        if (!valueStr)
            return valueStr.error();

        bool needParens = isNeedParens(Operator::AddrOf, parentPrec, isRightChild);
        return (needParens ? "(" : "") + std::string{"&"} + std::move(valueStr.value()) +
               (needParens ? ")" : "");
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> SymbolAddress::doGetACSLOfValue(
        const SymbolicExpr::GetACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned parentPrec,
        bool isRightChild) const {
        if (fromAddr_ == std::nullopt)
            return SymbolicExpr::GetACSLError::HeapAddress;

        auto [prefix, suffix] =
            getPrefixSuffixAndUpdateMap(config, usedPoints, currentPoint, fromPoint_);
        bool hasAt = !(prefix.empty() || suffix.empty());

        auto nameStrExpected =
            callGetACSLOfValue(*fromAddr_.value(), config, usedPoints, fromPoint_,
                               hasAt ? 0 : getPrecedence(Operator::Subscript), false);
        if (!nameStrExpected)
            return nameStrExpected.error();

        auto nameStr = prefix + std::move(nameStrExpected.value()) + suffix;

        if (length_ == std::nullopt) {
            auto offsetStr = callGetACSL(*offset_, config, usedPoints, fromPoint_,
                                         /* enclosed in [] */ 0, false);
            if (!offsetStr)
                return offsetStr.error();

            auto useDeref = offsetStr.value() == "0" && config.useDerefWithZeroOffset;
            std::string subedAddrStr;

            if (useDeref) {
                auto recalcNameStrExpected =
                    callGetACSLOfValue(*fromAddr_.value(), config, usedPoints, fromPoint_,
                                       hasAt ? 0 : getPrecedence(Operator::Dereference), true);
                if (!recalcNameStrExpected)
                    return recalcNameStrExpected.error();

                nameStr      = prefix + recalcNameStrExpected.value() + suffix;
                subedAddrStr = "*" + std::move(nameStr);
            } else
                subedAddrStr = std::move(nameStr) + "[" + std::move(offsetStr.value()) + "]";

            bool needParens = isNeedParens((useDeref ? Operator::Dereference : Operator::Subscript),
                                           parentPrec, isRightChild);
            return (needParens ? "(" : "") + std::move(subedAddrStr) + (needParens ? ")" : "");
        }

        /*---------------- deal with range -----------------*/
        auto rangePrec = getPrecedence(Operator::Range);
        auto offsetStr = callGetACSL(*offset_, config, usedPoints, fromPoint_, rangePrec, false);
        if (!offsetStr)
            return offsetStr.error();

        // offset + length - 1
        auto rightBound =
            std::make_unique<BinaryOpExpr>(
                std::make_unique<BinaryOpExpr>(getOffset()->clone(), BinaryOpExpr::Operator::Add,
                                               length_.value()->clone()),
                BinaryOpExpr::Operator::Subtract, std::make_unique<LiteralExpr>(1))
                ->simplifiedExpr();
        auto rightBoundStr =
            callGetACSL(*rightBound, config, usedPoints, fromPoint_, rangePrec, true);
        if (!rightBoundStr)
            return rightBoundStr.error();

        auto subedAddrStr = std::move(nameStr) + "[" + std::move(offsetStr.value()) + " .. " +
                            std::move(rightBoundStr.value()) + "]";

        bool needParens = isNeedParens(Operator::Subscript, parentPrec, isRightChild);
        return (needParens ? "(" : "") + std::move(subedAddrStr) + (needParens ? ")" : "");
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> VariableAddress::doGetACSLOfValue(
        const SymbolicExpr::GetACSLConfig &,
        std::unordered_set<SourcePoint> &,
        std::optional<SourcePoint>,
        unsigned,
        bool) const {
        return from_->getNameAsString();
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> FieldAddress::doGetACSLOfValue(
        const SymbolicExpr::GetACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned parentPrec,
        bool isRightChild) const {
        auto baseStr = callGetACSLOfValue(*baseAddr_, config, usedPoints, currentPoint,
                                          getPrecedence(Operator::MemberAccess), false);
        if (!baseStr)
            return baseStr.error();
        if (baseStr.value().empty())
            ERROR("Empty base std::string");
        auto fields = definition_->fields();
        auto it     = std::ranges::next(fields.begin(), fieldIndex_, fields.end());
        if (it == fields.end())
            ERROR("Out-of-bounds access");
        auto fieldStr = it->getNameAsString();

        std::string concatenatedStr;
        concatenatedStr = baseStr.value() + "." + fieldStr;

        bool needParens = isNeedParens(Operator::MemberAccess, parentPrec, isRightChild);
        return (needParens ? "(" : "") + concatenatedStr + (needParens ? ")" : "");
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> Structure::doGetACSL(
        const SymbolicExpr::GetACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned parentPrec,
        bool isRightChild) const {
        auto from = getFrom();
        if (from == std::nullopt)
            return SymbolicExpr::GetACSLError::PartiallyModifiedStruct;

        auto &[fromAddr, fromPoint] = from.value();
        auto [prefix, suffix] =
            getPrefixSuffixAndUpdateMap(config, usedPoints, currentPoint, fromPoint);
        bool hasAt    = !(prefix.empty() || suffix.empty());
        auto valueStr = callGetACSLOfValueProxy(*fromAddr, config, usedPoints, fromPoint,
                                                hasAt ? /* enclosed in \\at() */ 0 : parentPrec,
                                                hasAt ? false : isRightChild);
        if (!valueStr)
            return valueStr.error();

        return prefix + std::move(valueStr.value()) + suffix;
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> LiteralExpr::simplifiedExpr() const {
        return simplifiedExprIfLinear(); // Here, unlike a direct `clone`, after
                                         // `simplifiedExprIfLinear`, all constants will have the
                                         // same type.
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

    utils::not_null<std::unique_ptr<SymbolicExpr>> SymbolAddress::simplifiedExpr() const {
        if (length_)
            ERROR("Address range is solely for address representation and should not be "
                  "used as an expression.");
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

        // compare from
        if (fromAddr_ && other->fromAddr_ && *fromAddr_.value() != *other->fromAddr_.value()) {
            return false;
        }

        if ((other->fromAddr_ == std::nullopt) ^ (fromAddr_ == std::nullopt))
            return false;

        if (fromPoint_ != other->fromPoint_)
            return false;

        // compare offset
        // todo: Need a `offsetEqual`, here is not correct now.
        if (*offset_->simplifiedExpr() != *other->getOffset()->simplifiedExpr()) {
            return false;
        }

        // compare range
        if (length_ != std::nullopt && other->length_ != std::nullopt) {
            if (*length_.value() != *(other->length_.value()))
                return false;
        } else if ((length_ == std::nullopt) ^ (other->length_ == std::nullopt)) {
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

        return *baseAddr_ == *other->baseAddr_ && fieldIndex_ == other->fieldIndex_;
    }

    std::optional<utils::not_null<const clang::VarDecl *>> SymbolAddress::getFromRoot() const {
        if (fromAddr_ == std::nullopt)
            return std::nullopt;
        return fromAddr_.value()->getFromRoot();
    }

    std::optional<utils::not_null<const clang::VarDecl *>> SymbolAddress::BaseInfo::getFromRoot()
        const {
        if (fromAddr_ == std::nullopt)
            return std::nullopt;
        return fromAddr_.value()->getFromRoot();
    }

    std::optional<utils::not_null<const clang::VarDecl *>> VariableAddress::getFromRoot() const {
        return from_;
    }

    std::optional<utils::not_null<const clang::VarDecl *>> FieldAddress::getFromRoot() const {
        return baseAddr_->getFromRoot();
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

    utils::not_null<std::unique_ptr<SymbolicExpr>> LiteralExpr::getSubstitutedExpr(
        const Path &,
        const SourcePoint &) const {
        // Nothing to substitute
        return clone();
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> UnknownExpr::getSubstitutedExpr(
        const Path &,
        const SourcePoint &) const {
        // Nothing to substitute
        return clone();
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> VariableAddress::getSubstitutedExpr(
        const Path &,
        const SourcePoint &) const {
        // Nothing to substitute
        return clone();
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> SymbolValue::getSubstitutedExpr(
        const Path &pathSubTo,
        const SourcePoint &pointToSub) const {
        auto &mem = pathSubTo.getMemoryState();
        if (getFromPoint() && getFromPoint().value() != pointToSub)
            return clone();

        auto subedExpr    = fromAddr_->getSubstitutedExpr(pathSubTo, pointToSub);
        auto realFromAddr = llvm::dyn_cast<const Address>(subedExpr.get().get());
        if (realFromAddr == nullptr)
            UNREACHABLE();

        // Origin is an address-like handle; try reading from loop-entry memory.
        if (auto value = mem.read(*realFromAddr)) {
            // Replace current SymbolValue with the cloned value read from memory.
            return value.value()->clone();
        } else {
            // Address originates from an address present on this path at loop
            // entry but hasn't been accessed -> construct a SymbolValue with
            // corrext fromAddr and pointToSub.
            return std::make_unique<SymbolValue>(getValType(),
                                                 realFromAddr->addressClone().into_underlying(),
                                                 pathSubTo.getStartPoint());
        }
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> SymbolAddress::getSubstitutedExpr(
        const Path &pathSubTo,
        const SourcePoint &pointToSub) const {
        auto &mem = pathSubTo.getMemoryState();
        if (getFromPoint() && getFromPoint().value() != pointToSub)
            return clone();

        if (fromAddr_ == std::nullopt) {
            auto newAddr        = std::make_unique<SymbolAddress>(*this);
            newAddr->fromPoint_ = pointToSub;
            return newAddr;
        }

        auto subedExpr    = fromAddr_.value()->getSubstitutedExpr(pathSubTo, pointToSub);
        auto realFromAddr = llvm::dyn_cast<const Address>(subedExpr.get().get());
        if (realFromAddr == nullptr)
            UNREACHABLE();

        // Clone and substitute the offset of the original SymbolAddress.
        auto offset = offset_->getSubstitutedExpr(pathSubTo,
                                                  pointToSub)
                          ->simplifiedExpr(); // substitute std::any symbol in offset

        std::optional<utils::not_null<std::unique_ptr<SymbolicExpr>>> length{};
        // If original was a range, also substitute and std::set the length.
        if (length_)
            length = length_.value()->getSubstitutedExpr(pathSubTo, pointToSub)->simplifiedExpr();

        // Origin is an address; attempt to read the value at that origin.
        if (auto value = mem.read(*realFromAddr)) {
            // The origin resolves to a value; it must be convertible to an `SymbolAddress`.

            auto realAddr = value.value()->tryEvalAsSymbolAddr();
            if (realAddr == std::nullopt)
                ERROR("This expr should be a `SymbolAddress");

            // Apply substituted offset to the concrete address.
            realAddr.value()->addOffset(std::move(offset));

            if (length) {
                realAddr.value()->setLength(std::move(length).value());
            }
            // Return the underlying concrete address (std::unique_ptr<Address>).
            return std::move(realAddr).value().into_underlying();
        } else {
            // The origin hasn't been accessed at loop entry -> construct a
            // SymbolAddress with corrext fromAddr and fromPoint.
            if (length == std::nullopt) {
                return std::make_unique<SymbolAddress>(
                    pointeeType_, realFromAddr->addressClone().into_underlying(),
                    pathSubTo.getStartPoint(), std::move(offset).into_underlying(), std::nullopt);
            }
            return std::make_unique<SymbolAddress>(
                pointeeType_, realFromAddr->addressClone().into_underlying(),
                pathSubTo.getStartPoint(), std::move(offset).into_underlying(),
                std::move(length).value().into_underlying());
        };
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> FieldAddress::getSubstitutedExpr(
        const Path &pathSubTo,
        const SourcePoint &pointToSub) const {
        auto subedExpr    = baseAddr_->getSubstitutedExpr(pathSubTo, pointToSub);
        auto realBaseAddr = llvm::dyn_cast<const Address>(subedExpr.get().get());
        if (realBaseAddr == nullptr)
            UNREACHABLE();
        return std::make_unique<FieldAddress>(
            pointeeType_, definition_, realBaseAddr->addressClone().into_underlying(), fieldIndex_);
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> BinaryOpExpr::getSubstitutedExpr(
        const Path &pathSubTo,
        const SourcePoint &pointToSub) const {
        return std::make_unique<BinaryOpExpr>(left_->getSubstitutedExpr(pathSubTo, pointToSub), op_,
                                              right_->getSubstitutedExpr(pathSubTo, pointToSub));
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> UnaryOpExpr::getSubstitutedExpr(
        const Path &pathSubTo,
        const SourcePoint &pointToSub) const {
        return std::make_unique<UnaryOpExpr>(op_, expr_->getSubstitutedExpr(pathSubTo, pointToSub));
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> Structure::getSubstitutedExpr(
        const Path &pathSubTo,
        const SourcePoint &pointToSub) const {
        auto newSt = std::make_unique<Structure>(*this);
        for (auto &field : newSt->fieldsValues()) {
            field = field->getSubstitutedExpr(pathSubTo, pointToSub);
        }
        return newSt;
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
        if (length_)
            ERROR("Address range is solely for address representation and should not be "
                  "used as an expression.");
        return {{hash(), this}};
    }

    SymbolAddress::SymbolAddress(const SymbolAddress &other)
        : Address(other), Symbol(other), offset_(other.offset_->clone().into_underlying()),
          fromPoint_(other.fromPoint_), length_(std::nullopt) {
        if (other.length_)
            length_ = other.length_.value()->clone().into_underlying();
        if (other.fromAddr_ == std::nullopt)
            fromAddr_ = std::nullopt;
        else
            fromAddr_ = other.fromAddr_.value()->addressClone().into_underlying();
    }

    SymbolAddress::BaseInfo::BaseInfo(const SymbolAddress::BaseInfo &other)
        : fromPoint_(other.fromPoint_) {
        if (other.fromAddr_ == std::nullopt)
            fromAddr_ = std::nullopt;
        else
            fromAddr_ = other.fromAddr_.value()->addressClone().into_underlying();
    }

    SymbolAddress::SymbolAddress(
        const clang::QualType pointeeType,
        std::optional<utils::not_null<std::unique_ptr<const Address>>> from,
        SourcePoint fromPoint,
        std::optional<utils::not_null<std::unique_ptr<const SymbolicExpr>>> offset,
        std::optional<utils::not_null<std::unique_ptr<const SymbolicExpr>>> length)
        : Address(SymbolicExpr::ExprKind::K_SymbolAddress,
                  SymbolicExpr::Type{SymbolicExpr::ScalarKind::UInt, 64},
                  pointeeType),
          offset_(std::make_unique<LiteralExpr>(ZERO_OFFSET)), fromAddr_(std::move(from)),
          fromPoint_(fromPoint), length_(std::move(length)) {
        if (offset != std::nullopt)
            offset_ = std::move(offset.value());
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
        length_.emplace(std::move(len).into_underlying());
    }

    void SymbolAddress::addLength(utils::not_null<std::unique_ptr<SymbolicExpr>> extra) {
        if (!isValidOffsetOrLength(*extra))
            ERROR("Invalid offset.");
        if (length_ == std::nullopt) {
            length_.emplace(std::make_unique<BinaryOpExpr>(std::make_unique<LiteralExpr>(1),
                                                           BinaryOpExpr::Operator::Add,
                                                           std::move(extra))
                                ->simplifiedExpr()
                                .into_underlying());
            return;
        }
        length_ = std::make_unique<BinaryOpExpr>(length_.value()->clone().into_underlying(),
                                                 BinaryOpExpr::Operator::Add, std::move(extra))
                      ->simplifiedExpr()
                      .into_underlying();
    }

    size_t SymbolAddress::BaseInfo::hash() const {
        return utils::hash_val(fromPoint_.hash(), fromAddr_ ? fromAddr_.value()->hash() : 0);
    }

    std::optional<utils::not_null<std::unique_ptr<SymbolicExpr>>> SymbolAddress::getRightBound()
        const {
        // Not sure return which one is better, offset_+1 or nullopt.
        if (length_ == std::nullopt)
            return std::nullopt;
        return std::make_unique<BinaryOpExpr>(offset_->clone(), BinaryOpExpr::Operator::Add,
                                              length_.value()->clone());
    }

    SymbolAddress::BaseInfo SymbolAddress::getBaseInfo() const {
        if (fromAddr_ == std::nullopt)
            return BaseInfo{std::nullopt, fromPoint_, pointeeType_};
        return BaseInfo{fromAddr_.value()->addressClone().into_underlying(), fromPoint_,
                        pointeeType_};
    }

    int SymbolAddress::getDimension() const {
        if (fromAddr_ == std::nullopt)
            return -1;
        if (auto dim = fromAddr_.value()->getDimension(); dim >= 0)
            return dim + 1;
        return -1;
    }

    int VariableAddress::getDimension() const { return 0; }

    int FieldAddress::getDimension() const { return baseAddr_->getDimension(); }

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
          baseAddr_(other.baseAddr_->addressClone().into_underlying()),
          fieldIndex_(other.fieldIndex_) {}

    FieldAddress &FieldAddress::operator=(const FieldAddress &other) {
        if (this == &other)
            return *this;
        Address::operator=(other);
        definition_ = other.definition_;
        baseAddr_   = other.baseAddr_->addressClone().into_underlying();
        fieldIndex_ = other.fieldIndex_;
        return *this;
    }

    void Structure::setFieldValue(size_t index,
                                  utils::not_null<std::unique_ptr<SymbolicExpr>> expr) {
        if (index >= fields_.size())
            ERROR("Out-of-bounds access");
        fields_[index] = std::move(expr);
    }

    Structure::Structure(const clang::RecordDecl *RD,
                         const clang::ASTRecordLayout &layout,
                         utils::not_null<std::unique_ptr<const Address>> from,
                         SourcePoint fromPoint)
        : SymbolicExpr(
              ExprKind::K_Structure,
              Type{ScalarKind::Structure, static_cast<unsigned>(layout.getSize().getQuantity()) *
                                              8 /*By default, char is 8-bit.*/}),
          info_(Info{RD, layout}) {
        fields_.reserve(info_.layout_.getFieldCount());
        for (auto field : info_.definition_->fields()) {
            auto index          = field->getFieldIndex();
            clang::QualType fty = field->getType();

            auto fieldAddr = std::make_unique<FieldAddress>(
                fty, info_.definition_, from->addressClone().into_underlying(), index);

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
                auto addr = std::make_unique<SymbolAddress>(fty, std::move(fieldAddr), fromPoint);
                fields_.emplace_back(std::move(addr));
            } else if (fty->isArrayType()) {
                TODO();
            } else {
                auto vty = deriveType(fty);
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

    std::ostream &operator<<(std::ostream &os, SymbolicExpr::ExprKind t) {
        switch (t) {
            using enum SymbolicExpr::ExprKind;
            case K_LiteralExpr: os << "Literal"; break;
            case K_SymbolValue: os << "SymbolValue"; break;
            case K_SymbolAddress: os << "SymbolAddr"; break;
            case K_VariableAddress: os << "VariableAddr"; break;
            case K_FieldAddress: os << "FieldAddr"; break;
            case K_BinaryOpExpr: os << "BinaryOp"; break;
            case K_UnaryOpExpr: os << "UnaryOp"; break;
            case K_Structure: os << "Structure"; break;
            case K_UnknownExpr: os << "Unknown"; break;
            default: UNREACHABLE();
        }
        return os;
    }

    Structure::From Structure::getFrom() const {
        // This structure has a fixed 'from' only if every member is from the same `FieldAddress`
        // **and** same `SourcePoint`.
        std::optional<utils::not_null<std::unique_ptr<const Address>>> commonBaseAddr{};
        std::optional<SourcePoint> commonFromPoint{};
        for (size_t index = 0; index < fields_.size(); ++index) {
            auto &field = fields_.at(index);
            auto symbol = llvm::dyn_cast<const Symbol>(field.get().get());
            if (symbol == nullptr)
                return std::nullopt;
            auto fromAddr = symbol->getFromAddr();
            if (fromAddr == std::nullopt)
                return std::nullopt;
            auto fieldAddr = llvm::dyn_cast<const FieldAddress>(fromAddr.value().get().get());
            if (fieldAddr == nullptr)
                return std::nullopt;

            auto &base    = fieldAddr->getBaseAddr();
            auto &fieldId = fieldAddr->getFieldIndex();
            if (fieldId != index)
                return std::nullopt;

            if (commonBaseAddr == std::nullopt)
                commonBaseAddr = base->addressClone().into_underlying();

            auto fromPoint = symbol->getFromPoint();
            if (fromPoint == std::nullopt)
                return std::nullopt;
            if (commonFromPoint == std::nullopt)
                commonFromPoint.emplace(fromPoint.value());

            if (*commonBaseAddr.value() != *base || commonFromPoint.value() != fromPoint.value())
                return std::nullopt;
        }
        if (commonBaseAddr == std::nullopt || commonFromPoint == std::nullopt)
            UNREACHABLE();

        return std::pair{std::move(commonBaseAddr).value(), std::move(commonFromPoint).value()};
    }

    std::optional<utils::not_null<std::unique_ptr<const Address>>> Structure::getFromAddr() const {
        if (auto from = getFrom())
            return std::move(from.value().first);
        return std::nullopt;
    }

    std::optional<SourcePoint> Structure::getFromPoint() const {
        if (auto from = getFrom())
            return std::move(from.value().second);
        return std::nullopt;
    }

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

    SourcePoint SourcePoint::fromFuncDecl(const clang::FunctionDecl *FD,
                                          const clang::SourceManager &SM,
                                          const clang::LangOptions &LO) {
        if (FD == nullptr)
            ERROR("FunctionDecl is null.");
        if (!FD->hasBody())
            ERROR("FunctionDecl has no body.");

        auto labelPrefix = "BeginOf_" + FD->getNameAsString();
        SourcePoint p{SM, labelPrefix};

        const clang::Stmt *body = FD->getBody();
        assert(body != nullptr);

        clang::SourceLocation BL;

        if (const auto *CS = llvm::dyn_cast<clang::CompoundStmt>(body)) {
            BL = clang::Lexer::getLocForEndOfToken(CS->getLBracLoc(), 0, SM, LO);
        } else {
            BL = body->getBeginLoc();
        }
        if (BL.isInvalid()) {
            ERROR("Location is invalid: {" +
                  clang::Lexer::getSourceText(
                      clang::CharSourceRange::getTokenRange(FD->getSourceRange()), SM, LO)
                      .str() +
                  "}");
        }

        p.loc_ = SM.getExpansionLoc(BL);
        return p;
    }

    SourcePoint SourcePoint::fromStmtBefore(const clang::Stmt *S,
                                            const clang::SourceManager &SM,
                                            const clang::LangOptions &LO) {
        if (S == nullptr)
            ERROR("S is nullptr.");
        auto labelPrefix = std::string{"Before_"} + S->getStmtClassName();
        SourcePoint p{SM, std::move(labelPrefix)};
        auto BL = S->getBeginLoc();
        if (BL.isInvalid())
            ERROR("Location before clang::Stmt: {" +
                  clang::Lexer::getSourceText(
                      clang::CharSourceRange::getTokenRange(S->getSourceRange()), SM, LO)
                      .str() +
                  "} is invalid.");
        p.loc_ = SM.getExpansionLoc(BL);
        return p;
    }

    SourcePoint SourcePoint::fromStmtAfter(const clang::Stmt *S,
                                           const clang::SourceManager &SM,
                                           const clang::LangOptions &LO) {
        if (S == nullptr)
            ERROR("S is nullptr.");
        auto labelPrefix = std::string{"After_"} + S->getStmtClassName();
        SourcePoint p{SM, std::move(labelPrefix)};
        auto EL = S->getEndLoc();

        auto AL = clang::Lexer::getLocForEndOfToken(EL, /*Offset*/ 0, SM, LO);
        if (AL.isInvalid())
            ERROR("Location after clang::Stmt: {" +
                  clang::Lexer::getSourceText(
                      clang::CharSourceRange::getTokenRange(S->getSourceRange()), SM, LO)
                      .str() +
                  "} is invalid.");
        p.loc_ = SM.getExpansionLoc(AL);
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

    SymbolicExpr::Type deriveType(clang::QualType type) {
        if (auto ptr = type->getAs<clang::PointerType>())
            return deriveType(ptr->getPointeeType());
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
        auto from = symbol->getFromAddr();
        if (from == std::nullopt)
            return false;
        return fromAddr == *from.value();
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> getSymbol(
        clang::QualType type,
        std::optional<utils::not_null<std::unique_ptr<const Address>>> from,
        SourcePoint fromPoint) {
        if (type->isPointerType()) {
            if (from)
                return std::make_unique<SymbolAddress>(type, std::move(from.value()),
                                                       std::move(fromPoint));
            return std::make_unique<SymbolAddress>(type, std::nullopt, std::move(fromPoint));
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
            SymbolicExpr::Type vty = deriveType(type);
            return std::make_unique<SymbolValue>(vty, std::move(from.value()),
                                                 std::move(fromPoint));
        }
    }

    bool Symbol::classof(const SymbolicExpr *e) {
        return
#define SUBCLASS(NAME) llvm::isa<NAME>(e) ||
#include "subclassesOfSymbol.inc"
#undef SUBCLASS
            0;
    }

    Symbol *Symbol::toThis(SymbolicExpr *e) {
        if (!Symbol::classof(e))
            return nullptr;
        return llvm::TypeSwitch<SymbolicExpr *, Symbol *>(e)
#define SUBCLASS(NAME) .Case<NAME>([](NAME *sub) { return static_cast<NAME *>(sub); })
#include "subclassesOfSymbol.inc"
#undef SUBCLASS
            .Default([](SymbolicExpr *) {
                UNREACHABLE();
                return nullptr;
            });
    }

    const Symbol *Symbol::toThis(const SymbolicExpr *e) {
        return toThis(const_cast<SymbolicExpr *>(e));
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> Symbol::callGetACSLOfValueProxy(
        const Address &addr,
        const SymbolicExpr::GetACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned parentPrec,
        bool isRightChild) {
        return Address::callGetACSLOfValue(addr, config, usedPoints, currentPoint, parentPrec,
                                           isRightChild);
    }

} // namespace acslg::analyzer::symbolic