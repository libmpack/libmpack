#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tap.h"
#include "fixtures.h"

#ifdef FORCE_32BIT_INTS
#define UFORMAT PRIu32
#define SFORMAT PRId32
#undef ULLONG_MAX
#undef UINT64_MAX
#else
#define UFORMAT PRIu64
#define SFORMAT PRId64
#endif

#include "mpack.h"


typedef struct {
  mpack_unpacker_t unpacker;
  bool done;
  char buf[0xffffff];
  size_t bufpos;
  struct json_stack {
    size_t pos, length;
    char close;
  } stack[MPACK_DEFAULT_STACK_SIZE];
  size_t stackpos;
} unpacker_test_data_t;

unpacker_test_data_t data;

static void w(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  data.bufpos += (size_t)vsnprintf(data.buf + data.bufpos,
      sizeof(data.buf) - data.bufpos, fmt, ap);
  va_end(ap);
}

struct json_stack *top(void)
{
  if (!data.stackpos) {
    return NULL;
  }
  return data.stack + data.stackpos - 1;
}

static void pop(size_t cnt)
{
  struct json_stack *s = top();
  if (!s) {
    data.done = true;
    return;
  }
  s->pos += cnt;
}

static void push(size_t length, char close)
{
  struct json_stack *s = data.stack + data.stackpos++;
  memset(s, 0, sizeof(*s));
  s->length = length;
  s->pos = 0;
  s->close = close;
}

static void process_token(mpack_token_t *t)
{
  switch (t->type) {
    case MPACK_TOKEN_NIL:
      w("null"); pop(1); break;
    case MPACK_TOKEN_BOOLEAN:
      w(mpack_unpack_boolean(t) ? "true" : "false"); pop(1); break;
    case MPACK_TOKEN_UINT:
      w("%" UFORMAT, mpack_unpack_uint(t)); pop(1); break;
    case MPACK_TOKEN_SINT:
      w("%" SFORMAT, mpack_unpack_sint(t)); pop(1); break;
    case MPACK_TOKEN_FLOAT:
      w("%.*g", 17, t->data.value.f64); pop(1);
      if (round(t->data.value.f64) == t->data.value.f64
          && (fabs(t->data.value.f64) < 10e3)) {
        /* Need a trailing .0 to be parsed as float in the packer tests */
        w(".0");
      }
      break;
    case MPACK_TOKEN_CHUNK:
      w("%.*s", t->length, t->data.chunk_ptr); pop(t->length); break;
    case MPACK_TOKEN_BIN:
    case MPACK_TOKEN_STR:
    case MPACK_TOKEN_EXT:
      if (top() && top()->close == '}' && top()->pos % 2 == 0) {
        w("\"");
      } else if (t->type == MPACK_TOKEN_EXT) {
        w("\"e:%02x:", t->data.ext_type);
      } else {
        w(t->type == MPACK_TOKEN_BIN ? "\"b:" : "\"s:");
      }
      push(t->length, '"');
      break;
    case MPACK_TOKEN_ARRAY:
      w("["); push(t->length, ']'); break;
    case MPACK_TOKEN_MAP:
      w("{"); push(t->length, '}'); break;
    default: abort();
  }


  while (data.stackpos) {
    struct json_stack *s = top();

    if (s->pos < s->length) {
      if (s->pos) {
        if (s->close == ']') {
          w(",");
        } else if (s->close == '}') {
          w(s->pos % 2 ? ":" : ",");
        }
      }
      break;
    }
    
    if (s->close == '"') {
      w("\"");
    } else if (s->close == ']') {
      w("]");
    } else if (s->close == '}') {
      w("}");
    }
    data.stackpos--;
    pop(1);
  }
}

static uint32_t item_count(const char *s)
{
  size_t depth = 1;
  uint32_t count = 0; 
  while (depth) {
    int c = *s++;
    if (c == ' ') continue;
    else if (c == ']' || c == '}') depth--;
    else if (c == '[' || c == '{') depth++;
    else if (depth == 1 && c == ',') count++;
    else if (count == 0) count = 1;
  }
  return count;
}

