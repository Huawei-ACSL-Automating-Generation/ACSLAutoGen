/**
 * @file aggregateExpr.cpp
 * @brief Implements aggregate symbolic expressions such as sums and quantifiers over ranges.
 */
#include "aggregateExpr.h"

#include <memory>
#include <optional>
#include <strings.h>

#include "expr.h"
#include "detail/aggregateNodes.h"
#include "detail/factoryInternals.h"
#include "macros.h"
#include "utils.h"
#include "stringTemplate.h"
#include "Analyzer/state.h"

namespace acslg::analyzer::symbolic {
    using detail::MaxMinOverRangeNode;
    using detail::OverRangeExprNode;
    using detail::QuantifierOverRangeNode;
    using detail::RangeIndexNode;
    using detail::SumOverRangeNode;

    namespace {
        ExprHandle makeMaxMinDefaultBody(ExprFactory &factory,
                                         SymbolAddressView range,
                                         std::string_view indexName,
                                         const SourcePoint &fromPoint) {
            auto indexedRange = Addr{factory, factory.importAddress(range.handle())}
                                    .withOffset(Expr{factory, factory.rangeIndex(indexName)});
            indexedRange = indexedRange.withoutLength();
            return getSymbol(range.pointeeType(), indexedRange.handle(), fromPoint);
        }
    } // namespace

    SumOverRangeView::SumOverRangeView(ExprHandle handle) : handle_(handle) {
        if (!handle_->isSumOverRange())
            ERROR("SumOverRangeView requires a sum-over-range expression.");
    }

    std::optional<SumOverRangeView> SumOverRangeView::tryFrom(ExprHandle handle) {
        if (!handle->isSumOverRange())
            return std::nullopt;
        return SumOverRangeView{handle};
    }

    SymbolAddressView SumOverRangeView::range() const {
        return cast<const SumOverRangeNode>(handle_.get().get())->getRange();
    }

    std::string_view SumOverRangeView::indexName() const {
        return cast<const SumOverRangeNode>(handle_.get().get())->getIndexName();
    }

    SourcePoint SumOverRangeView::fromPoint() const {
        return cast<const SumOverRangeNode>(handle_.get().get())->getFromPoint().value();
    }

    QuantifierOverRangeView::QuantifierOverRangeView(ExprHandle handle) : handle_(handle) {
        if (!handle_->isQuantifierOverRange())
            ERROR("QuantifierOverRangeView requires a quantified range expression.");
    }

    std::optional<QuantifierOverRangeView> QuantifierOverRangeView::tryFrom(ExprHandle handle) {
        if (!handle->isQuantifierOverRange())
            return std::nullopt;
        return QuantifierOverRangeView{handle};
    }

    SymbolAddressView QuantifierOverRangeView::range() const {
        return cast<const QuantifierOverRangeNode>(handle_.get().get())->getRange();
    }

    std::string_view QuantifierOverRangeView::indexName() const {
        return cast<const QuantifierOverRangeNode>(handle_.get().get())->getIndexName();
    }

    RangeQuantifier QuantifierOverRangeView::quantifier() const {
        return cast<const QuantifierOverRangeNode>(handle_.get().get())->getQuantifier();
    }

    ExprHandle QuantifierOverRangeView::predicate() const {
        return cast<const QuantifierOverRangeNode>(handle_.get().get())->getPredicate();
    }

    MaxMinOverRangeView::MaxMinOverRangeView(ExprHandle handle) : handle_(handle) {
        if (!handle_->isMaxMinOverRange())
            ERROR("MaxMinOverRangeView requires a max/min-over-range expression.");
    }

    std::optional<MaxMinOverRangeView> MaxMinOverRangeView::tryFrom(ExprHandle handle) {
        if (!handle->isMaxMinOverRange())
            return std::nullopt;
        return MaxMinOverRangeView{handle};
    }

    SymbolAddressView MaxMinOverRangeView::range() const {
        return cast<const MaxMinOverRangeNode>(handle_.get().get())->getRange();
    }

    std::string_view MaxMinOverRangeView::indexName() const {
        return cast<const MaxMinOverRangeNode>(handle_.get().get())->getIndexName();
    }

    RangeExtremum MaxMinOverRangeView::extremum() const {
        return cast<const MaxMinOverRangeNode>(handle_.get().get())->getExtremum();
    }

    ExprHandle MaxMinOverRangeView::body() const {
        return cast<const MaxMinOverRangeNode>(handle_.get().get())->getExpr();
    }

    SourcePoint MaxMinOverRangeView::fromPoint() const {
        return cast<const MaxMinOverRangeNode>(handle_.get().get())->getFromPoint().value();
    }

