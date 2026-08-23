#ifdef emacs
/* Building inside Emacs: pick up its configuration first, so that
   gnulib's wrapped system headers are used consistently.  */
# include <config.h>
#endif

#include "jsonrpc.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- growable buffer ---------------- */

void
jb_reset_keep_cap (enca_json_buf *b)
{
  b->len = 0;
  if (b->buf)
    b->buf[0] = 0;
}

enca_result
jb_init (enca_json_buf *b)
{
  b->cap = 4096;
  b->len = 0;
  b->buf = malloc (b->cap);
  return b->buf ? ENCA_OK : ENCA_ERR_OUT_OF_MEMORY;
}

void
jb_free (enca_json_buf *b)
{
  free (b->buf);
  b->buf = NULL;
  b->len = b->cap = 0;
}

static enca_result
jb_reserve (enca_json_buf *b, size_t extra)
{
  if (b->len + extra + 1 <= b->cap)
    return ENCA_OK;
  size_t cap = b->cap * 2;
  while (cap < b->len + extra + 1)
    cap *= 2;
  char *nb = realloc (b->buf, cap);
  if (!nb)
    return ENCA_ERR_OUT_OF_MEMORY;
  b->buf = nb;
  b->cap = cap;
  return ENCA_OK;
}

enca_result
jb_puts (enca_json_buf *b, const char *s)
{
  return jb_put_bytes (b, s, strlen (s));
}

enca_result
jb_put_bytes (enca_json_buf *b, const char *s, size_t n)
{
  enca_result r = jb_reserve (b, n);
  if (r != ENCA_OK)
    return r;
  memcpy (b->buf + b->len, s, n);
  b->len += n;
  b->buf[b->len] = 0;
  return ENCA_OK;
}

enca_result
jb_printf (enca_json_buf *b, const char *fmt, ...)
{
  va_list ap;
  va_start (ap, fmt);
  int need = vsnprintf (NULL, 0, fmt, ap);
  va_end (ap);
  if (need < 0)
    return ENCA_ERR_INTERNAL;
  enca_result r = jb_reserve (b, (size_t) need);
  if (r != ENCA_OK)
    return r;
  va_start (ap, fmt);
  int wrote = vsnprintf (b->buf + b->len, (size_t) need + 1, fmt, ap);
  va_end (ap);
  if (wrote != need)
    return ENCA_ERR_INTERNAL;
  b->len += (size_t) need;
  return ENCA_OK;
}

enca_result
jb_put_json_string (enca_json_buf *b, const char *s, size_t len)
{
  enca_result r = jb_puts (b, "\"");
  for (size_t i = 0; i < len && r == ENCA_OK; i++)
    {
      unsigned char c = (unsigned char) s[i];
      switch (c)
        {
        case '"': r = jb_puts (b, "\\\""); break;
        case '\\': r = jb_puts (b, "\\\\"); break;
        case '\n': r = jb_puts (b, "\\n"); break;
        case '\r': r = jb_puts (b, "\\r"); break;
        case '\t': r = jb_puts (b, "\\t"); break;
        default:
          if (c < 0x20)
            r = jb_printf (b, "\\u%04x", c);
          else
            {
              r = jb_reserve (b, 1);
              if (r == ENCA_OK)
                {
                  b->buf[b->len++] = (char) c;
                  b->buf[b->len] = 0;
                }
            }
        }
    }
  if (r == ENCA_OK)
    r = jb_puts (b, "\"");
  return r;
}

enca_result
jb_put_lsp_header (enca_json_buf *b, size_t payload_len)
{
  return jb_printf (b, "Content-Length: %zu\r\n\r\n", payload_len);
}

/* ---------------- minimal scanner ---------------- */

/* Recursive-descent walker: visits every MEMBER KEY in the document
   (objects nest, arrays recurse), so keys nested inside result objects
   are still found.  Strings are always skipped atomically (escape-
   aware), which keeps adversarial value content from looking like a
   key. */

static bool
j_walk_value (const char *j, size_t n, size_t *i, const char *key,
              bool want_u64, enca_u64 *out, int depth);

static size_t
j_skip_ws (const char *j, size_t n, size_t i)
{
  while (i < n
         && (j[i] == ' ' || j[i] == '\t' || j[i] == '\n' || j[i] == '\r'))
    i++;
  return i;
}

/* I points at '"'.  Skips to just past the closing quote. */
static void
j_skip_string (const char *j, size_t n, size_t *i)
{
  size_t p = *i + 1;
  while (p < n)
    {
      if (j[p] == '\\')
        p += 2;
      else if (j[p] == '"')
        {
          p++;
          break;
        }
      else
        p++;
    }
  *i = p;
}

