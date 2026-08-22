#ifndef ENCA_ASSERT_H
#define ENCA_ASSERT_H

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

_Noreturn void enca_panic (const char *file, int line, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#ifdef NDEBUG
# define ENCA_ASSERT(cond, msg) ((void) sizeof ((cond) ? 1 : 0))
#else
# define ENCA_ASSERT(cond, msg) \
  ((cond) ? (void) 0 : enca_panic (__FILE__, __LINE__, "%s", (msg)))
#endif

#define ENCA_ASSERT_ALWAYS(cond, msg) \
  ((cond) ? (void) 0 : enca_panic (__FILE__, __LINE__, "%s", (msg)))

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
# define ENCA_STATIC_ASSERT(cond, msg) static_assert (cond, msg)
#else
# define ENCA_STATIC_ASSERT(cond, msg) _Static_assert (cond, msg)
#endif

#endif
