#include "aggregateExpr.h"

#include <llvm-19/llvm/Support/Casting.h>
#include <memory>
#include <optional>

#include "expr.h"
#include "macros.h"
#include "utils.h"
#include "stringTemplate.h"
#include "Analyzer/state.h"

namespace acslg::analyzer::symbolic {
    OverRangeExpr::OverRangeExpr(const OverRangeExpr &other)
        : SymbolicExpr(other), range_(std::make_unique<SymbolAddress>(*other.range_)),
          indexName_(other.indexName_), fromPoint_(other.fromPoint_) {}

    OverRangeExpr &OverRangeExpr::operator=(const OverRangeExpr &other) {
        if (&other == this)
            return *this;
        SymbolicExpr::operator=(other);
        range_     = std::make_unique<SymbolAddress>(*other.range_);
        indexName_ = other.indexName_;
        fromPoint_ = other.fromPoint_;
        return *this;
    }

    std::string OverRangeExpr::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        oss << "{" + key("range: ") + range_->dump() + "}, ";
        oss << "{" + key("index name: ") + accent(indexName_) + "}, ";
        oss << "{" + key("from point: ") + fromPoint_.dump() + "}";
        return oss.str();
    }

    bool OverRangeExpr::equal(const SymbolicExpr &other) const {
        auto ORE = llvm::dyn_cast<const OverRangeExpr>(&other);
        if (ORE == nullptr)
            return false;
        if (*range_ != *ORE->range_)
            return false;
        // No indexName_.
        if (fromPoint_ != ORE->fromPoint_)
            return false;
        return true;
    }

    std::size_t OverRangeExpr::hash() const {
        return utils::hash_val(range_->hash(), fromPoint_.hash());
    }

    OverRangeExpr::RangeElement &OverRangeExpr::RangeElement::operator=(
        const OverRangeExpr::RangeElement &other) {
        if (&other == this)
            return *this;
        SymbolicExpr::operator=(other);
        indexName_ = other.indexName_;
        range_     = std::make_unique<SymbolAddress>(*other.range_);
        return *this;
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> OverRangeExpr::RangeElement::clone() const {
        return std::make_unique<RangeElement>(*this);
    };

    std::string OverRangeExpr::RangeElement::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        oss << type("RangeElement ");
        oss << "{" << key("index name: ") << indexName_ << "}, ";
        oss << "{" << key("range info: ") << range_->dump() << "}";
        return oss.str();
    }

    bool OverRangeExpr::RangeElement::equal(const SymbolicExpr &other) const {
        auto index = llvm::dyn_cast<const OverRangeExpr::RangeElement>(&other);
        if (!index)
            return false;

        // `RangeElement` is just a placeholder and does not determine equality.
        return true;
    }

    std::size_t OverRangeExpr::RangeElement::hash() const {
        // `RangeElement` is just a placeholder.
        return utils::hash_val(getKind());
    }

    utils::not_null<std::unique_ptr<SymbolicExpr>> OverRangeExpr::RangeElement::getSubstitutedExpr(
        const Path &pathSubTo,
        const SourcePoint &pointToSub) const {
        auto subedExpr  = range_->getSubstitutedExpr(pathSubTo, pointToSub);
        auto subedRange = llvm::dyn_cast<SymbolAddress>(subedExpr.get().get());
        if (subedRange == nullptr || subedRange->getLength() == std::nullopt)
            ERROR("Substituted expression should be a *range*");
        return std::make_unique<RangeElement>(indexName_,
                                              std::make_unique<const SymbolAddress>(*subedRange));
    }

    std::string SumOverRange::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        oss << type("SumOverRange ");
        oss << OverRangeExpr::dump();
        return oss.str();
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

    utils::not_null<std::unique_ptr<SymbolicExpr>> QuantifierOverRange::getSubstitutedExpr(
        const Path &pathSubTo,
        const SourcePoint &pointToSub) const {
        if (fromPoint_ != pointToSub)
            return clone();
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

    QuantifierOverRange &QuantifierOverRange::operator=(const QuantifierOverRange &other) {
        if (&other == this)
            return *this;
        OverRangeExpr::operator=(other);
        pred_ = other.pred_->clone().into_underlying();
        return *this;
    }

    std::string QuantifierOverRange::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        oss << type("QuantifierOverRange ");
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

} // namespace acslg::analyzer::symbolic