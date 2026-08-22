#ifndef ENCA_ID_H
#define ENCA_ID_H

#include "../base/types.h"
#include "../base/attributes.h"
#include "../diagnostics/error.h"

#define ENCA_ID_TYPE_BITS 8
#define ENCA_ID_GEN_BITS 24
#define ENCA_ID_INDEX_BITS 32

#define ENCA_ID_TYPE_MAX ((enca_u8) 255)
#define ENCA_ID_GEN_MAX ((enca_u32) ((1u << ENCA_ID_GEN_BITS) - 1))
#define ENCA_ID_INDEX_MAX UINT32_MAX

typedef enum enca_obj_type
{
  ENCA_OBJ_NONE = 0,
  ENCA_OBJ_BUFFER = 1,
  ENCA_OBJ_WINDOW = 2,
  ENCA_OBJ_FRAME = 3,
  ENCA_OBJ_TASK = 4,
  ENCA_OBJ_EVENT_SOURCE = 5,
  ENCA_OBJ_WORKSPACE = 6,
  ENCA_OBJ_SNAPSHOT = 7,
  ENCA_OBJ_CHANNEL = 8,
} enca_obj_type;

ENCA_INLINE enca_object_id
enca_id_make (enca_u8 type, enca_u32 gen, enca_u32 index)
{
  return ((enca_object_id) type << 56)
    | ((enca_object_id) (gen & ENCA_ID_GEN_MAX) << 32)
    | (enca_object_id) index;
}

ENCA_INLINE enca_u8
enca_id_type (enca_object_id id)
{
  return (enca_u8) (id >> 56);
}

ENCA_INLINE enca_u32
enca_id_gen (enca_object_id id)
{
  return (enca_u32) ((id >> 32) & ENCA_ID_GEN_MAX);
}

ENCA_INLINE enca_u32
enca_id_index (enca_object_id id)
{
  return (enca_u32) id;
}

ENCA_INLINE bool
enca_id_valid (enca_object_id id)
{
  return id != ENCA_INVALID_ID;
}

typedef struct enca_id_slot
{
  enca_u32 gen;
  enca_u32 next_free;
} enca_id_slot;

typedef struct enca_id_type_table
{
  enca_id_slot *slots;
  enca_u32 count;
  enca_u32 cap;
  enca_u32 free_head;
} enca_id_type_table;

typedef struct enca_id_registry
{
  enca_id_type_table tables[ENCA_ID_TYPE_MAX + 1];
  enca_usize live_count;
} enca_id_registry;

#define ENCA_IDR_NIL UINT32_MAX

enca_result enca_idr_init (enca_id_registry *reg);
void enca_idr_destroy (enca_id_registry *reg);

ENCA_NODISCARD enca_result enca_idr_alloc (enca_id_registry *reg,
                                           enca_u8 type,
                                           enca_object_id *out_id);

ENCA_NODISCARD enca_result enca_idr_free (enca_id_registry *reg,
                                          enca_object_id id);

bool enca_idr_is_alive (const enca_id_registry *reg, enca_object_id id);

ENCA_NODISCARD enca_usize enca_idr_live_count (const enca_id_registry *reg);

#endif