void parse_json(char **buf, size_t *buflen, char **s)
{
  while (**s == ' ' || **s == ',') {
    (*s)++;
  }

  switch (**s) {
    case 'n':
      mpack_pack_nil(buf, buflen);
      *s += 4;
      break;
    case 'f':
      *s += 5;
      mpack_pack_boolean(buf, buflen, false);
      break;
    case 't':
      *s += 4;
      mpack_pack_boolean(buf, buflen, true);
      break;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case '.':
    case '+':
    case '-': {
      char *s2 = *s;
      double d = strtod(*s, &s2);
      size_t l = (size_t)(s2 - *s);
      char tmp[256];
      memcpy(tmp, *s, l);
      tmp[l] = 0;
      *s = s2;
      if (strchr(tmp, '.') || strchr(tmp, 'e')) {
        mpack_pack_float(buf, buflen, d);
      } else {
#ifdef FORCE_32BIT_INTS
        mpack_pack_sint(buf, buflen, (mpack_int32_t)strtoll(tmp, NULL, 10));
#else
        mpack_pack_sint(buf, buflen, strtoll(tmp, NULL, 10));
#endif
      }
      break;
    }
    case '"': {
      (*s)++;
      char *s2 = strchr(*s, '"');
      mpack_uint32_t len = (mpack_uint32_t)(s2 - *s);
      switch (**s) {
        case 's':
          mpack_pack_str(buf, buflen, len - 2);
          memcpy(*buf, *s + 2, len - 2);
          *buf += len - 2;
          *buflen -= len - 2;
          break;
        case 'b':
          mpack_pack_bin(buf, buflen, len - 2);
          memcpy(*buf, *s + 2, len - 2);
          *buf += len - 2;
          *buflen -= len - 2;
          break;
        case 'e':
          mpack_pack_ext(buf, buflen, (int)strtol(*s + 2, NULL, 16), len - 5);
          memcpy(*buf, *s + 5, len - 5);
          *buf += len - 5;
          *buflen -= len - 5;
          break;
        default:
          mpack_pack_str(buf, buflen, len);
          memcpy(*buf, *s, len);
          *buf += len;
          *buflen -= len;
          break;
      }
      *s = s2 + 1;
      break;
    }
    case '[': {
      (*s)++;
      mpack_pack_array(buf, buflen, item_count(*s));
      while (**s != ']') {
        parse_json(buf, buflen, s);
      }
      (*s)++;
      break;
    }
    case '{': {
      (*s)++;
      mpack_pack_map(buf, buflen, item_count(*s));
      while (**s != '}') {
        parse_json(buf, buflen, s);
        (*s)++;
        parse_json(buf, buflen, s);
      }
      (*s)++;
      break;
    }
    default: abort();
  }
  while (**s == ' ') {
    (*s)++;
  }
} 

static void unpack_buf(const char *buf, size_t buflen)
{
  while (!data.done && buflen) {
    mpack_token_t *tok = mpack_unpack(&data.unpacker, &buf, &buflen);
    if (tok) {
      process_token(tok);
    }
  }
}

/* Each unpack test is executed multiple times, with each feeding data in
 * chunks of different sizes. */
static const size_t chunksizes[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, SIZE_MAX};

static void fixture_test(int fixture_idx)
{
  const struct fixture *f = fixtures + fixture_idx;
  char *fjson;
  uint8_t *fmsgpack;
  size_t fmsgpacklen;
  if (f->generator) {
    f->generator(&fjson, &fmsgpack, &fmsgpacklen, f->generator_size);
  } else {
    fjson = f->json;
    fmsgpack = f->msgpack;
    fmsgpacklen = f->msgpacklen;
  }

  /* unpacking test */
  for (size_t i = 0; i < ARRAY_SIZE(chunksizes); i++) {
    size_t cs = chunksizes[i];
    data.stackpos = 0;
    data.done = false;
    data.bufpos = 0;
    mpack_unpacker_init(&data.unpacker);


    for (size_t j = 0; !data.done; j = MIN(j + cs, fmsgpacklen - 1)) {
      unpack_buf((const char *)fmsgpack + j, MIN(cs, fmsgpacklen - j));
    }

    is(data.buf, fjson, "fixture %d unpack with chunksize of %zu",
        fixture_idx, cs);
  }
  /* packing test */
  char *b = data.buf;
  size_t bl = sizeof(data.buf);
  char *j = fjson;
  parse_json(&b, &bl, &j);
  cmp_mem(data.buf, fmsgpack, fmsgpacklen, "fixture %d pack", fixture_idx);
}

static void positive_integer_passed_to_mpack_int_packs_as_positive(void)
{
  char mpackbuf[256];
  char *buf = mpackbuf;
  size_t buflen = sizeof(mpackbuf);
  mpack_pack_sint(&buf, &buflen, 0);
  mpack_pack_sint(&buf, &buflen, 1);
  mpack_pack_sint(&buf, &buflen, 0x7f);
  mpack_pack_sint(&buf, &buflen, 0xff);
  mpack_pack_sint(&buf, &buflen, 0xffff);
#ifndef FORCE_32BIT_INTS
  mpack_pack_sint(&buf, &buflen, 0xffffffff);
  mpack_pack_sint(&buf, &buflen, 0x7fffffffffffffff);
#endif
  uint8_t expected[] = {
    0x00,
    0x01,
    0x7f,
    0xcc, 0xff,
    0xcd, 0xff, 0xff,
#ifndef FORCE_32BIT_INTS
    0xce, 0xff, 0xff, 0xff, 0xff,
    0xcf, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
#endif
  };
  cmp_mem(mpackbuf, expected, sizeof(mpackbuf) - buflen, "packs as positive");
}

int main(void)
{
  plan(fixture_count * (int)ARRAY_SIZE(chunksizes) + fixture_count + 1);
  for (int i = 0; i < fixture_count; i++) {
    fixture_test(i);
  }
  positive_integer_passed_to_mpack_int_packs_as_positive();
  done_testing();
}
