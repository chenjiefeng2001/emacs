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

/* Locate the '[' that opens result.items (clangd completion shape).
   Deliberately walks the top-level object then the result object so
   sibling members are skipped with full bracket/string awareness. */
static bool
j_find_items_array (const char *j, size_t n, size_t *i)
{
  size_t p = j_skip_ws (j, n, 0);
  if (p >= n || j[p] != '{')
    return false;
  p++;
  const char *want1 = "result";
  for (;;)
    {
      p = j_skip_ws (j, n, p);
      if (p >= n)
        return false;
      if (j[p] == '}')
        return false;
      if (j[p] == ',')
        {
          p++;
          continue;
        }
      if (j[p] != '"')
        {
          p++;
          continue;
        }
      size_t ks = p + 1;
      j_skip_string (j, n, &p);
      size_t ke = p - 1;
      p = j_skip_ws (j, n, p);
      if (p >= n || j[p] != ':')
        continue;
      p = j_skip_ws (j, n, p + 1);
      if ((size_t) (ke - ks) != strlen (want1)
          || memcmp (j + ks, want1, strlen (want1)) != 0)
        {
          /* skip this value generically */
          if (j[p] == '{' || j[p] == '[')
            {
              int depth = 0;
              while (p < n)
                {
                  char c = j[p];
                  if (c == '"')
                    {
                      j_skip_string (j, n, &p);
                      continue;
                    }
                  if (c == '{' || c == '[')
                    depth++;
                  else if (c == '}' || c == ']')
                    {
                      depth--;
                      p++;
                      if (depth == 0)
                        break;
                      continue;
                    }
                  p++;
                }
            }
          else
            while (p < n && j[p] != ',' && j[p] != '}')
              p++;
          continue;
        }
      /* result found: expect object */
      if (p >= n || j[p] != '{')
        return false;
      p++;
      for (;;)
        {
          p = j_skip_ws (j, n, p);
          if (p >= n)
            return false;
          if (j[p] == '}')
            return false;
          if (j[p] == ',')
            {
              p++;
              continue;
            }
          if (j[p] != '"')
            {
              p++;
              continue;
            }
          size_t k2s = p + 1;
          j_skip_string (j, n, &p);
          size_t k2e = p - 1;
          p = j_skip_ws (j, n, p);
          if (p >= n || j[p] != ':')
            continue;
          p = j_skip_ws (j, n, p + 1);
          if ((size_t) (k2e - k2s) == 5
              && memcmp (j + k2s, "items", 5) == 0)
            {
              if (p < n && j[p] == '[')
                {
                  *i = p + 1;
                  return true;
                }
              return false;
            }
          if (j[p] == '{' || j[p] == '[')
            {
              int depth = 0;
              while (p < n)
                {
                  char c = j[p];
                  if (c == '"')
                    {
                      j_skip_string (j, n, &p);
                      continue;
                    }
                  if (c == '{' || c == '[')
                    depth++;
                  else if (c == '}' || c == ']')
                    {
                      depth--;
                      p++;
                      if (depth == 0)
                        break;
                      continue;
                    }
                  p++;
                }
            }
          else
            {
              while (p < n && j[p] != ',' && j[p] != '}')
                p++;
            }
        }
    }
}

enca_usize
enca_json_collect_item_labels (const char *payload, size_t len,
                               const char **labels, size_t *lens,
                               size_t cap)
{
  size_t pos = 0;
  if (!j_find_items_array (payload, len, &pos))
    return 0;

  enca_usize count = 0;
  for (;;)
    {
      pos = j_skip_ws (payload, len, pos);
      if (pos >= len)
        break;
      if (payload[pos] == ']')
        break;
      if (payload[pos] != '{')
        {
          pos++;
          continue;
        }

      /* element object: find its end (string-aware), then locate the
         label member inside. */
      size_t elem_end = pos;
      int depth = 0;
      while (elem_end < len)
        {
          char c = payload[elem_end];
          if (c == '"')
            {
              j_skip_string (payload, len, &elem_end);
              continue;
            }
          if (c == '{')
            depth++;
          else if (c == '}')
            {
              depth--;
              elem_end++;
              if (depth == 0)
                break;
              continue;
            }
          elem_end++;
        }

      size_t p = pos + 1;
      while (p < elem_end)
        {
          if (payload[p] != '"')
            {
              p++;
              continue;
            }
          size_t ks = p + 1;
          j_skip_string (payload, elem_end, &p);
          size_t ke = p - 1;
          size_t q = j_skip_ws (payload, elem_end, p);
          if (q >= elem_end || payload[q] != ':')
            continue;
          q = j_skip_ws (payload, elem_end, q + 1);
          if (q < elem_end && payload[q] == '"'
              && ke - ks == 5 && memcmp (payload + ks, "label", 5) == 0)
            {
              size_t vs = q + 1;
              size_t ve = q + 1;
              j_skip_string (payload, elem_end, &ve);
              /* ve = past closing quote; strip both quotes below */
              if (ve > vs + 1)
                {
                  if (count < cap)
                    {
                      labels[count] = payload + vs;
                      lens[count] = ve - vs - 1;
                    }
                  count++;
                }
              p = ve;
            }
        }
      pos = elem_end;
      pos = j_skip_ws (payload, len, pos);
      if (pos < len && payload[pos] == ',')
        pos++;
    }
  return count;
}
