/**
 * @file aggregateExpr.cpp
 * @brief Implements aggregate symbolic expressions such as sums and quantifiers over ranges.
 */
#include "aggregateExpr.h"

#include <memory>
#include <optional>
#include <strings.h>

#include "expr.h"
#include "macros.h"
#include "utils.h"
#include "stringTemplate.h"
#include "Analyzer/state.h"

namespace acslg::analyzer::symbolic {
    namespace {
        ExprHandle makeMaxMinDefaultBody(ExprFactory &factory,
                                         const SymbolAddress &range,
                                         std::string_view indexName,
                                         const SourcePoint &fromPoint) {
            auto indexedRange = Addr{factory, factory.importAddress(range)}
                                    .withOffset(Expr{factory, factory.rangeIndex(indexName)});
            indexedRange = indexedRange.withoutLength();
            return getSymbol(range.getPointeeType(), indexedRange.handle(), fromPoint);
        }
    } // namespace

    ExprHandle makeSumOverRangeHandle(ExprFactory &factory,
                                      const SymbolAddress &range,
                                      std::string_view indexName,
                                      SourcePoint fromPoint) {
        return factory.intern(std::make_unique<SumOverRange>(
            factory.importAddress(range), indexName, std::move(fromPoint)));
    }

    ExprHandle makeQuantifierOverRangeHandle(ExprFactory &factory,
                                             const SymbolAddress &range,
                                             std::string_view indexName,
                                             QuantifierOverRange::Quantifier quantifier,
                                             const SymbolicExpr &predicate) {
        return makeQuantifierOverRangeHandle(factory, factory.importAddress(range),
                                             indexName, quantifier,
                                             factory.importExpr(predicate));
    }

    ExprHandle makeQuantifierOverRangeHandle(ExprFactory &factory,
                                             AddrHandle range,
                                             std::string_view indexName,
                                             QuantifierOverRange::Quantifier quantifier,
                                             ExprHandle predicate) {
        return factory.intern(std::make_unique<QuantifierOverRange>(
            range, indexName, quantifier, predicate));
    }

    ExprHandle makeMaxMinOverRangeHandle(ExprFactory &factory,
                                         const SymbolAddress &range,
                                         std::string_view indexName,
                                         MaxMinOverRange::Extremum extremum,
                                         SourcePoint fromPoint) {
        return makeMaxMinOverRangeHandle(factory, factory.importAddress(range), indexName,
                                         extremum, std::move(fromPoint));
    }

    ExprHandle makeMaxMinOverRangeHandle(ExprFactory &factory,
                                         AddrHandle range,
                                         std::string_view indexName,
                                         MaxMinOverRange::Extremum extremum,
                                         SourcePoint fromPoint) {
        auto body = makeMaxMinDefaultBody(factory, range.cast<SymbolAddress>(), indexName,
                                          fromPoint);
        return makeMaxMinOverRangeHandle(factory, range, indexName, extremum, body,
                                         std::move(fromPoint));
    }

    ExprHandle makeMaxMinOverRangeHandle(ExprFactory &factory,
                                         const SymbolAddress &range,
                                         std::string_view indexName,
                                         MaxMinOverRange::Extremum extremum,
                                         ExprHandle body,
                                         SourcePoint fromPoint) {
        return makeMaxMinOverRangeHandle(factory, factory.importAddress(range), indexName,
                                         extremum, body, std::move(fromPoint));
    }

    ExprHandle makeMaxMinOverRangeHandle(ExprFactory &factory,
                                         AddrHandle range,
                                         std::string_view indexName,
                                         MaxMinOverRange::Extremum extremum,
                                         ExprHandle body,
                                         SourcePoint fromPoint) {
        return factory.intern(std::make_unique<MaxMinOverRange>(
            range, indexName, extremum, body, std::move(fromPoint)));
    }

