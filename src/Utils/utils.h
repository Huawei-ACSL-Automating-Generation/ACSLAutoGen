#ifndef __ACSLG_SRC_UTILS_UTILS_H__
#define __ACSLG_SRC_UTILS_UTILS_H__

#include <iomanip>
#include <utility>
#include <sstream>
#include "clang/AST/RecursiveASTVisitor.h"
#include <unordered_set>
#include <variant>

namespace acslg::utils {
    bool isAssignOp(const clang::BinaryOperator *binOp);
    bool ignoreTopBinop(const clang::BinaryOperator *binOp);
    std::unordered_set<const clang::VarDecl *> collectLocalVars(const clang::Stmt *stmt);

    namespace dump_fmt {
        std::string type(std::string_view s);
        std::string key(std::string_view s);
        std::string op(std::string_view s);
        std::string lit(std::string_view s);
        std::string path(std::string_view s);
        std::string accent(std::string_view s);
        std::string hint(std::string_view s);
    } // namespace dump_fmt

    std::optional<clang::QualType> findSizeofQualType(const clang::Expr *E);

    /// Count occurrences of `sizeof(...)` within an expression subtree.
    std::size_t countSizeofInExpr(const clang::Expr *E);

    /// Count total `sizeof(...)` occurrences across all arguments of a call.
    std::size_t countSizeofInCall(const clang::CallExpr *call);

    /// Return true iff the (canonical) type is a builtin scalar (e.g., integer/bool/char).
    bool isBuiltinScalar(clang::QualType QT);

    // handy hash
    // from boost (functional/hash):
    // see http://www.boost.org/doc/libs/1_35_0/doc/html/hash/combine.html template
    // Note: Added splitmix64 to obtain a more uniform hash.
    namespace details {

        template <typename T> inline void hash_combine(std::size_t &seed, const T &val) {
            seed ^= std::hash<T>()(val) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        // auxiliary generic functions to create a hash value using a seed
        template <typename T> inline void hash_val(std::size_t &seed, const T &val) {
            hash_combine(seed, val);
        }
        template <typename T, typename... Types>
        inline void hash_val(std::size_t &seed, const T &val, const Types &...args) {
            hash_combine(seed, val);
            hash_val(seed, args...);
        }

        static inline uint64_t splitmix64(uint64_t x) {
            x += 0x9e3779b97f4a7c15ull;
            x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
            x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
            x = x ^ (x >> 31);
            return x;
        }

    } // namespace details

    template <typename... Types> inline std::size_t hash_val(const Types &...args) {
        std::size_t seed = 0;
        details::hash_val(seed, args...);
        return details::splitmix64(seed);
    }

    inline std::string hash_prefix_hex_chars(uint64_t hashValue, std::size_t xChars) {
        xChars = std::min<std::size_t>(xChars, 16);

        unsigned bitsNeeded = static_cast<unsigned>(xChars * 4);

        uint64_t shifted = (bitsNeeded == 64) ? hashValue : (hashValue >> (64 - bitsNeeded));

        std::ostringstream oss;
        oss << std::hex << std::setfill('0') << std::nouppercase;

        oss << std::setw(static_cast<int>(xChars)) << (shifted & ((1ULL << bitsNeeded) - 1));

        std::string s = oss.str();
        assert(s.size() == xChars && "Unexpected hex string length mismatch");
        return s;
    }

    struct pair_hash {
        template <class T1, class T2> std::size_t operator()(const std::pair<T1, T2> &p) const {
            return hash_val(p.first, p.second);
        }
    };

    // destructure a C++ function
    // from Fekir
    // see https://fekir.info/post/destructure-cpp-function/

    template <typename T> struct function_traits : function_traits<decltype(&T::operator())> {};

    template <typename R, typename... Args> struct function_traits<R(Args...)> {
        static constexpr std::size_t arity = sizeof...(Args);
        template <std::size_t N>
        using argn = typename std::tuple_element<N, std::tuple<Args...>>::type;

        using result = R;

#if __cplusplus >= 201703L
        static constexpr bool is_noexcept = false;
#endif
        static constexpr bool is_variadic = false;

        using ptr = R (*)(Args...);
    };

#if __cplusplus >= 201703L
    template <typename R, typename... Args>
    struct function_traits<R(Args...) noexcept> : function_traits<R(Args...)> {
        static constexpr bool is_noexcept = true;
        using ptr                         = R (*)(Args...) noexcept;
    };
#endif
    template <typename R, typename... Args>
    struct function_traits<R(Args..., ...)> : function_traits<R(Args...)> {
        static constexpr bool is_variadic = true;
        using ptr                         = R (*)(Args..., ...);
    };
#if __cplusplus >= 201703L
    template <typename R, typename... Args>
    struct function_traits<R(Args..., ...) noexcept> : function_traits<R(Args...) noexcept> {
        static constexpr bool is_noexcept = true;
        static constexpr bool is_variadic = true;
        using ptr                         = R (*)(Args..., ...) noexcept;
    };
#endif

#define F_TRAIT(NOEXCEPT)                                                                          \
    template <typename R, typename... Args>                                                        \
    struct function_traits<R (*)(Args...) NOEXCEPT> : function_traits<R(Args...) NOEXCEPT> {};     \
    template <typename R, typename... Args>                                                        \
    struct function_traits<R (*)(Args..., ...) NOEXCEPT>                                           \
        : function_traits<R(Args..., ...) NOEXCEPT> {};                                            \
    template <typename R, typename... Args>                                                        \
    struct function_traits<R (&)(Args...) NOEXCEPT> : function_traits<R(Args...) NOEXCEPT> {};     \
    template <typename R, typename... Args>                                                        \
    struct function_traits<R (&)(Args..., ...) NOEXCEPT>                                           \
        : function_traits<R(Args..., ...) NOEXCEPT> {};