    ExprHandle makeSumOverRangeHandle(ExprFactory &factory,
                                      AddrHandle range,
                                      std::string_view indexName,
                                      SourcePoint fromPoint) {
        SymbolAddressView rangeView{range};
        return detail::ExprFactoryInternals::intern(
            factory, detail::ExprFactoryInternals::makeNode<SumOverRangeNode>(
                         factory.importAddress(rangeView.handle()), indexName,
                         std::move(fromPoint)));
    }

    ExprHandle makeQuantifierOverRangeHandle(ExprFactory &factory,
                                             AddrHandle range,
                                             std::string_view indexName,
                                             RangeQuantifier quantifier,
                                             ExprHandle predicate) {
        return detail::ExprFactoryInternals::intern(
            factory,
            detail::ExprFactoryInternals::makeNode<QuantifierOverRangeNode>(
                range, indexName, quantifier, predicate));
    }

    ExprHandle makeMaxMinOverRangeHandle(ExprFactory &factory,
                                         AddrHandle range,
                                         std::string_view indexName,
                                         RangeExtremum extremum,
                                         SourcePoint fromPoint) {
        auto importedRange = factory.importAddress(range);
        auto body = makeMaxMinDefaultBody(factory, SymbolAddressView{importedRange}, indexName,
                                          fromPoint);
        return makeMaxMinOverRangeHandle(factory, importedRange, indexName, extremum, body,
                                         std::move(fromPoint));
    }

    ExprHandle makeMaxMinOverRangeHandle(ExprFactory &factory,
                                         AddrHandle range,
                                         std::string_view indexName,
                                         RangeExtremum extremum,
                                         ExprHandle body,
                                         SourcePoint fromPoint) {
        return detail::ExprFactoryInternals::intern(
            factory, detail::ExprFactoryInternals::makeNode<MaxMinOverRangeNode>(
                         range, indexName, extremum, body, std::move(fromPoint)));
    }

    SumOverRangeNode::SumOverRangeNode(AddrHandle range,
                                       std::string_view indexName,
                                       SourcePoint fromPoint,
                                       std::optional<Type> explicitType)
        : OverRangeExprNode(ExprKind::K_SumOverRange,
                            deriveType(SymbolAddressView{range}.pointeeType()),
                            range,
                            indexName,
                            explicitType),
          Symbol(Kind::K_SumOverRange),
          fromPoint_(std::move(fromPoint)) {}

    SymbolAddressView OverRangeExprNode::range() const {
        auto range = SymbolAddressView::tryFrom(range_.handle());
        if (!range)
            ERROR("Over-range expression range child must be a SymbolAddress.");
        return range.value();
    }