    ExprHandle makeMaxMinOverRangeHandle(ExprFactory &factory,
                                         const SymbolAddress &range,
                                         std::string_view indexName,
                                         MaxMinOverRange::Extremum extremum,
                                         const SymbolicExpr &body,
                                         SourcePoint fromPoint) {
        return makeMaxMinOverRangeHandle(factory, range, indexName, extremum,
                                         factory.importExpr(body), std::move(fromPoint));
    }

    SumOverRange::SumOverRange(AddrHandle range,
                               std::string_view indexName,
                               SourcePoint fromPoint)
        : OverRangeExpr(ExprKind::K_SumOverRange,
                        deriveType(range.cast<SymbolAddress>().getPointeeType()),
                        range,
                        indexName),
          Symbol(Kind::K_SumOverRange),
          fromPoint_(std::move(fromPoint)) {}

    OverRangeExpr::OverRangeExpr(const OverRangeExpr &other)
        : SymbolicExpr(other), range_(other.range_),
          indexName_(other.indexName_) {}

    OverRangeExpr &OverRangeExpr::operator=(const OverRangeExpr &other) {
        if (&other == this)
            return *this;
        SymbolicExpr::operator=(other);
        range_     = other.range_;
        indexName_ = other.indexName_;
        return *this;
    }

    const SymbolAddress &OverRangeExpr::range() const {
        auto *symbolAddr = dyn_cast<const SymbolAddress>(range_.get().get());
        if (symbolAddr == nullptr)
            ERROR("Over-range expression range child must be a SymbolAddress.");
        return *symbolAddr;
    }