    F_TRAIT(noexcept(false));
#if __cplusplus >= 201703L
    F_TRAIT(noexcept(true));
#endif
#undef F_TRAIT

#define F_AB_TRAIT_I(MOD, NOEXCEPT)                                                                \
    template <typename R, typename... Args>                                                        \
    struct function_traits<R(Args...) MOD NOEXCEPT> : function_traits<R(Args...) NOEXCEPT> {};     \
    template <typename R, typename... Args>                                                        \
    struct function_traits<R(Args..., ...) MOD NOEXCEPT> : function_traits<R(Args...) NOEXCEPT> {}

#if __cplusplus >= 201703L
#define F_AB_TRAIT(MOD)                                                                            \
    F_AB_TRAIT_I(MOD, noexcept(false));                                                            \
    F_AB_TRAIT_I(MOD, noexcept(true))
#else
#define F_AB_TRAIT(MOD) F_AB_TRAIT_I(MOD, noexcept(false));
#endif
    F_AB_TRAIT(const volatile);
    F_AB_TRAIT(const);
    F_AB_TRAIT(volatile);
    F_AB_TRAIT(const volatile &);
    F_AB_TRAIT(const &);
    F_AB_TRAIT(volatile &);
    F_AB_TRAIT(&);
    F_AB_TRAIT(const volatile &&);
    F_AB_TRAIT(const &&);
    F_AB_TRAIT(volatile &&);
    F_AB_TRAIT(&&);
#undef F_AB_TRAIT
#undef F_AB_TRAIT_I

    template <typename C, typename R, typename... Args>
    struct function_traits<R (C::*)(Args...)> : function_traits<R(Args...)> {
        using owner = C;
    };

#if __cplusplus >= 201703L
    template <typename C, typename R, typename... Args>
    struct function_traits<R (C::*)(Args...) noexcept> : function_traits<R(Args...) noexcept> {
        using owner = C;
    };
#endif
    template <typename C, typename R, typename... Args>
    struct function_traits<R (C::*)(Args..., ...)> : function_traits<R(Args..., ...)> {
        using owner = C;
    };
#if __cplusplus >= 201703L
    template <typename C, typename R, typename... Args>
    struct function_traits<R (C::*)(Args..., ...) noexcept>
        : function_traits<R(Args..., ...) noexcept> {
        using owner = C;
    };
#endif

#define MF_TRAIT_I(MOD, NOEXCEPT)                                                                  \
    template <typename C, typename R, typename... Args>                                            \
    struct function_traits<R (C::*)(Args...) MOD NOEXCEPT>                                         \
        : function_traits<R(Args...) NOEXCEPT> {                                                   \
        using owner = C MOD;                                                                       \
    };                                                                                             \
    template <typename C, typename R, typename... Args>                                            \
    struct function_traits<R (C::*)(Args..., ...) MOD NOEXCEPT>                                    \
        : function_traits<R(Args..., ...) NOEXCEPT> {                                              \
        using owner = C MOD;                                                                       \
    }

#if __cplusplus >= 201703L
#define MF_TRAIT(MOD)                                                                              \
    MF_TRAIT_I(MOD, noexcept(false));                                                              \
    MF_TRAIT_I(MOD, noexcept(true))
#else
#define MF_TRAIT(MOD) MF_TRAIT_I(MOD, noexcept(false))
#endif

    MF_TRAIT(const);
    MF_TRAIT(&);
    MF_TRAIT(const &);
    MF_TRAIT(&&);
    MF_TRAIT(const &&);
    MF_TRAIT(volatile);
    MF_TRAIT(const volatile);
    MF_TRAIT(volatile &);
    MF_TRAIT(const volatile &);
    MF_TRAIT(volatile &&);
    MF_TRAIT(const volatile &&);
#undef MF_TRAIT

