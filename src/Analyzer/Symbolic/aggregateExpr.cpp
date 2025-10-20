#include "aggregateExpr.h"

#include <llvm-19/llvm/Support/Casting.h>
#include <memory>

#include "expr.h"
#include "utils.h"
#include "stringTemplate.h"

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

    utils::not_null<std::unique_ptr<SymbolicExpr>> OverRangeExpr::RangeIndex::clone() const {
        return std::make_unique<RangeIndex>(*this);
    };

    std::string OverRangeExpr::RangeIndex::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        oss << type("RangeIndex") << " {" << name_ << "}";
        return oss.str();
    }

    std::optional<std::string> OverRangeExpr::RangeIndex::regularForm(
        std::optional<std::string_view>,
        std::optional<std::string_view>,
        int,
        bool) const {
        return name_;
    }

    bool OverRangeExpr::RangeIndex::equal(const SymbolicExpr &other) const {
        auto index = llvm::dyn_cast<const OverRangeExpr::RangeIndex>(&other);
        if (!index)
            return false;

        // `RangeIndex` is just a placeholder and does not determine equality.
        return true;
    }

    std::size_t OverRangeExpr::RangeIndex::hash() const { return utils::hash_val(getKind()); }

    std::string SumOverRange::dump() const {
        using namespace utils::dump_fmt;
        std::ostringstream oss;
        oss << type("SumOverRange ");
        oss << OverRangeExpr::dump();
        return oss.str();
    }

    std::optional<std::string> SumOverRange::regularForm(std::optional<std::string_view> prefix,
                                                         std::optional<std::string_view> suffix,
                                                         int,
                                                         bool) const {
        auto st =
            spec_generator::StringTemplate{"\\sum(integer {i} = {0}; {i} < {n}; {i}++, {a}[{i}])"};
        auto zeroStr =
            range_->getOffset()->regularForm(prefix, suffix, /*assign's prec:*/ 10, true);
        if (zeroStr == std::nullopt)
            return std::nullopt;
        auto &len = range_->getLength();
        if (len == std::nullopt)
            UNREACHABLE();
        auto nStr = len.value()->regularForm(prefix, suffix, /*less's prec:*/ 60, true);
        if (nStr == std::nullopt)
            return std::nullopt;
        auto aStr = range_->regularFormOfBase(prefix, suffix);
        if (aStr == std::nullopt)
            return std::nullopt;
        return st.to_string(
            {{"i", indexName_}, {"n", nStr.value()}, {"a", aStr.value()}, {"0", zeroStr.value()}});
    }

} // namespace acslg::analyzer::symbolic