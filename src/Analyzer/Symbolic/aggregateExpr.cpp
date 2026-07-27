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
#include "detail/aggregateViews.h"
#include "detail/facadeAccess.h"
#include "detail/factoryInternals.h"
#include "detail/handleInternals.h"
#include "detail/nativeRtti.h"
#include "macros.h"
#include "utils.h"
#include "stringTemplate.h"
#include "Analyzer/state.h"

namespace acslg::analyzer::symbolic {
    using detail::AddrHandle;
    using detail::dyn_cast;
    using detail::ExprHandle;
    using detail::MaxMinOverRangeNode;
    using detail::OverRangeExprNode;
    using detail::QuantifierOverRangeNode;
    using detail::RangeIndexNode;
    using detail::SumOverRangeNode;

    namespace {
        ExprHandle makeMaxMinDefaultBody(ExprFactory &factory,
                                         const SymbolAddress &range,
                                         std::string_view indexName,
                                         const SourcePoint &fromPoint) {
            auto indexedRange = range.withOffset(Expr::rangeIndex(factory, indexName));
            indexedRange = indexedRange.withoutLength();
            return detail::FacadeAccess::exprHandle(
                Expr::symbol(range.pointeeType(), indexedRange, fromPoint));
        }
    } // namespace

    namespace detail {
    SumOverRangeView::SumOverRangeView(ExprHandle handle) : handle_(handle) {
        if (!handle_.isSumOverRange())
            ERROR("SumOverRangeView requires a sum-over-range expression.");
    }

    std::optional<SumOverRangeView> SumOverRangeView::tryFrom(ExprHandle handle) {
        if (!handle.isSumOverRange())
            return std::nullopt;
        return SumOverRangeView{handle};
    }

    SymbolAddressView SumOverRangeView::range() const {
        return detail::HandleAccess::cast<SumOverRangeNode>(handle_).getRange();
    }

    std::string_view SumOverRangeView::indexName() const {
        return detail::HandleAccess::cast<SumOverRangeNode>(handle_).getIndexName();
    }

    SourcePoint SumOverRangeView::fromPoint() const {
        return detail::HandleAccess::cast<SumOverRangeNode>(handle_).getFromPoint().value();
    }

    QuantifierOverRangeView::QuantifierOverRangeView(ExprHandle handle) : handle_(handle) {
        if (!handle_.isQuantifierOverRange())
            ERROR("QuantifierOverRangeView requires a quantified range expression.");
    }

    std::optional<QuantifierOverRangeView> QuantifierOverRangeView::tryFrom(ExprHandle handle) {
        if (!handle.isQuantifierOverRange())
            return std::nullopt;
        return QuantifierOverRangeView{handle};
    }

    SymbolAddressView QuantifierOverRangeView::range() const {
        return detail::HandleAccess::cast<QuantifierOverRangeNode>(handle_).getRange();
    }

    std::string_view QuantifierOverRangeView::indexName() const {
        return detail::HandleAccess::cast<QuantifierOverRangeNode>(handle_).getIndexName();
    }

    RangeQuantifier QuantifierOverRangeView::quantifier() const {
        return detail::HandleAccess::cast<QuantifierOverRangeNode>(handle_).getQuantifier();
    }

    ExprHandle QuantifierOverRangeView::predicate() const {
        return detail::HandleAccess::cast<QuantifierOverRangeNode>(handle_).getPredicate();
    }

    MaxMinOverRangeView::MaxMinOverRangeView(ExprHandle handle) : handle_(handle) {
        if (!handle_.isMaxMinOverRange())
            ERROR("MaxMinOverRangeView requires a max/min-over-range expression.");
    }

    std::optional<MaxMinOverRangeView> MaxMinOverRangeView::tryFrom(ExprHandle handle) {
        if (!handle.isMaxMinOverRange())
            return std::nullopt;
        return MaxMinOverRangeView{handle};
    }

    SymbolAddressView MaxMinOverRangeView::range() const {
        return detail::HandleAccess::cast<MaxMinOverRangeNode>(handle_).getRange();
    }

    std::string_view MaxMinOverRangeView::indexName() const {
        return detail::HandleAccess::cast<MaxMinOverRangeNode>(handle_).getIndexName();
    }