static bool
j_key_eq (const char *j, size_t ks, size_t ke, const char *want)
{
  if ((size_t) (ke - ks) != strlen (want))
    return false;
  return memcmp (j + ks, want, ke - ks) == 0;
}

static bool
j_walk_container (const char *j, size_t n, size_t *i, const char *key,
                  bool want_u64, enca_u64 *out, int depth);

/* Walk one value starting at *I. */
static bool
j_walk_value (const char *j, size_t n, size_t *i, const char *key,
              bool want_u64, enca_u64 *out, int depth)
{
  if (*i >= n || depth > 64)
    return false;
  if (j[*i] == '"')
    {
      j_skip_string (j, n, i);
      return false;
    }
  if (j[*i] == '{' || j[*i] == '[')
    return j_walk_container (j, n, i, key, want_u64, out, depth + 1);
  /* scalar */
  size_t p = *i;
  if (want_u64 && p < n && j[p] >= '0' && j[p] <= '9')
    {
      enca_u64 v = 0;
      while (p < n && j[p] >= '0' && j[p] <= '9')
        {
          v = v * 10 + (enca_u64) (j[p] - '0');
          p++;
        }
      *i = p;
      *out = v;
      return true;
    }
  while (*i < n && j[*i] != ',' && j[*i] != '}' && j[*i] != ']')
    (*i)++;
  return false;
}

static bool
j_walk_container (const char *j, size_t n, size_t *i, const char *key,
                  bool want_u64, enca_u64 *out, int depth)
{
  const bool is_array = j[*i] == '[';
  (*i)++;                       /* past '{' or '[' */

  if (is_array)
    {
      /* Array: recurse into each element. */
      for (;;)
        {
          *i = j_skip_ws (j, n, *i);
          if (*i >= n)
            return false;
          if (j[*i] == ']')
            {
              (*i)++;
              return false;
            }
          if (j_walk_value (j, n, i, key, want_u64, out, depth))
            return true;
          *i = j_skip_ws (j, n, *i);
          if (*i < n && j[*i] == ',')
            {
              (*i)++;
              continue;
            }
          if (*i < n && j[*i] == ']')
            {
              (*i)++;
              return false;
            }
          if (*i >= n)
            return false;
        }
    }

  /* Object: member-name : value pairs. */
  for (;;)
    {
      *i = j_skip_ws (j, n, *i);
      if (*i >= n)
        return false;
      if (j[*i] == '}')
        {
          (*i)++;
          return false;
        }
      if (j[*i] == ',')
        {
          (*i)++;
          continue;
        }
      if (j[*i] != '"')
        {
          (*i)++;
          continue;
        }

      size_t ks = *i + 1;
      j_skip_string (j, n, i);
      size_t ke = *i - 1;

      *i = j_skip_ws (j, n, *i);
      if (*i >= n || j[*i] != ':')
        continue;
      (*i)++;
      *i = j_skip_ws (j, n, *i);

      if (j_key_eq (j, ks, ke, key))
        {
          if (!want_u64)
            return true;        /* presence hit */
          /* Numeric wanted: consume the value manually so we can bind
             it; non-numeric values fall through unsatisfied. */
          if (*i < n && j[*i] >= '0' && j[*i] <= '9')
            {
              enca_u64 v = 0;
              while (*i < n && j[*i] >= '0' && j[*i] <= '9')
                {
                  v = v * 10 + (enca_u64) (j[*i] - '0');
                  (*i)++;
                }
              *out = v;
              return true;
            }
          j_walk_value (j, n, i, "", false, NULL, depth);
        }
      else
        {
          /* Non-matching member: only CONTAINERS can hide matching
             members deeper; a scalar value must not bind (this is
             what made {"a":1,"id":42} bind 1). */
          if (j[*i] == '{' || j[*i] == '[')
            {
              if (j_walk_value (j, n, i, key, want_u64, out, depth))
                return true;
            }
          else
            {
              while (*i < n && j[*i] != ',' && j[*i] != '}')
                (*i)++;
            }
        }
    }
}

bool
enca_json_get_u64 (const char *payload, size_t len, const char *key,
                   enca_u64 *out)
{
  size_t i = j_skip_ws (payload, len, 0);
  return j_walk_value (payload, len, &i, key, true, out, 0);
}

bool
enca_json_has_member (const char *payload, size_t len, const char *key)
{
  size_t i = j_skip_ws (payload, len, 0);
  enca_u64 dummy = 0;
  return j_walk_value (payload, len, &i, key, false, &dummy, 0);
}