    namespace {
        // tests:

        using T1 = int (*)(...);
        using T2 = void(int);
        using T3 = void (&)(int);
#if __cplusplus >= 201703L
        using T4 = void(int) noexcept;
#endif

        static_assert(std::is_same<function_traits<T1>::result, int>::value, "");
        static_assert(std::is_same<function_traits<T2>::result, void>::value, "");
        static_assert(std::is_same<function_traits<T3>::result, void>::value, "");

        static_assert(function_traits<T1>::arity == 0, "");
        static_assert(function_traits<T2>::arity == 1, "");

        static_assert(std::is_same<function_traits<T2>::argn<0>, int>::value, "");
        static_assert(std::is_same<function_traits<T3>::argn<0>, int>::value, "");

        static_assert(std::is_same<function_traits<T1>::ptr, T1>::value, "");
        static_assert(std::is_same<function_traits<T2>::ptr, T2 *>::value, "");
        static_assert(std::is_same<function_traits<T3>::ptr, T2 *>::value, "");

#if __cplusplus >= 201703L
        static_assert(!function_traits<T1>::is_noexcept);
        static_assert(function_traits<T4>::is_noexcept);
#endif

        struct fwd;
#if __cplusplus >= 201703L
        using H1 = int (fwd::*)() noexcept;
#endif
        using H2 = int (fwd::*)() const;
        using H3 = int &(fwd::*)();
        using H4 = void (fwd::*)(int);
        using H5 = void (fwd::*)(int, bool, char *);
        using H6 = int (fwd::*)(...) volatile;

#if __cplusplus >= 201703L
        static_assert(function_traits<H1>::is_noexcept);
        static_assert(!function_traits<H2>::is_noexcept);
#endif

        static_assert(std::is_same<function_traits<H2>::result, int>::value, "");
        static_assert(std::is_same<function_traits<H3>::result, int &>::value, "");
        static_assert(std::is_same<function_traits<H4>::result, void>::value, "");
        static_assert(std::is_same<function_traits<H6>::result, int>::value, "");

        static_assert(function_traits<H2>::arity == 0, "");
        static_assert(function_traits<H4>::arity == 1, "");

        static_assert(std::is_same<function_traits<H4>::argn<0>, int>::value, "");
        static_assert(std::is_same<function_traits<H5>::argn<2>, char *>::value, "");

        static_assert(std::is_same<function_traits<H2>::owner, const fwd>::value, "");
        static_assert(std::is_same<function_traits<H3>::owner, fwd>::value, "");

#if __cplusplus >= 201703L
        static_assert(std::is_same<function_traits<H1>::ptr, int (*)() noexcept>::value);
#endif
        static_assert(std::is_same<function_traits<H2>::ptr, int (*)()>::value, "");

        auto l = [] { return 42; };
        static_assert(function_traits<decltype(l)>::arity == 0, "");

        struct s {
            int operator()(bool);
        };
        static_assert(function_traits<s>::arity == 1, "");
    } // namespace

    // not_null wrapper
    // from micosoft
    // see https://github.com/microsoft/GSL/blob/main/include/gsl/pointers
    // NOTE: allow move constructor
    namespace details {
        template <typename T, typename = void> struct is_comparable_to_nullptr : std::false_type {};

        template <typename T>
        struct is_comparable_to_nullptr<
            T,
            std::enable_if_t<std::is_convertible<decltype(std::declval<T>() != nullptr), bool>::value>>
            : std::true_type {};

        // Resolves to the more efficient of `const T` or `const T&`, in the context of returning a
        // const-qualified value of type T.
        //
        // Copied from cppfront's implementation of the CppCoreGuidelines F.16
        // (https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rf-in)
        template <typename T>
        using value_or_reference_return_t =
            std::conditional_t<sizeof(T) <= 2 * sizeof(void *) &&
                                   std::is_trivially_copy_constructible<T>::value,
                               const T,
                               const T &>;

    } // namespace details