    RangeExtremum MaxMinOverRangeView::extremum() const {
        return detail::HandleAccess::cast<MaxMinOverRangeNode>(handle_).getExtremum();
    }

    ExprHandle MaxMinOverRangeView::body() const {
        return detail::HandleAccess::cast<MaxMinOverRangeNode>(handle_).getExpr();
    }

    SourcePoint MaxMinOverRangeView::fromPoint() const {
        return detail::HandleAccess::cast<MaxMinOverRangeNode>(handle_).getFromPoint().value();
    }
    } // namespace detail

    static ExprHandle buildSumOverRange(ExprFactory &factory,
                                        AddrHandle range,
                                        std::string_view indexName,
                                        SourcePoint fromPoint) {
        return detail::ExprFactoryInternals::intern(
            factory, detail::ExprFactoryInternals::makeNode<SumOverRangeNode>(
                         detail::ExprFactoryInternals::importAddress(factory, range), indexName,
                         std::move(fromPoint)));
    }

    static ExprHandle buildQuantifierOverRange(ExprFactory &factory,
                                               AddrHandle range,
                                               std::string_view indexName,
                                               RangeQuantifier quantifier,
                                               ExprHandle predicate) {
        auto importedRange     = detail::ExprFactoryInternals::importAddress(factory, range);
        auto importedPredicate = detail::ExprFactoryInternals::importExpr(factory, predicate);
        return detail::ExprFactoryInternals::intern(
            factory, detail::ExprFactoryInternals::makeNode<QuantifierOverRangeNode>(
                         importedRange, indexName, quantifier, importedPredicate));
    }

    static ExprHandle buildMaxMinOverRange(ExprFactory &factory,
                                           AddrHandle range,
                                           std::string_view indexName,
                                           RangeExtremum extremum,
                                           ExprHandle body,
                                           SourcePoint fromPoint) {
        auto importedRange = detail::ExprFactoryInternals::importAddress(factory, range);
        auto importedBody  = detail::ExprFactoryInternals::importExpr(factory, body);
        return detail::ExprFactoryInternals::intern(
            factory, detail::ExprFactoryInternals::makeNode<MaxMinOverRangeNode>(
                         importedRange, indexName, extremum, importedBody, std::move(fromPoint)));
    }

    static ExprHandle buildMaxMinOverRange(ExprFactory &factory,
                                           AddrHandle range,
                                           std::string_view indexName,
                                           RangeExtremum extremum,
                                           SourcePoint fromPoint) {
        auto importedRange = detail::ExprFactoryInternals::importAddress(factory, range);
        auto body = makeMaxMinDefaultBody(
            factory,
            SymbolAddress{detail::FacadeAccess::makeAddress(factory, importedRange)},
            indexName,
            fromPoint);
        return buildMaxMinOverRange(factory, importedRange, indexName, extremum, body,
                                    std::move(fromPoint));
    }

    SumOverRangeExpr::SumOverRangeExpr(const Addr &range,
                                       std::string_view indexName,
                                       SourcePoint fromPoint)
        : Expr(
              range.factory(),
              buildSumOverRange(range.factory(),
                                detail::FacadeAccess::addressHandle(range),
                                indexName,
                                std::move(fromPoint))) {
    }

    SumOverRangeExpr::SumOverRangeExpr(const Expr &expression) : Expr(expression) {
        if (!detail::FacadeAccess::exprHandle(*this).isSumOverRange())
            ERROR("SumOverRangeExpr requires a sum-over-range expression.");
    }

    std::optional<SumOverRangeExpr> SumOverRangeExpr::tryFrom(const Expr &expression) {
        if (!detail::FacadeAccess::exprHandle(expression).isSumOverRange())
            return std::nullopt;
        return SumOverRangeExpr{expression};
    }

    SymbolAddress SumOverRangeExpr::range() const {
        auto handle = detail::SumOverRangeView{detail::FacadeAccess::exprHandle(*this)}
                          .range()
                          .handle();
        return SymbolAddress{detail::FacadeAccess::makeAddress(factory(), handle)};
    }

    std::string_view SumOverRangeExpr::indexName() const {
        return detail::SumOverRangeView{detail::FacadeAccess::exprHandle(*this)}.indexName();
    }