    std::string OverRangeExpr::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        oss << "{" + key("range: ") + range().dump() + "}, ";
        oss << "{" + key("index name: ") + accent(indexName_) + "}";
        return oss.str();
    }

    bool OverRangeExpr::equal(const SymbolicExpr &other) const {
        auto ORE = dyn_cast<const OverRangeExpr>(&other);
        if (ORE == nullptr)
            return false;
        if (getValType() != other.getValType())
            return false;
        if (range() != ORE->range())
            return false;
        // No indexName_.
        return true;
    }

    std::size_t OverRangeExpr::hash() const { return utils::hash_val(range().hash()); }

    utils::not_null<std::unique_ptr<SymbolicExpr>> SymbolAddress::RangeIndex::clone() const {
        return std::make_unique<RangeIndex>(*this);
    };

    std::string SymbolAddress::RangeIndex::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        oss << type("RangeIndex") << " {" << lit(name_) << "}";
        return oss.str();
    }

    bool SymbolAddress::RangeIndex::equal(const SymbolicExpr &other) const {
        auto index = dyn_cast<const SymbolAddress::RangeIndex>(&other);
        if (!index)
            return false;
        if (getValType() != other.getValType())
            return false;

        // `name_` does not determine equality.
        return true;
    }

    std::size_t SymbolAddress::RangeIndex::hash() const { return utils::hash_val(getKind()); }

    std::string SumOverRange::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        oss << type("SumOverRange ");
        oss << OverRangeExpr::dump();
        return oss.str();
    }

    bool SumOverRange::equal(const SymbolicExpr &other) const {
        auto SOR = dyn_cast<const SumOverRange>(&other);
        if (SOR == nullptr)
            return false;
        return OverRangeExpr::equal(*SOR) && fromPoint_ == SOR->fromPoint_;
    }

    std::size_t SumOverRange::hash() const {
        return utils::hash_val(SymbolicExpr::getKind(), OverRangeExpr::hash(), fromPoint_.hash());
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> SumOverRange::doGetACSL(
        const GetACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned,
        bool) const {
        auto st = spec_generator::StringTemplate{
            "\\sum(integer ${i} = ${0}; ${i} < ${n}; ${i}++, ${prefix}${a}[${i}]${suffix})"};

        // Lower bound of a range is always zero for now.
        auto zeroStr = callGetACSL(*range().getOffset(), config, usedPoints, currentPoint,
                                   getPrecedence(Operator::Assign), true);
        if (!zeroStr)
            return zeroStr.error();

        auto rightBound = range().getRightBound();
        if (rightBound == std::nullopt)
            UNREACHABLE();
        auto nStr = callGetACSL(*rightBound.value(), config, usedPoints, currentPoint,
                                getPrecedence(Operator::LessThan), true);
        if (!nStr)
            return nStr.error();

        auto rangeFrom = range().getFromAddrHandle();
        if (rangeFrom == std::nullopt)
            return GetACSLError::HeapAddress;

        auto [prefix, suffix] =
            details::getPrefixSuffixAndUpdateMap(config, usedPoints, currentPoint, fromPoint_);
        bool hasAt = !(prefix.empty() || suffix.empty());

        // Build the pointer expression used inside the summation body.
        auto aStr = callGetACSLOfValueProxy(**rangeFrom, config, usedPoints, fromPoint_,
                                            hasAt ? 0 : getPrecedence(Operator::Subscript), false);
        if (!aStr)
            return aStr.error();

        return st.to_string({{"i", indexName_},
                             {"0", zeroStr.value()},
                             {"n", nStr.value()},
                             {"prefix", prefix},
                             {"a", aStr.value()},
                             {"suffix", suffix}});
    }

    QuantifierOverRange &QuantifierOverRange::operator=(const QuantifierOverRange &other) {
        if (&other == this)
            return *this;
        OverRangeExpr::operator=(other);
        quant_ = other.quant_;
        pred_  = other.pred_;
        return *this;
    }

    std::string QuantifierOverRange::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        oss << type("QuantifierOverRange ");
        switch (quant_) {
            case Quantifier::Exist: oss << accent("Exists"); break;
            case Quantifier::ForAll: oss << accent("ForAll"); break;
            default: UNREACHABLE();
        }
        oss << OverRangeExpr::dump() << ", ";
        oss << "{" << key("predicate: ") << pred_->dump() << "}";
        return oss.str();
    }

    bool QuantifierOverRange::equal(const SymbolicExpr &other) const {
        auto QOV = dyn_cast<const QuantifierOverRange>(&other);
        if (QOV == nullptr)
            return false;
        return OverRangeExpr::equal(*QOV) && quant_ == QOV->quant_ && *pred_ == *QOV->pred_;
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> QuantifierOverRange::doGetACSL(
        const GetACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned,
        bool) const {
        auto st = spec_generator::StringTemplate{
            "\\${quant} integer ${i}; ${0} <= ${i} < ${n} ${entailOrAnd} ${pred}"};

        auto &factory = ExprFactoryScope::current();
        std::string quantStr, entailOrAnd;
        switch (quant_) {
            using enum Quantifier;
            case ForAll:
                quantStr    = "forall";
                entailOrAnd = "==>";
                break;
            case Exist:
                quantStr    = "exists";
                entailOrAnd = "&&";
                break;
            default: UNREACHABLE();
        }

        auto zero = simplifiedExprHandle(factory, *range().getOffset());
        auto zeroStr = callGetACSL(*zero, config, usedPoints, currentPoint,
                                   getPrecedence(Operator::LessThan), false);
        if (!zeroStr)
            return zeroStr.error();

        auto rightBound = range().getRightBound();
        if (rightBound == std::nullopt)
            UNREACHABLE();
        auto upper = simplifiedExprHandle(factory, *rightBound.value());
        auto nStr = callGetACSL(*upper, config, usedPoints, currentPoint,
                                getPrecedence(Operator::LessThan), true);
        if (!nStr)
            return nStr.error();

        auto pred = simplifiedExprHandle(factory, *pred_);
        auto predStr = callGetACSL(
            *pred, config, usedPoints, currentPoint,
            getPrecedence(entailOrAnd == "==>" ? Operator::Entailment : Operator::LogicalAnd),
            true);
        if (!predStr)
            return predStr.error();

        return st.to_string({{"quant", quantStr},
                             {"i", indexName_},
                             {"0", zeroStr.value()},
                             {"n", nStr.value()},
                             {"entailOrAnd", entailOrAnd},
                             {"pred", predStr.value()}});
    }

    MaxMinOverRange &MaxMinOverRange::operator=(const MaxMinOverRange &other) {
        if (&other == this)
            return *this;
        OverRangeExpr::operator=(other);
        Symbol::operator=(other);
        extremum_  = other.extremum_;
        expr_      = other.expr_;
        fromPoint_ = other.fromPoint_;
        return *this;
    }

    std::string MaxMinOverRange::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        oss << type("MaxMinOverRange ");
        switch (extremum_) {
            case Extremum::Max: oss << accent("Max"); break;
            case Extremum::Min: oss << accent("Min"); break;
            default: UNREACHABLE();
        }
        oss << OverRangeExpr::dump() << ", ";
        oss << "{" << key("expr: ") << expr_->dump() << "}";
        return oss.str();
    }

    bool MaxMinOverRange::equal(const SymbolicExpr &other) const {
        auto MMOR = dyn_cast<const MaxMinOverRange>(&other);
        if (MMOR == nullptr)
            return false;
        return OverRangeExpr::equal(*MMOR) && extremum_ == MMOR->extremum_ &&
               *expr_ == *MMOR->expr_ && fromPoint_ == MMOR->fromPoint_;
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> MaxMinOverRange::doGetACSL(
        const GetACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned,
        bool) const {
        // todo: use axiom to define a \max.
        // WP plugin doesn't support \max
        /*
        auto st = spec_generator::StringTemplate{
            "\\${extremum}(${l}, ${u}, \\lambda integer ${i}; ${expr})"};
        // ...
        return st.to_string({{"extremum", extremumStr},
                             {"i", indexName_},
                             {"l", lowerStr.value()},
                             {"u", upperStr.value()},
                             {"expr", exprStr.value()}});
        */

        auto tmpl = spec_generator::StringTemplate{
            "(\\forall integer ${i}; ${l} <= ${i} < ${u} ==> \\result ${cmp} ${expr}) &&\n"
            "      (\\exists integer ${i}; ${l} <= ${i} < ${u} && \\result == ${expr})"};

        auto rightBound = range().getRightBound();
        if (rightBound == std::nullopt)
            UNREACHABLE();

        auto &factory = ExprFactoryScope::current();
        auto [prefix, suffix] =
            details::getPrefixSuffixAndUpdateMap(config, usedPoints, currentPoint, fromPoint_);

        auto lower = simplifiedExprHandle(factory, *range().getOffset());
        auto lowerStr = callGetACSL(*lower, config, usedPoints, fromPoint_,
                                    getPrecedence(Operator::LessEqual), false);
        if (!lowerStr)
            return lowerStr.error();

        auto upper = simplifiedExprHandle(factory, *rightBound.value());
        auto upperStr = callGetACSL(*upper, config, usedPoints, fromPoint_,
                                    getPrecedence(Operator::LessThan), true);
        if (!upperStr)
            return upperStr.error();

        auto cmpOp   = extremum_ == Extremum::Max ? Operator::GreaterEqual : Operator::LessEqual;
        auto expr    = simplifiedExprHandle(factory, *expr_);
        auto exprStr = callGetACSL(*expr, config, usedPoints, fromPoint_,
                                   getPrecedence(cmpOp), true);
        if (!exprStr)
            return exprStr.error();

        auto exprVal = (!prefix.empty() || !suffix.empty()) ? prefix + exprStr.value() + suffix
                                                            : exprStr.value();

        std::string cmpStr = extremum_ == Extremum::Max ? ">=" : "<=";

        return tmpl.to_string({{"i", indexName_},
                               {"l", lowerStr.value()},
                               {"u", upperStr.value()},
                               {"cmp", cmpStr},
                               {"expr", exprVal}});
    }

} // namespace acslg::analyzer::symbolic