    template <class T> class not_null {
      public:
        static_assert(details::is_comparable_to_nullptr<T>::value,
                      "T cannot be compared to nullptr.");

        template <typename U, typename = std::enable_if_t<std::is_convertible<U, T>::value>>
        constexpr not_null(U &&u) : ptr_(std::forward<U>(u)) {
            assert(ptr_ != nullptr);
        }

        template <typename = std::enable_if_t<!std::is_same<std::nullptr_t, T>::value>>
        constexpr not_null(T u) : ptr_(std::move(u)) {
            assert(ptr_ != nullptr);
        }

        template <typename U, typename = std::enable_if_t<std::is_convertible<U, T>::value>>
        constexpr not_null(const not_null<U> &other) : not_null(other.get()) {}

        not_null(const not_null &other)            = default;
        not_null &operator=(const not_null &other) = default;
        not_null(not_null &&other)                 = default;
        not_null &operator=(not_null &&other)      = default;

        template <typename U, typename = std::enable_if_t<std::is_convertible<U, T>::value>>
        not_null &operator=(U &&u) {
            assert(u != nullptr);
            ptr_ = std::forward<U>(u);
            return *this;
        }

        constexpr details::value_or_reference_return_t<T> get() const
        // noexcept(noexcept(details::value_or_reference_return_t<T>{std::declval<T &>()}))
        {
            return ptr_;
        }

        constexpr operator T() const { return get(); }
        constexpr decltype(auto) operator->() const { return get(); }
        constexpr decltype(auto) operator*() const { return *get(); }

        [[nodiscard]]
        T into_underlying() && noexcept(std::is_nothrow_move_constructible_v<T>) {
            static_assert(std::is_move_constructible_v<T>,
                          "T must be move-constructible for into_underlying()");
            return std::move(ptr_);
        }

        // prevents compilation when someone attempts to assign a null pointer constant
        not_null(std::nullptr_t)            = delete;
        not_null &operator=(std::nullptr_t) = delete;

        // unwanted operators...pointers only point to single objects!
        not_null &operator++()                = delete;
        not_null &operator--()                = delete;
        not_null operator++(int)              = delete;
        not_null operator--(int)              = delete;
        not_null &operator+=(std::ptrdiff_t)  = delete;
        not_null &operator-=(std::ptrdiff_t)  = delete;
        void operator[](std::ptrdiff_t) const = delete;

      private:
        T ptr_;
    };

    // A simple `expected` similar to `std::expected` in C++23.
    template <typename T, typename E> class expected {
      public:
        constexpr expected(const T &value) noexcept(std::is_nothrow_copy_constructible_v<T>)
            : data_(value) {}
        constexpr expected(T &&value) noexcept(std::is_nothrow_move_constructible_v<T>)
            : data_(std::move(value)) {}

        constexpr expected(E error) noexcept(std::is_nothrow_copy_constructible_v<E>)
            : data_(std::in_place_index<1>, error) {}

        [[nodiscard]] constexpr bool has_value() const noexcept { return data_.index() == 0; }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return has_value(); }

        constexpr T &value() & {
            assert(has_value());
            return std::get<0>(data_);
        }
        constexpr const T &value() const & {
            assert(has_value());
            return std::get<0>(data_);
        }
        constexpr T &&value() && {
            assert(has_value());
            return std::get<0>(std::move(data_));
        }

        constexpr E error() const noexcept {
            assert(!has_value());
            return std::get<1>(data_);
        }

      private:
        std::variant<T, E> data_;
    };

    template <typename E> class expected<void, E> {
      public:
        constexpr expected() noexcept : has_(true) {}
        constexpr expected(E error) noexcept : has_(false), err_(error) {}

        [[nodiscard]] constexpr bool has_value() const noexcept { return has_; }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return has_; }

        constexpr void value() const { assert(has_); }
        constexpr E error() const noexcept {
            assert(!has_);
            return err_;
        }

      private:
        bool has_;
        E err_{};
    };

    struct TransparentStringHash {
        using is_transparent           = void;
        using is_transparent_key_equal = void;

        size_t operator()(std::string_view sv) const noexcept {
            return std::hash<std::string_view>{}(sv);
        }
    };

    struct TransparentStringEqual {
        using is_transparent = void;
        bool operator()(std::string_view a, std::string_view b) const noexcept { return a == b; }
    };

    template <typename StmtType, typename CallbackType>
    class StmtVisitor : public clang::RecursiveASTVisitor<StmtVisitor<StmtType, CallbackType>> {
      public:
        StmtVisitor() = delete;
        explicit StmtVisitor(CallbackType &&cb) : callback_(std::forward<CallbackType>(cb)) {}

        bool VisitStmt(clang::Stmt *s) {
            if (!s)
                return true;
            if (auto *node = llvm::dyn_cast<StmtType>(s)) {
                if constexpr (std::is_pointer_v<Param0Type>) {
                    callback_(node);
                } else {
                    callback_(*node);
                }
            }
            return true;
        }

        void runOn(const clang::Stmt *root) { this->TraverseStmt(const_cast<clang::Stmt *>(root)); }

      private:
        CallbackType callback_;
        using CallbackTraits = function_traits<std::decay_t<CallbackType>>;
        using Param0Type     = typename CallbackTraits::template argn<0>;
    };

    template <typename CallbackT,
              typename Arg = typename function_traits<std::decay_t<CallbackT>>::template argn<0>>
    StmtVisitor(CallbackT)
        -> StmtVisitor<std::remove_const_t<std::remove_pointer_t<std::remove_reference_t<Arg>>>,
                       CallbackT>;
} // namespace acslg::utils

#endif // __ACSLG_SRC_UTILS_UTILS_H__