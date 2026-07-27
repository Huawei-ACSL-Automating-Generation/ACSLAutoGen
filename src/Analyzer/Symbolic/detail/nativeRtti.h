#pragma once

#include <memory>
#include <type_traits>

#include "macros.h"

namespace acslg::analyzer::symbolic::detail {

    template <typename To, typename From> To *dyn_cast(From *from) {
        return dynamic_cast<To *>(from);
    }

    template <typename To, typename From> const To *dyn_cast(const From *from) {
        return dynamic_cast<const To *>(from);
    }

    template <typename To, typename From>
    std::unique_ptr<To> dyn_cast(std::unique_ptr<From> &from) {
        if (auto *casted = dynamic_cast<To *>(from.get())) {
            from.release();
            return std::unique_ptr<To>(casted);
        }
        return nullptr;
    }

    template <typename To, typename From> bool isa(const From *from) {
        return from != nullptr && dyn_cast<To>(from) != nullptr;
    }

    template <typename To, typename From> bool isa(const std::unique_ptr<From> &from) {
        return isa<To>(from.get());
    }

    template <typename To, typename From>
    requires(!std::is_pointer_v<From>) bool isa(const From &from) {
        return dyn_cast<To>(&from) != nullptr;
    }

    template <typename To, typename From> To *cast(From *from) {
        auto *result = dyn_cast<To>(from);
        if (result == nullptr)
            ERROR("Invalid symbolic cast.");
        return result;
    }

    template <typename To, typename From> const To *cast(const From *from) {
        auto *result = dyn_cast<To>(from);
        if (result == nullptr)
            ERROR("Invalid symbolic cast.");
        return result;
    }

} // namespace acslg::analyzer::symbolic::detail
