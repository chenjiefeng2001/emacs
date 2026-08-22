#ifndef ENCA_TEST_UTIL_H
#define ENCA_TEST_UTIL_H

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int enca_test_failures;
extern int enca_test_checks;

#if defined(_WIN32)
# define ENCA_TEST_U64F "%I64u"
#else
# define ENCA_TEST_U64F "%llu"
#endif

#if defined(__SANITIZE_ADDRESS__)
# define ENCA_TEST_ASAN 1
#elif defined(__has_feature)
# if __has_feature (address_sanitizer)
#  define ENCA_TEST_ASAN 1
# endif
#endif

#if defined(__SANITIZE_THREAD__)
# define ENCA_TEST_TSAN 1
#elif defined(__has_feature)
# if __has_feature (thread_sanitizer)
#  define ENCA_TEST_TSAN 1
# endif
#endif

#if defined(__SANITIZE_UNDEFINED__)
# define ENCA_TEST_UBSAN 1
#elif defined(__has_feature)
# if __has_feature (undefined_behavior_sanitizer)
#  define ENCA_TEST_UBSAN 1
# endif
#endif

#if defined (ENCA_TEST_ASAN) || defined (ENCA_TEST_TSAN) \
  || defined (ENCA_TEST_UBSAN)
# define ENCA_TEST_SANITIZER_RISKY 1
#endif

#define CHECK(cond) \
  do \
    { \
      enca_test_checks++; \
      if (! (cond)) \
        { \
          printf ("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
          enca_test_failures++; \
        } \
    } \
  while (0)

#define CHECK_EQ_U64(a, b) \
  do \
    { \
      enca_test_checks++; \
      unsigned long long va_ = (unsigned long long) (a); \
      unsigned long long vb_ = (unsigned long long) (b); \
      if (va_ != vb_) \
        { \
          printf ("    FAIL %s:%d: %s == %s (" ENCA_TEST_U64F \
                  " != " ENCA_TEST_U64F ")\n", __FILE__, __LINE__, #a, #b, \
                  va_, vb_); \
          enca_test_failures++; \
        } \
    } \
  while (0)

typedef void (*enca_test_fn) (void);

void enca_test_run_suite (const char *name, enca_test_fn fn);

void enca_test_set_panic_jmp (jmp_buf *jb);

#define ENCA_TEST_EXPECT_PANIC(stmt) \
  do \
    { \
      jmp_buf jb_; \
      if (setjmp (jb_) == 0) \
        { \
          enca_test_set_panic_jmp (&jb_); \
          stmt; \
          enca_test_set_panic_jmp (NULL); \
          printf ("    FAIL %s:%d: expected panic, none raised\n", __FILE__, \
                  __LINE__); \
          enca_test_failures++; \
        } \
      else \
        { \
          enca_test_set_panic_jmp (NULL); \
          enca_test_checks++; \
        } \
    } \
  while (0)

#endif