    std::string OverRangeExprNode::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        oss << "{" + key("range: ") + range().handle().dump() + "}, ";
        oss << "{" + key("index name: ") + accent(indexName_) + "}";
        return oss.str();
    }

    bool OverRangeExprNode::equal(const SymbolicExpr &other) const {
        auto ORE = dyn_cast<const OverRangeExprNode>(&other);
        if (ORE == nullptr)
            return false;
        if (getValType() != other.getValType())
            return false;
        if (!range().handle()->equal(*ORE->range().handle()))
            return false;
        // No indexName_.
        return true;
    }

    std::size_t OverRangeExprNode::hash() const {
        return utils::hash_val(range().handle().hash());
    }

    std::string RangeIndexNode::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        oss << type("RangeIndex") << " {" << lit(name_) << "}";
        return oss.str();
    }

    bool RangeIndexNode::equal(const SymbolicExpr &other) const {
        auto index = dyn_cast<const RangeIndexNode>(&other);
        if (!index)
            return false;
        if (getValType() != other.getValType())
            return false;

        // `name_` does not determine equality.
        return true;
    }

    std::size_t RangeIndexNode::hash() const { return utils::hash_val(getKind()); }

    std::string SumOverRangeNode::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        oss << type("SumOverRange ");
        oss << OverRangeExprNode::dump();
        return oss.str();
    }

    bool SumOverRangeNode::equal(const SymbolicExpr &other) const {
        auto SOR = dyn_cast<const SumOverRangeNode>(&other);
        if (SOR == nullptr)
            return false;
        return OverRangeExprNode::equal(*SOR) && fromPoint_ == SOR->fromPoint_;
    }

    std::size_t SumOverRangeNode::hash() const {
        return utils::hash_val(SymbolicExpr::getKind(), OverRangeExprNode::hash(), fromPoint_.hash());
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> SumOverRangeNode::doGetACSL(
        const GetACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned,
        bool) const {
        auto st = spec_generator::StringTemplate{
            "\\sum(integer ${i} = ${0}; ${i} < ${n}; ${i}++, ${prefix}${a}[${i}]${suffix})"};

        // Lower bound of a range is always zero for now.
        auto zeroStr = callGetACSL(*range().offset(), config, usedPoints, currentPoint,
                                   getPrecedence(Operator::Assign), true);
        if (!zeroStr)
            return zeroStr.error();

        auto rightBound = range().rightBound();
        if (rightBound == std::nullopt)
            UNREACHABLE();
        auto nStr = callGetACSL(*rightBound.value(), config, usedPoints, currentPoint,
                                getPrecedence(Operator::LessThan), true);
        if (!nStr)
            return nStr.error();

        auto rangeFrom = range().from();
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

    std::string QuantifierOverRangeNode::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        oss << type("QuantifierOverRange ");
        switch (quant_) {
            case RangeQuantifier::Exist: oss << accent("Exists"); break;
            case RangeQuantifier::ForAll: oss << accent("ForAll"); break;
            default: UNREACHABLE();
        }
        oss << OverRangeExprNode::dump() << ", ";
        oss << "{" << key("predicate: ") << pred_->dump() << "}";
        return oss.str();
    }

    bool QuantifierOverRangeNode::equal(const SymbolicExpr &other) const {
        auto QOV = dyn_cast<const QuantifierOverRangeNode>(&other);
        if (QOV == nullptr)
            return false;
        return OverRangeExprNode::equal(*QOV) && quant_ == QOV->quant_ && *pred_ == *QOV->pred_;
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> QuantifierOverRangeNode::doGetACSL(
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
            using enum RangeQuantifier;
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

        auto zero = simplifiedExprHandle(factory, *range().offset());
        auto zeroStr = callGetACSL(*zero, config, usedPoints, currentPoint,
                                   getPrecedence(Operator::LessThan), false);
        if (!zeroStr)
            return zeroStr.error();

        auto rightBound = range().rightBound();
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

    std::string MaxMinOverRangeNode::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        oss << type("MaxMinOverRange ");
        switch (extremum_) {
            case RangeExtremum::Max: oss << accent("Max"); break;
            case RangeExtremum::Min: oss << accent("Min"); break;
            default: UNREACHABLE();
        }
        oss << OverRangeExprNode::dump() << ", ";
        oss << "{" << key("expr: ") << expr_->dump() << "}";
        return oss.str();
    }

    bool MaxMinOverRangeNode::equal(const SymbolicExpr &other) const {
        auto MMOR = dyn_cast<const MaxMinOverRangeNode>(&other);
        if (MMOR == nullptr)
            return false;
        return OverRangeExprNode::equal(*MMOR) && extremum_ == MMOR->extremum_ &&
               *expr_ == *MMOR->expr_ && fromPoint_ == MMOR->fromPoint_;
    }

    utils::expected<std::string, SymbolicExpr::GetACSLError> MaxMinOverRangeNode::doGetACSL(
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

        auto rightBound = range().rightBound();
        if (rightBound == std::nullopt)
            UNREACHABLE();

        auto &factory = ExprFactoryScope::current();
        auto [prefix, suffix] =
            details::getPrefixSuffixAndUpdateMap(config, usedPoints, currentPoint, fromPoint_);

        auto lower = simplifiedExprHandle(factory, *range().offset());
        auto lowerStr = callGetACSL(*lower, config, usedPoints, fromPoint_,
                                    getPrecedence(Operator::LessEqual), false);
        if (!lowerStr)
            return lowerStr.error();

        auto upper = simplifiedExprHandle(factory, *rightBound.value());
        auto upperStr = callGetACSL(*upper, config, usedPoints, fromPoint_,
                                    getPrecedence(Operator::LessThan), true);
        if (!upperStr)
            return upperStr.error();

        auto cmpOp   = extremum_ == RangeExtremum::Max ? Operator::GreaterEqual : Operator::LessEqual;
        auto expr    = simplifiedExprHandle(factory, *expr_);
        auto exprStr = callGetACSL(*expr, config, usedPoints, fromPoint_,
                                   getPrecedence(cmpOp), true);
        if (!exprStr)
            return exprStr.error();

        auto exprVal = (!prefix.empty() || !suffix.empty()) ? prefix + exprStr.value() + suffix
                                                            : exprStr.value();

        std::string cmpStr = extremum_ == RangeExtremum::Max ? ">=" : "<=";

        return tmpl.to_string({{"i", indexName_},
                               {"l", lowerStr.value()},
                               {"u", upperStr.value()},
                               {"cmp", cmpStr},
                               {"expr", exprVal}});
    }

} // namespace acslg::analyzer::symbolic
