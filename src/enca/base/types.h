#ifndef ENCA_TYPES_H
#define ENCA_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limits.h>

typedef uint8_t enca_u8;
typedef uint16_t enca_u16;
typedef uint32_t enca_u32;
typedef uint64_t enca_u64;

typedef int8_t enca_i8;
typedef int16_t enca_i16;
typedef int32_t enca_i32;
typedef int64_t enca_i64;

typedef size_t enca_usize;
typedef ptrdiff_t enca_isize;
typedef uintptr_t enca_uptr;

typedef enca_u64 enca_object_id;
typedef enca_u64 enca_timestamp_ns;
typedef enca_u32 enca_flags_t;
typedef enca_u64 enca_seq_t;

#define ENCA_INVALID_ID ((enca_object_id) 0)
#define ENCA_KIB(n) ((enca_usize) (n) * 1024u)
#define ENCA_MIB(n) (ENCA_KIB (n) * 1024u)

#if defined(_MSC_VER) || (defined(__MINGW32__) && !defined(_UCRT))
# define ENCA_U64F "%I64u"
#else
# define ENCA_U64F "%llu"
#endif

#endif
