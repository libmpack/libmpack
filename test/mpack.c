#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "tap.h"
#include "fixtures.h"

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
      w(MPACK_INT32(t) ? "true" : "false"); pop(1); break;
    case MPACK_TOKEN_UINT:
      w("%" PRIu64, MPACK_UINT64(t)); pop(1); break;
    case MPACK_TOKEN_SINT:
      w("%" PRId64, MPACK_INT64(t)); pop(1); break;
    case MPACK_TOKEN_FLOAT:
      w("%.*g", 17, MPACK_DOUBLE(t)); pop(1);
      if (round(MPACK_DOUBLE(t)) == MPACK_DOUBLE(t)
          && (fabs(MPACK_DOUBLE(t)) < 10e3)) {
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
  static const size_t chunksizes[] =
     {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 30, 60, SIZE_MAX};

static void unpack_test(int fixture_idx)
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
}

int main(void)
{
  plan(fixture_count * (int)ARRAY_SIZE(chunksizes));
  for (int i = 0; i < fixture_count; i++) {
    unpack_test(i);
  }
  done_testing();
}
