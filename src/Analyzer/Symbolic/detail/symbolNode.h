#pragma once

#include "addressNode.h"

namespace acslg::analyzer::symbolic::detail {

    /// Internal mix-in for nodes that carry symbolic provenance.
    class Symbol {
      public:
        virtual ~Symbol()                 = default;
        Symbol(const Symbol &)            = delete;
        Symbol &operator=(const Symbol &) = delete;
        Symbol(Symbol &&)                 = default;
        Symbol &operator=(Symbol &&)      = delete;

        enum class Kind {
            K_Structure,
            K_SymbolAddress,
            K_SymbolValue,
            K_SumOverRange,
            K_MaxMinOverRange,
        };

        Kind getKind() const { return kind_; }
        virtual std::optional<SourcePoint> getFromPoint() const = 0;

      protected:
        explicit Symbol(Kind kind) : kind_(kind) {}

        static utils::expected<std::string, ACSLError> callGetACSLOfValueProxy(
            const AddressNode &addr,
            const ACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec = 0,
            bool isRightChild   = false);

      private:
        Kind kind_;
    };

} // namespace acslg::analyzer::symbolic::detail
