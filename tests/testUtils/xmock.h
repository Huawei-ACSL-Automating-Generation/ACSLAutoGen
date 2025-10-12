// tests/testUtils/xmock.cpp

// Enables mocking of non-virtual member functions compatible with GoogleMock.
// This macro leverages GoogleMock and hook techniques to detour non-virtual methods
// to their mock implementations, allowing the use of EXPECT_CALL and related GMock features.
// Note: Linux x86_64 only.

#ifndef XMOCK_H
#define XMOCK_H
#include <gmock/gmock.h>
#include <sys/mman.h>
#include <unistd.h>

namespace acslg::test::utils {
#define MOCK_NONVIRTUAL_METHOD(_Ret, _MethodName, _Args, _Spec, _Class)                            \
    GMOCK_INTERNAL_WARNING_PUSH()                                                                  \
    GMOCK_INTERNAL_WARNING_CLANG(ignored, "-Wunused-member-function")                              \
    XMOCK_INTERNAL_MOCK_METHOD(_Ret, _MethodName, _Args, _Spec, _Class)                            \
    GMOCK_INTERNAL_WARNING_POP()

#define XMOCK_INTERNAL_MOCK_METHOD(_Ret, _MethodName, _Args, _Spec, _Class)                        \
    GMOCK_INTERNAL_ASSERT_PARENTHESIS(_Args);                                                      \
    GMOCK_INTERNAL_ASSERT_PARENTHESIS(_Spec);                                                      \
    GMOCK_INTERNAL_ASSERT_VALID_SIGNATURE(GMOCK_PP_NARG0 _Args,                                    \
                                          GMOCK_INTERNAL_SIGNATURE(_Ret, _Args));                  \
    GMOCK_INTERNAL_ASSERT_VALID_SPEC(_Spec)                                                        \
    XMOCK_INTERNAL_MOCK_METHOD_IMPL(                                                               \
        GMOCK_PP_NARG0 _Args, _MethodName, GMOCK_INTERNAL_HAS_CONST(_Spec),                        \
        GMOCK_INTERNAL_HAS_OVERRIDE(_Spec), GMOCK_INTERNAL_HAS_FINAL(_Spec),                       \
        GMOCK_INTERNAL_GET_NOEXCEPT_SPEC(_Spec), GMOCK_INTERNAL_GET_CALLTYPE_SPEC(_Spec),          \
        GMOCK_INTERNAL_GET_REF_SPEC(_Spec), (GMOCK_INTERNAL_SIGNATURE(_Ret, _Args)), _Class)

#define XMOCK_INTERNAL_MOCK_METHOD_IMPL(_N, _MethodName, _Constness, _Override, _Final,            \
                                        _NoexceptSpec, _CallType, _RefSpec, _Signature, _Class)    \
    ::testing::MockSpec<GMOCK_PP_REMOVE_PARENS(_Signature)> gmock_##_MethodName(                   \
        GMOCK_PP_REPEAT(GMOCK_INTERNAL_MATCHER_PARAMETER, _Signature, _N))                         \
        GMOCK_PP_IF(_Constness, const, ) _RefSpec {                                                \
        GMOCK_MOCKER_(_N, _Constness, _MethodName).RegisterOwner(this);                            \
        return GMOCK_MOCKER_(_N, _Constness, _MethodName)                                          \
            .With(GMOCK_PP_REPEAT(GMOCK_INTERNAL_MATCHER_ARGUMENT, , _N));                         \
    }                                                                                              \
    ::testing::MockSpec<GMOCK_PP_REMOVE_PARENS(_Signature)> gmock_##_MethodName(                   \
        const ::testing::internal::WithoutMatchers &,                                              \
        GMOCK_PP_IF(_Constness,                                                                    \
                    const, )::testing::internal::Function<GMOCK_PP_REMOVE_PARENS(_Signature)> *)   \
        const _RefSpec _NoexceptSpec {                                                             \
        return ::testing::internal::ThisRefAdjuster<GMOCK_PP_IF(                                   \
            _Constness, const, ) int _RefSpec>::Adjust(*this)                                      \
            .gmock_##_MethodName(                                                                  \
                GMOCK_PP_REPEAT(GMOCK_INTERNAL_A_MATCHER_ARGUMENT, _Signature, _N));               \
    }                                                                                              \
    mutable ::testing::FunctionMocker<GMOCK_PP_REMOVE_PARENS(_Signature)> GMOCK_MOCKER_(           \
        _N, _Constness, _MethodName);                                                              \
    typename ::testing::internal::Function<GMOCK_PP_REMOVE_PARENS(                                 \
        _Signature)>::Result static GMOCK_INTERNAL_EXPAND(_CallType)                               \
        _stub_##_MethodName(GMOCK_PP_IF(_Constness, const, ) _Class *self,                         \
                            GMOCK_PP_REPEAT(GMOCK_INTERNAL_PARAMETER, _Signature, _N))             \
            GMOCK_PP_IF(_Constness, const, ) _RefSpec _NoexceptSpec                                \
            GMOCK_PP_IF(_Override, override, ) GMOCK_PP_IF(_Final, final, ) {                      \
        self->GMOCK_MOCKER_(_N, _Constness, _MethodName).SetOwnerAndName(self, #_MethodName);      \
        return self->GMOCK_MOCKER_(_N, _Constness, _MethodName)                                    \
            .Invoke(GMOCK_PP_REPEAT(GMOCK_INTERNAL_FORWARD_ARG, _Signature, _N));                  \
    }                                                                                              \
    struct _HookGuard_##_MethodName {                                                              \
        unsigned char savedBytes[5];                                                               \
        void *targetAddr;                                                                          \
        _HookGuard_##_MethodName() {                                                               \
            targetAddr        = getMemberFunctionAddress(&_Class::_MethodName);                    \
            size_t pg         = sysconf(_SC_PAGESIZE);                                             \
            uintptr_t pgStart = (uintptr_t)targetAddr & ~(pg - 1);                                 \
            mprotect((void *)pgStart, pg, PROT_READ | PROT_WRITE | PROT_EXEC);                     \
            memcpy(savedBytes, targetAddr, 5);                                                     \
            uintptr_t dest           = (uintptr_t)&_stub_##_MethodName;                            \
            int32_t relOffset        = (int32_t)(dest - ((uintptr_t)targetAddr + 5));              \
            unsigned char jmpCode[5] = {0xE9};                                                     \
            memcpy(jmpCode + 1, &relOffset, 4);                                                    \
            memcpy(targetAddr, jmpCode, 5);                                                        \
            mprotect((void *)pgStart, pg, PROT_READ | PROT_EXEC);                                  \
        }                                                                                          \
        ~_HookGuard_##_MethodName() {                                                              \
            size_t pg         = sysconf(_SC_PAGESIZE);                                             \
            uintptr_t pgStart = (uintptr_t)targetAddr & ~(pg - 1);                                 \
            mprotect((void *)pgStart, pg, PROT_READ | PROT_WRITE | PROT_EXEC);                     \
            memcpy(targetAddr, savedBytes, 5);                                                     \
            mprotect((void *)pgStart, pg, PROT_READ | PROT_EXEC);                                  \
        }                                                                                          \
    } _hook_guard_##_MethodName;

    namespace {
        template <typename T> void *getMemberFunctionAddress(T memberFunctionPtr) {
            union {
                T mfp;
                void *addr;
            } converter;
            converter.mfp = memberFunctionPtr;
            return converter.addr;
        }
    } // namespace
} // namespace acslg::test::utils

#endif