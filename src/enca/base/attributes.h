#ifndef ENCA_ATTRIBUTES_H
#define ENCA_ATTRIBUTES_H

#include "types.h"

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
# define ENCA_C23 1
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201710L && defined(__clang__) && defined(__GNUC__) && __GNUC__ >= 4
# define ENCA_C23_PREVIEW 1
#endif

#if defined(__cplusplus)
# define ENCA_INLINE inline
#else
# define ENCA_INLINE static inline
#endif

#if ENCA_C23 || defined(ENCA_C23_PREVIEW)
# define ENCA_NODISCARD [[nodiscard]]
# define ENCA_UNUSED [[maybe_unused]]
# define ENCA_FALLTHROUGH [[fallthrough]]
#elif defined(__GNUC__)
# define ENCA_NODISCARD __attribute__((warn_unused_result))
# define ENCA_UNUSED __attribute__((unused))
# define ENCA_FALLTHROUGH __attribute__((fallthrough))
#elif defined(_MSC_VER)
# define ENCA_NODISCARD _Check_return_
# define ENCA_UNUSED
# define ENCA_FALLTHROUGH
#else
# define ENCA_NODISCARD
# define ENCA_UNUSED
# define ENCA_FALLTHROUGH
#endif

#if defined(__GNUC__)
# define ENCA_LIKELY(x) (__builtin_expect (!!(x), 1))
# define ENCA_UNLIKELY(x) (__builtin_expect (!!(x), 0))
#elif defined(_MSC_VER)
# define ENCA_LIKELY(x) (x)
# define ENCA_UNLIKELY(x) (x)
#else
# define ENCA_LIKELY(x) (x)
# define ENCA_UNLIKELY(x) (x)
#endif

#if defined(__GNUC__)
# define ENCA_FORCE_INLINE ENCA_INLINE __attribute__((always_inline))
# define ENCA_NOINLINE __attribute__((noinline))
# define ENCA_PRINTF(fmt_idx, va_idx) __attribute__((format (printf, fmt_idx, va_idx)))
#elif defined(_MSC_VER)
# define ENCA_FORCE_INLINE static __forceinline
# define ENCA_NOINLINE __declspec(noinline)
# define ENCA_PRINTF(fmt_idx, va_idx)
#else
# define ENCA_FORCE_INLINE ENCA_INLINE
# define ENCA_NOINLINE
# define ENCA_PRINTF(fmt_idx, va_idx)
#endif

#define ENCA_ALIGNED(n) _Alignas(n)

#if ENCA_C23
# define ENCA_THREAD_LOCAL thread_local
#elif __STDC_VERSION__ >= 201112L
# define ENCA_THREAD_LOCAL _Thread_local
#elif defined(__GNUC__)
# define ENCA_THREAD_LOCAL __thread
#elif defined(_MSC_VER)
# define ENCA_THREAD_LOCAL __declspec(thread)
#else
# error "no thread-local storage support"
#endif

#if defined(__GNUC__)
# define ENCA_UNREACHABLE() __builtin_unreachable ()
#elif defined(_MSC_VER)
# define ENCA_UNREACHABLE() __assume (0)
#else
# define ENCA_UNREACHABLE() abort ()
#endif

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
# define ENCA_ALIGNOF(t) alignof (t)
#else
# define ENCA_ALIGNOF(t) _Alignof (t)
#endif

#endif
