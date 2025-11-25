#include "aggregateExpr.h"

#include <llvm-19/llvm/Support/Casting.h>
#include <memory>
#include <optional>
#include <strings.h>

#include "expr.h"
#include "macros.h"
#include "utils.h"
#include "stringTemplate.h"
#include "Analyzer/state.h"

namespace acslg::analyzer::symbolic {
    OverRangeExpr::OverRangeExpr(const OverRangeExpr &other)
        : SymbolicExpr(other), range_(std::make_unique<SymbolAddress>(*other.range_)),
          indexName_(other.indexName_) {}

    OverRangeExpr &OverRangeExpr::operator=(const OverRangeExpr &other) {
        if (&other == this)
            return *this;
        SymbolicExpr::operator=(other);
        range_     = std::make_unique<SymbolAddress>(*other.range_);
        indexName_ = other.indexName_;
        return *this;
    }

    std::string OverRangeExpr::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        oss << "{" + key("range: ") + range_->dump() + "}, ";
        oss << "{" + key("index name: ") + accent(indexName_) + "}";
        return oss.str();
    }

    bool OverRangeExpr::equal(const SymbolicExpr &other) const {
        auto ORE = llvm::dyn_cast<const OverRangeExpr>(&other);
        if (ORE == nullptr)
            return false;
        if (*range_ != *ORE->range_)
            return false;
        // No indexName_.
        return true;
    }

    std::size_t OverRangeExpr::hash() const { return utils::hash_val(range_->hash()); }

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
        auto index = llvm::dyn_cast<const SymbolAddress::RangeIndex>(&other);
        if (!index)
            return false;

        // `name_` does not determine equality.
        return true;
    }

    std::size_t SymbolAddress::RangeIndex::hash() const { return utils::hash_val(getKind()); }

    utils::not_null<std::unique_ptr<SymbolicExpr>> SymbolAddress::RangeIndex::getSubstitutedExpr(
        const Path &,
        const SourcePoint &) const {
        return clone();
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> SymbolAddress::RangeIndex::
        getRangeIndexSubstituted(const SymbolAddrBaseInfo &, const SymbolicExpr &indexExpr) const {
        return indexExpr.clone();
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> SymbolAddress::RangeIndex::getSubstitutedValueExpr(
        const HashExprMap &hashExprMap) const {
        if (auto it = hashExprMap.find(hash()); it != hashExprMap.end())
            return it->second->clone();
        return clone();
    }

    std::string SumOverRange::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        oss << type("SumOverRange ");
        oss << OverRangeExpr::dump();
        return oss.str();
    }

    bool SumOverRange::equal(const SymbolicExpr &other) const {
        auto SOR = llvm::dyn_cast<const SumOverRange>(&other);
        if (SOR == nullptr)
            return false;
        return OverRangeExpr::equal(*SOR) && fromPoint_ == SOR->fromPoint_;
    }

    std::size_t SumOverRange::hash() const {
        return utils::hash_val(SymbolicExpr::getKind(), OverRangeExpr::hash(), fromPoint_.hash());
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> SumOverRange::getSubstitutedExpr(
        const Path &pathSubTo,
        const SourcePoint &pointToSub) const {
        if (fromPoint_ != pointToSub)
            return clone();
        auto subedExpr  = range_->getSubstitutedExpr(pathSubTo, pointToSub);
        auto subedRange = llvm::dyn_cast<SymbolAddress>(subedExpr.get().get());
        if (subedRange == nullptr || subedRange->getLength() == std::nullopt)
            ERROR("Substituted expression should be a *range*");
        return std::make_unique<SumOverRange>(std::make_unique<const SymbolAddress>(*subedRange),
                                              indexName_, pathSubTo.getStartPoint());
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> SumOverRange::getRangeIndexSubstituted(
        const SymbolAddrBaseInfo &rangeBase,
        const SymbolicExpr &indexExpr) const {
        auto subedExpr  = range_->getRangeIndexSubstituted(rangeBase, indexExpr);
        auto subedRange = llvm::dyn_cast<SymbolAddress>(subedExpr.get().get());
        if (subedRange == nullptr || subedRange->getLength() == std::nullopt)
            ERROR("Substituted expression should be a *range*");
        return std::make_unique<SumOverRange>(std::make_unique<const SymbolAddress>(*subedRange),
                                              indexName_, fromPoint_);
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> SumOverRange::getSubstitutedValueExpr(
        const HashExprMap &hashExprMap) const {
        if (auto it = hashExprMap.find(hash()); it != hashExprMap.end())
            return it->second->clone();
        auto subedExpr  = range_->getSubstitutedValueExpr(hashExprMap);
        auto subedRange = llvm::dyn_cast<SymbolAddress>(subedExpr.get().get());
        if (subedRange == nullptr || subedRange->getLength() == std::nullopt)
            ERROR("Substituted expression should be a *range*");
        return std::make_unique<SumOverRange>(std::make_unique<const SymbolAddress>(*subedRange),
                                              indexName_, fromPoint_);
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> SumOverRange::doGetACSL(
        const GetACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned,
        bool) const {
        auto st = spec_generator::StringTemplate{
            "\\sum(integer ${i} = ${0}; ${i} < ${n}; ${i}++, ${prefix}${a}[${i}]${suffix})"};

        auto zeroStr = callGetACSL(*range_->getOffset(), config, usedPoints, currentPoint,
                                   getPrecedence(Operator::Assign), true);
        if (!zeroStr)
            return zeroStr.error();

        auto rightBound = range_->getRightBound();
        if (rightBound == std::nullopt)
            UNREACHABLE();
        auto nStr = callGetACSL(*rightBound.value(), config, usedPoints, currentPoint,
                                getPrecedence(Operator::LessThan), true);
        if (!nStr)
            return nStr.error();

        auto rangeFrom = range_->getFromAddr();
        if (rangeFrom == std::nullopt)
            return GetACSLError::HeapAddress;

        auto [prefix, suffix] =
            details::getPrefixSuffixAndUpdateMap(config, usedPoints, currentPoint, fromPoint_);
        bool hasAt = !(prefix.empty() || suffix.empty());

        auto aStr =
            callGetACSLOfValueProxy(*range_->getFromAddr().value(), config, usedPoints, fromPoint_,
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

    utils::not_null<std::unique_ptr<SymbolicExpr>> QuantifierOverRange::getSubstitutedExpr(
        const Path &pathSubTo,
        const SourcePoint &pointToSub) const {
        auto subedExpr  = range_->getSubstitutedExpr(pathSubTo, pointToSub);
        auto subedRange = llvm::dyn_cast<SymbolAddress>(subedExpr.get().get());
        if (subedRange == nullptr || subedRange->getLength() == std::nullopt)
            ERROR("Substituted expression should be a *range*");

        auto subedPred = pred_->getSubstitutedExpr(pathSubTo, pointToSub);

        auto newQOR    = std::make_unique<QuantifierOverRange>(*this);
        newQOR->range_ = std::make_unique<const SymbolAddress>(*subedRange);
        newQOR->pred_  = std::move(subedPred).into_underlying();
        return newQOR;
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> QuantifierOverRange::getRangeIndexSubstituted(
        const SymbolAddrBaseInfo &rangeBase,
        const SymbolicExpr &indexExpr) const {
        auto subedExpr  = range_->getRangeIndexSubstituted(rangeBase, indexExpr);
        auto subedRange = llvm::dyn_cast<SymbolAddress>(subedExpr.get().get());
        if (subedRange == nullptr || subedRange->getLength() == std::nullopt)
            ERROR("Substituted expression should be a *range*");
        auto subedPred = pred_->getRangeIndexSubstituted(rangeBase, indexExpr);

        auto newQOR    = std::make_unique<QuantifierOverRange>(*this);
        newQOR->range_ = std::make_unique<const SymbolAddress>(*subedRange);
        newQOR->pred_  = std::move(subedPred).into_underlying();
        return newQOR;
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> QuantifierOverRange::getSubstitutedValueExpr(
        const HashExprMap &hashExprMap) const {
        if (auto it = hashExprMap.find(hash()); it != hashExprMap.end())
            return it->second->clone();
        auto subedExpr  = range_->getSubstitutedValueExpr(hashExprMap);
        auto subedRange = llvm::dyn_cast<SymbolAddress>(subedExpr.get().get());
        if (subedRange == nullptr || subedRange->getLength() == std::nullopt)
            ERROR("Substituted expression should be a *range*");
        auto subedPred = pred_->getSubstitutedValueExpr(hashExprMap);

        auto newQOR    = std::make_unique<QuantifierOverRange>(*this);
        newQOR->range_ = std::make_unique<const SymbolAddress>(*subedRange);
        newQOR->pred_  = std::move(subedPred).into_underlying();
        return newQOR;
    }

    QuantifierOverRange &QuantifierOverRange::operator=(const QuantifierOverRange &other) {
        if (&other == this)
            return *this;
        OverRangeExpr::operator=(other);
        quant_ = other.quant_;
        pred_  = other.pred_->clone().into_underlying();
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
        auto QOV = llvm::dyn_cast<const QuantifierOverRange>(&other);
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

        auto zeroStr = callGetACSL(*range_->getOffset()->simplifiedExpr(), config, usedPoints,
                                   currentPoint, getPrecedence(Operator::LessThan), false);
        if (!zeroStr)
            return zeroStr.error();

        auto rightBound = range_->getRightBound();
        if (rightBound == std::nullopt)
            UNREACHABLE();
        auto nStr = callGetACSL(*rightBound.value()->simplifiedExpr(), config, usedPoints,
                                currentPoint, getPrecedence(Operator::LessThan), true);
        if (!nStr)
            return nStr.error();

        auto predStr = callGetACSL(
            *pred_->simplifiedExpr(), config, usedPoints, currentPoint,
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

    utils::not_null<std::unique_ptr<SymbolicExpr>> MaxMinOverRange::getSubstitutedExpr(
        const Path &pathSubTo,
        const SourcePoint &pointToSub) const {
        auto subedExpr  = range_->getSubstitutedExpr(pathSubTo, pointToSub);
        auto subedRange = llvm::dyn_cast<SymbolAddress>(subedExpr.get().get());
        if (subedRange == nullptr || subedRange->getLength() == std::nullopt)
            ERROR("Substituted expression should be a *range*");

        auto subedBody = expr_->getSubstitutedExpr(pathSubTo, pointToSub);

        auto newMMOR    = std::make_unique<MaxMinOverRange>(*this);
        newMMOR->range_ = std::make_unique<const SymbolAddress>(*subedRange);
        newMMOR->expr_  = std::move(subedBody).into_underlying();
        if (fromPoint_ == pointToSub)
            TODO();
        return newMMOR;
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> MaxMinOverRange::getRangeIndexSubstituted(
        const SymbolAddrBaseInfo &rangeBase,
        const SymbolicExpr &indexExpr) const {
        auto subedExpr  = range_->getRangeIndexSubstituted(rangeBase, indexExpr);
        auto subedRange = llvm::dyn_cast<SymbolAddress>(subedExpr.get().get());
        if (subedRange == nullptr || subedRange->getLength() == std::nullopt)
            ERROR("Substituted expression should be a *range*");
        auto subedBody = expr_->getRangeIndexSubstituted(rangeBase, indexExpr);

        auto newMMOR    = std::make_unique<MaxMinOverRange>(*this);
        newMMOR->range_ = std::make_unique<const SymbolAddress>(*subedRange);
        newMMOR->expr_  = std::move(subedBody).into_underlying();
        return newMMOR;
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> MaxMinOverRange::getSubstitutedValueExpr(
        const HashExprMap &hashExprMap) const {
        if (auto it = hashExprMap.find(hash()); it != hashExprMap.end())
            return it->second->clone();
        auto subedExpr  = range_->getSubstitutedValueExpr(hashExprMap);
        auto subedRange = llvm::dyn_cast<SymbolAddress>(subedExpr.get().get());
        if (subedRange == nullptr || subedRange->getLength() == std::nullopt)
            ERROR("Substituted expression should be a *range*");
        auto subedBody = expr_->getSubstitutedValueExpr(hashExprMap);

        auto newMMOR    = std::make_unique<MaxMinOverRange>(*this);
        newMMOR->range_ = std::make_unique<const SymbolAddress>(*subedRange);
        newMMOR->expr_  = std::move(subedBody).into_underlying();
        return newMMOR;
    }

    MaxMinOverRange &MaxMinOverRange::operator=(const MaxMinOverRange &other) {
        if (&other == this)
            return *this;
        OverRangeExpr::operator=(other);
        Symbol::operator=(other);
        extremum_  = other.extremum_;
        expr_      = other.expr_->clone().into_underlying();
        fromPoint_ = other.fromPoint_;
        return *this;
    }

    MaxMinOverRange::MaxMinOverRange(utils::not_null<std::unique_ptr<const SymbolAddress>> range,
                                     std::string_view indexName,
                                     Extremum extremum,
                                     SourcePoint fromPoint)
        : OverRangeExpr(ExprKind::K_MaxMinOverRange,
                        deriveType(range->getPointeeType()),
                        std::move(range),
                        indexName),
          Symbol(Kind::K_MaxMinOverRange), extremum_(extremum),
          expr_(makeDefaultExpr(*range_, indexName, fromPoint).into_underlying()),
          fromPoint_(std::move(fromPoint)) {}

    utils::not_null<std::unique_ptr<const SymbolicExpr>> MaxMinOverRange::makeDefaultExpr(
        const SymbolAddress &range,
        std::string_view indexName,
        const SourcePoint &fromPoint) {
        auto indexedRange = std::make_unique<SymbolAddress>(range);
        indexedRange->setOffset(std::make_unique<SymbolAddress::RangeIndex>(indexName));
        indexedRange->resetLength();
        return getSymbol(range.getPointeeType(), std::move(indexedRange), fromPoint)
            .into_underlying();
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
        auto MMOR = llvm::dyn_cast<const MaxMinOverRange>(&other);
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

        auto rightBound = range_->getRightBound();
        if (rightBound == std::nullopt)
            UNREACHABLE();

        auto [prefix, suffix] =
            details::getPrefixSuffixAndUpdateMap(config, usedPoints, currentPoint, fromPoint_);

        auto lowerStr = callGetACSL(*range_->getOffset()->simplifiedExpr(), config, usedPoints,
                                    fromPoint_, getPrecedence(Operator::LessEqual), false);
        if (!lowerStr)
            return lowerStr.error();

        auto upperStr = callGetACSL(*rightBound.value()->simplifiedExpr(), config, usedPoints,
                                    fromPoint_, getPrecedence(Operator::LessThan), true);
        if (!upperStr)
            return upperStr.error();

        auto cmpOp   = extremum_ == Extremum::Max ? Operator::GreaterEqual : Operator::LessEqual;
        auto exprStr = callGetACSL(*expr_->simplifiedExpr(), config, usedPoints, fromPoint_,
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