    SourcePoint SumOverRangeExpr::fromPoint() const {
        return detail::SumOverRangeView{detail::FacadeAccess::exprHandle(*this)}.fromPoint();
    }

    QuantifierOverRangeExpr::QuantifierOverRangeExpr(const Addr &range,
                                                     std::string_view indexName,
                                                     RangeQuantifier quantifier,
                                                     const Expr &predicate)
        : Expr(make(range, indexName, quantifier, predicate)) {}

    QuantifierOverRangeExpr::QuantifierOverRangeExpr(const Expr &expression) : Expr(expression) {
        if (!detail::FacadeAccess::exprHandle(*this).isQuantifierOverRange())
            ERROR("QuantifierOverRangeExpr requires a quantified range expression.");
    }

    std::optional<QuantifierOverRangeExpr> QuantifierOverRangeExpr::tryFrom(const Expr &expression) {
        if (!detail::FacadeAccess::exprHandle(expression).isQuantifierOverRange())
            return std::nullopt;
        return QuantifierOverRangeExpr{expression};
    }

    SymbolAddress QuantifierOverRangeExpr::range() const {
        auto handle = detail::QuantifierOverRangeView{detail::FacadeAccess::exprHandle(*this)}
                          .range()
                          .handle();
        return SymbolAddress{detail::FacadeAccess::makeAddress(factory(), handle)};
    }

    std::string_view QuantifierOverRangeExpr::indexName() const {
        return detail::QuantifierOverRangeView{detail::FacadeAccess::exprHandle(*this)}.indexName();
    }

    RangeQuantifier QuantifierOverRangeExpr::quantifier() const {
        return detail::QuantifierOverRangeView{detail::FacadeAccess::exprHandle(*this)}.quantifier();
    }

    Expr QuantifierOverRangeExpr::predicate() const {
        auto handle = detail::QuantifierOverRangeView{detail::FacadeAccess::exprHandle(*this)}
                          .predicate();
        return detail::FacadeAccess::makeExpr(factory(), handle);
    }

    Expr QuantifierOverRangeExpr::make(const Addr &range,
                                       std::string_view indexName,
                                       RangeQuantifier quantifier,
                                       const Expr &predicate) {
        if (&range.factory() != &predicate.factory())
            ERROR("Cannot build a quantifier from different factories.");
        return detail::FacadeAccess::makeExpr(
            range.factory(),
            buildQuantifierOverRange(range.factory(),
                                     detail::FacadeAccess::addressHandle(range),
                                     indexName,
                                     quantifier,
                                     detail::FacadeAccess::exprHandle(predicate)));
    }

    MaxMinOverRangeExpr::MaxMinOverRangeExpr(const Addr &range,
                                             std::string_view indexName,
                                             RangeExtremum extremum,
                                             SourcePoint fromPoint)
        : Expr(range.factory(),
               buildMaxMinOverRange(range.factory(),
                                    detail::FacadeAccess::addressHandle(range),
                                    indexName,
                                    extremum,
                                    std::move(fromPoint))) {}

    MaxMinOverRangeExpr::MaxMinOverRangeExpr(const Addr &range,
                                             std::string_view indexName,
                                             RangeExtremum extremum,
                                             const Expr &body,
                                             SourcePoint fromPoint)
        : Expr(make(range, indexName, extremum, body, std::move(fromPoint))) {}

    MaxMinOverRangeExpr::MaxMinOverRangeExpr(const Expr &expression) : Expr(expression) {
        if (!detail::FacadeAccess::exprHandle(*this).isMaxMinOverRange())
            ERROR("MaxMinOverRangeExpr requires a max/min-over-range expression.");
    }

    std::optional<MaxMinOverRangeExpr> MaxMinOverRangeExpr::tryFrom(const Expr &expression) {
        if (!detail::FacadeAccess::exprHandle(expression).isMaxMinOverRange())
            return std::nullopt;
        return MaxMinOverRangeExpr{expression};
    }

    SymbolAddress MaxMinOverRangeExpr::range() const {
        auto handle = detail::MaxMinOverRangeView{detail::FacadeAccess::exprHandle(*this)}
                          .range()
                          .handle();
        return SymbolAddress{detail::FacadeAccess::makeAddress(factory(), handle)};
    }

