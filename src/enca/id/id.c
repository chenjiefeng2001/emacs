#include "id.h"

#include "../base/assert.h"
#include <string.h>

enca_result
enca_idr_init (enca_id_registry *reg)
{
  if (!reg)
    return ENCA_ERR_INVALID_ARGUMENT;
  memset (reg, 0, sizeof *reg);
  for (int i = 0; i <= ENCA_ID_TYPE_MAX; i++)
    reg->tables[i].free_head = ENCA_IDR_NIL;
  return ENCA_OK;
}

void
enca_idr_destroy (enca_id_registry *reg)
{
  if (!reg)
    return;
  for (int i = 0; i <= ENCA_ID_TYPE_MAX; i++)
    free (reg->tables[i].slots);
  memset (reg, 0, sizeof *reg);
}

static enca_result
table_grow (enca_id_type_table *t)
{
  enca_u32 new_cap = t->cap ? t->cap * 2u : 64u;
  enca_id_slot *p = realloc (t->slots, new_cap * sizeof (enca_id_slot));
  if (!p)
    return ENCA_ERR_OUT_OF_MEMORY;

  for (enca_u32 i = t->cap; i < new_cap; i++)
    {
      p[i].gen = 0;
      p[i].next_free = (i + 1u < new_cap) ? i + 1u : ENCA_IDR_NIL;
    }
  t->free_head = t->count;
  t->slots = p;
  t->cap = new_cap;
  return ENCA_OK;
}

enca_result
enca_idr_alloc (enca_id_registry *reg, enca_u8 type, enca_object_id *out_id)
{
  if (!reg || !out_id || type == ENCA_OBJ_NONE)
    return ENCA_ERR_INVALID_ARGUMENT;

  enca_id_type_table *t = &reg->tables[type];

  if (ENCA_UNLIKELY (t->free_head == ENCA_IDR_NIL && t->count == t->cap))
    {
      enca_result r = table_grow (t);
      if (ENCA_RESULT_IS_ERR (r))
        return r;
    }

  ENCA_ASSERT (t->free_head != ENCA_IDR_NIL, "id table growth failed");

  enca_u32 index = t->free_head;
  t->free_head = t->slots[index].next_free;

  if (index == t->count)
    t->count++;

  if (t->slots[index].gen == 0)
    t->slots[index].gen = 1;

  *out_id = enca_id_make (type, t->slots[index].gen, index);
  reg->live_count++;
  return ENCA_OK;
}

enca_result
enca_idr_free (enca_id_registry *reg, enca_object_id id)
{
  if (!reg)
    return ENCA_ERR_INVALID_ARGUMENT;

  enca_u8 type = enca_id_type (id);
  enca_u32 index = enca_id_index (id);

  if (!enca_id_valid (id) || type == ENCA_OBJ_NONE
      || index >= reg->tables[type].count)
    return ENCA_ERR_NOT_FOUND;

  enca_id_type_table *t = &reg->tables[type];
  enca_id_slot *slot = &t->slots[index];

  if (slot->gen != enca_id_gen (id))
    return ENCA_ERR_NOT_FOUND;

  slot->gen = (slot->gen + 1u) & ENCA_ID_GEN_MAX;
  if (slot->gen == 0)
    slot->gen = 1;

  slot->next_free = t->free_head;
  t->free_head = index;
  reg->live_count--;
  return ENCA_OK;
}

bool
enca_idr_is_alive (const enca_id_registry *reg, enca_object_id id)
{
  if (!reg || !enca_id_valid (id))
    return false;

  enca_u8 type = enca_id_type (id);
  const enca_id_type_table *t = &reg->tables[type];
  enca_u32 index = enca_id_index (id);

  if (type == ENCA_OBJ_NONE || index >= t->count)
    return false;

  return t->slots[index].gen == enca_id_gen (id);
}

enca_usize
enca_idr_live_count (const enca_id_registry *reg)
{
  return reg ? reg->live_count : 0;
}
