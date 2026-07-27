#pragma once

#include "nodeBase.h"

namespace acslg::analyzer::symbolic::detail {

    /// Internal base for symbolic memory address nodes.
    class AddressNode : public SymbolicExprNode {
      public:
        virtual ~AddressNode()                      = default;
        AddressNode(const AddressNode &)            = delete;
        AddressNode &operator=(const AddressNode &) = delete;
        AddressNode(AddressNode &&)                 = default;
        AddressNode &operator=(AddressNode &&)      = delete;

        utils::expected<std::pair<std::string, std::unordered_set<SourcePoint>>, ACSLError> getACSLOfValue(
            const ACSLConfig &config,
            std::optional<SourcePoint> currentPoint = std::nullopt) const {
            std::unordered_set<SourcePoint> usedPoints;
            auto res = callGetACSLOfValue(*this, config, usedPoints, currentPoint);
            if (res)
                return std::pair{std::move(res.value()), std::move(usedPoints)};
            return res.error();
        }

        virtual std::optional<utils::not_null<const clang::VarDecl *>> getFromRoot() const = 0;
        virtual int getDimension() const                                                   = 0;

        const clang::QualType &getPointeeType() const { return pointeeType_; }

      protected:
        AddressNode(ExprKind kind,
                    ExprType naturalType,
                    const clang::QualType &pointeeType,
                    std::optional<ExprType> explicitType = std::nullopt)
            : SymbolicExprNode(kind, naturalType, explicitType), pointeeType_(pointeeType) {}

        static utils::expected<std::string, ACSLError> callGetACSLOfValue(
            const AddressNode &addr,
            const ACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec = 0,
            bool isRightChild   = false) {
            return addr.doGetACSLOfValue(config, usedPoints, currentPoint, parentPrec,
                                         isRightChild);
        }

      private:
        virtual utils::expected<std::string, ACSLError> doGetACSLOfValue(
            const ACSLConfig &config,
            std::unordered_set<SourcePoint> &usedPoints,
            std::optional<SourcePoint> currentPoint,
            unsigned parentPrec,
            bool isRightChild) const = 0;

      protected:
        clang::QualType pointeeType_;

      private:
        friend class Symbol;
    };

} // namespace acslg::analyzer::symbolic::detail