    std::string_view MaxMinOverRangeExpr::indexName() const {
        return detail::MaxMinOverRangeView{detail::FacadeAccess::exprHandle(*this)}.indexName();
    }

    RangeExtremum MaxMinOverRangeExpr::extremum() const {
        return detail::MaxMinOverRangeView{detail::FacadeAccess::exprHandle(*this)}.extremum();
    }

    Expr MaxMinOverRangeExpr::body() const {
        auto handle =
            detail::MaxMinOverRangeView{detail::FacadeAccess::exprHandle(*this)}.body();
        return detail::FacadeAccess::makeExpr(factory(), handle);
    }

    SourcePoint MaxMinOverRangeExpr::fromPoint() const {
        return detail::MaxMinOverRangeView{detail::FacadeAccess::exprHandle(*this)}.fromPoint();
    }

    Expr MaxMinOverRangeExpr::make(const Addr &range,
                                   std::string_view indexName,
                                   RangeExtremum extremum,
                                   const Expr &body,
                                   SourcePoint fromPoint) {
        if (&range.factory() != &body.factory())
            ERROR("Cannot build a max/min expression from different factories.");
        return detail::FacadeAccess::makeExpr(
            range.factory(),
            buildMaxMinOverRange(range.factory(),
                                 detail::FacadeAccess::addressHandle(range),
                                 indexName,
                                 extremum,
                                 detail::FacadeAccess::exprHandle(body),
                                 std::move(fromPoint)));
    }

    SumOverRangeNode::SumOverRangeNode(AddrHandle range,
                                       std::string_view indexName,
                                       SourcePoint fromPoint,
                                       std::optional<ExprType> explicitType)
        : OverRangeExprNode(ExprKind::K_SumOverRange,
                            deriveType(detail::SymbolAddressView{range}.pointeeType()),
                            range,
                            indexName,
                            explicitType),
          Symbol(Kind::K_SumOverRange), fromPoint_(std::move(fromPoint)) {}

    detail::SymbolAddressView OverRangeExprNode::range() const {
        auto range = detail::SymbolAddressView::tryFrom(range_);
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

    bool OverRangeExprNode::equal(const detail::SymbolicExprNode &other) const {
        auto ORE = dyn_cast<const OverRangeExprNode>(&other);
        if (ORE == nullptr)
            return false;
        if (getValType() != other.getValType())
            return false;
        if (!range().handle().structurallyEqual(ORE->range().handle()))
            return false;
        // No indexName_.
        return true;
    }

    std::size_t OverRangeExprNode::hash() const { return utils::hash_val(range().handle().hash()); }

    std::string RangeIndexNode::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        oss << type("RangeIndex") << " {" << lit(name_) << "}";
        return oss.str();
    }

