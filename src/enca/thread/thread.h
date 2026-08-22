#ifndef ENCA_THREAD_H
#define ENCA_THREAD_H

#include "../base/types.h"
#include "../base/attributes.h"
#include "../diagnostics/error.h"

#if defined(_WIN32)
typedef void *enca_thread_native_t;
typedef enca_u64 enca_mutex_native_t[6];
#else
# include <pthread.h>
typedef pthread_t enca_thread_native_t;
typedef pthread_mutex_t enca_mutex_native_t;
#endif

typedef struct enca_thread
{
  enca_thread_native_t native;
  enca_u32 id;
  bool detached;
} enca_thread;

typedef enca_result (*enca_thread_fn) (void *arg);

enca_result enca_thread_create (enca_thread *t, const char *name,
                                enca_thread_fn fn, void *arg);
enca_result enca_thread_join (enca_thread *t);
enca_result enca_thread_detach (enca_thread *t);

ENCA_NODISCARD enca_u32 enca_thread_self_id (void);
void enca_thread_yield (void);

typedef struct enca_mutex
{
  enca_mutex_native_t native;
} enca_mutex;

enca_result enca_mutex_init (enca_mutex *m);
void enca_mutex_destroy (enca_mutex *m);
void enca_mutex_lock (enca_mutex *m);
void enca_mutex_unlock (enca_mutex *m);
bool enca_mutex_try_lock (enca_mutex *m);

typedef struct enca_condition
{
#if defined(_WIN32)
  void *native;
#else
  pthread_cond_t native;
#endif
} enca_condition;

enca_result enca_condition_init (enca_condition *c);
void enca_condition_destroy (enca_condition *c);
void enca_condition_wait (enca_condition *c, enca_mutex *m);
bool enca_condition_timed_wait (enca_condition *c, enca_mutex *m, enca_u64 ns);
void enca_condition_signal (enca_condition *c);
void enca_condition_broadcast (enca_condition *c);

#define ENCA_TLS_MAX_SLOTS 64

enca_result enca_tls_alloc (unsigned *out_slot);
enca_result enca_tls_free (unsigned slot);
void *enca_tls_get (unsigned slot);
bool enca_tls_set (unsigned slot, void *value);

#endif