    bool RangeIndexNode::equal(const detail::SymbolicExprNode &other) const {
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

    bool SumOverRangeNode::equal(const detail::SymbolicExprNode &other) const {
        auto SOR = dyn_cast<const SumOverRangeNode>(&other);
        if (SOR == nullptr)
            return false;
        return OverRangeExprNode::equal(*SOR) && fromPoint_ == SOR->fromPoint_;
    }

    std::size_t SumOverRangeNode::hash() const {
        return utils::hash_val(detail::SymbolicExprNode::getKind(), OverRangeExprNode::hash(),
                               fromPoint_.hash());
    }

    utils::expected<std::string, ACSLError> SumOverRangeNode::doGetACSL(
        const ACSLConfig &config,
        std::unordered_set<SourcePoint> &usedPoints,
        std::optional<SourcePoint> currentPoint,
        unsigned,
        bool) const {
        auto st = spec_generator::StringTemplate{
            "\\sum(integer ${i} = ${0}; ${i} < ${n}; ${i}++, ${prefix}${a}[${i}]${suffix})"};

        // Lower bound of a range is always zero for now.
        auto zeroStr = callGetACSL(detail::HandleAccess::node(range().offset()), config, usedPoints,
                                   currentPoint, getPrecedence(Operator::Assign), true);
        if (!zeroStr)
            return zeroStr.error();

        auto rightBound = range().rightBound();
        if (rightBound == std::nullopt)
            UNREACHABLE();
        auto nStr = callGetACSL(detail::HandleAccess::node(rightBound.value()), config, usedPoints,
                                currentPoint, getPrecedence(Operator::LessThan), true);
        if (!nStr)
            return nStr.error();

        auto rangeFrom = range().from();
        if (rangeFrom == std::nullopt)
            return ACSLError::HeapAddress;

        auto [prefix, suffix] =
            details::getPrefixSuffixAndUpdateMap(config, usedPoints, currentPoint, fromPoint_);
        bool hasAt = !(prefix.empty() || suffix.empty());

        // Build the pointer expression used inside the summation body.
        auto aStr = callGetACSLOfValueProxy(detail::HandleAccess::node(*rangeFrom), config,
                                            usedPoints, fromPoint_,
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
        oss << "{" << key("predicate: ") << pred_.dump() << "}";
        return oss.str();
    }

    bool QuantifierOverRangeNode::equal(const detail::SymbolicExprNode &other) const {
        auto QOV = dyn_cast<const QuantifierOverRangeNode>(&other);
        if (QOV == nullptr)
            return false;
        return OverRangeExprNode::equal(*QOV) && quant_ == QOV->quant_ &&
               pred_.structurallyEqual(QOV->pred_);
    }

    utils::expected<std::string, ACSLError> QuantifierOverRangeNode::doGetACSL(
        const ACSLConfig &config,
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

        auto zero = detail::FacadeAccess::exprHandle(
            detail::FacadeAccess::makeExpr(factory, range().offset()).simplified());
        auto zeroStr = callGetACSL(detail::HandleAccess::node(zero), config, usedPoints,
                                   currentPoint, getPrecedence(Operator::LessThan), false);
        if (!zeroStr)
            return zeroStr.error();

        auto rightBound = range().rightBound();
        if (rightBound == std::nullopt)
            UNREACHABLE();
        auto upper = detail::FacadeAccess::exprHandle(
            detail::FacadeAccess::makeExpr(factory, rightBound.value()).simplified());
        auto nStr = callGetACSL(detail::HandleAccess::node(upper), config, usedPoints, currentPoint,
                                getPrecedence(Operator::LessThan), true);
        if (!nStr)
            return nStr.error();

        auto pred = detail::FacadeAccess::exprHandle(
            detail::FacadeAccess::makeExpr(factory, pred_).simplified());
        auto predStr = callGetACSL(
            detail::HandleAccess::node(pred), config, usedPoints, currentPoint,
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
        oss << "{" << key("expr: ") << expr_.dump() << "}";
        return oss.str();
    }

    bool MaxMinOverRangeNode::equal(const detail::SymbolicExprNode &other) const {
        auto MMOR = dyn_cast<const MaxMinOverRangeNode>(&other);
        if (MMOR == nullptr)
            return false;
        return OverRangeExprNode::equal(*MMOR) && extremum_ == MMOR->extremum_ &&
               expr_.structurallyEqual(MMOR->expr_) && fromPoint_ == MMOR->fromPoint_;
    }

    utils::expected<std::string, ACSLError> MaxMinOverRangeNode::doGetACSL(
        const ACSLConfig &config,
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

        auto lower = detail::FacadeAccess::exprHandle(
            detail::FacadeAccess::makeExpr(factory, range().offset()).simplified());
        auto lowerStr = callGetACSL(detail::HandleAccess::node(lower), config, usedPoints,
                                    fromPoint_, getPrecedence(Operator::LessEqual), false);
        if (!lowerStr)
            return lowerStr.error();

        auto upper = detail::FacadeAccess::exprHandle(
            detail::FacadeAccess::makeExpr(factory, rightBound.value()).simplified());
        auto upperStr = callGetACSL(detail::HandleAccess::node(upper), config, usedPoints,
                                    fromPoint_, getPrecedence(Operator::LessThan), true);
        if (!upperStr)
            return upperStr.error();

        auto cmpOp = extremum_ == RangeExtremum::Max ? Operator::GreaterEqual : Operator::LessEqual;
        auto expr = detail::FacadeAccess::exprHandle(
            detail::FacadeAccess::makeExpr(factory, expr_).simplified());
        auto exprStr = callGetACSL(detail::HandleAccess::node(expr), config, usedPoints, fromPoint_,
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
