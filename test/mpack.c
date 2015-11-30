#include <assert.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <check.h>
#include <jansson.h>

#include "mpack.h"

#ifndef MIN
# define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#endif
#define ARRAY_SIZE(arr) \
  ((sizeof(arr)/sizeof((arr)[0])) / \
   ((size_t)(!(sizeof(arr) % sizeof((arr)[0])))))

typedef struct {
  union {
    json_t **items;
    char *buf;
  } data;
  json_type type;
  size_t size, pos;
} json_builder_t;

typedef struct {
  mpack_unpacker_t unpacker;
  json_builder_t stack[64];
  size_t stackpos;
  json_t *unpacked;
} unpacker_test_data_t;

static json_builder_t *top(unpacker_test_data_t *data)
{
  if (!data->stackpos) {
    return NULL;
  }
  return data->stack + data->stackpos - 1;
}

static json_builder_t *push(unpacker_test_data_t *data)
{
  json_builder_t *rv = data->stack + data->stackpos++;
  memset(rv, 0, sizeof(*rv));
  return rv;
}

static void merge_json(unpacker_test_data_t *data, json_t *val)
{
  json_builder_t *t = top(data);
  if (!t) {
    data->unpacked = val;
  } else {
    t->data.items[t->pos++] = val;
  }
}

static void merge_chunk(unpacker_test_data_t *data, const char *chunk,
    size_t chunklen)
{
  json_builder_t *t = top(data);
  memcpy(t->data.buf + t->pos, chunk, chunklen);
  t->pos += chunklen;
}

static void process_token(unpacker_test_data_t *data, mpack_token_t *t)
{
  json_builder_t *p = NULL;
  switch (t->type) {
    case MPACK_TOKEN_NIL:
      merge_json(data, json_null()); break;
    case MPACK_TOKEN_BOOLEAN:
      merge_json(data, MPACK_INT32(t) ? json_true() : json_false()); break;
    case MPACK_TOKEN_UINT:
      merge_json(data, json_integer(MPACK_INT64(t))); break;
    case MPACK_TOKEN_SINT:
      merge_json(data, json_integer(MPACK_INT64(t))); break;
    case MPACK_TOKEN_FLOAT:
      merge_json(data, json_real(MPACK_DOUBLE(t))); break;
    case MPACK_TOKEN_CHUNK:
      merge_chunk(data, t->data.chunk_ptr, t->length);
      break;
    case MPACK_TOKEN_BIN:
    case MPACK_TOKEN_STR:
    case MPACK_TOKEN_EXT:
      p = push(data);
      p->size = t->length + 2;
      p->pos = 2;
      if (t->type == MPACK_TOKEN_EXT) {
        p->size += 3;
        p->pos += 3;
        p->data.buf = malloc(p->size + 1);
        snprintf(p->data.buf, p->size + 1, "e:%02x:", t->data.ext_type);
      } else {
        p->data.buf = malloc(p->size + 1);
        snprintf(p->data.buf, p->size + 1,
            t->type == MPACK_TOKEN_BIN ? "b:" : "s:");
      }
      p->data.buf[p->size] = 0;
      p->type = JSON_STRING;
      break;
    case MPACK_TOKEN_ARRAY:
    case MPACK_TOKEN_MAP:
      p = push(data);
      p->size = t->length;
      p->pos = 0;
      p->type = t->type == MPACK_TOKEN_MAP ? JSON_OBJECT : JSON_ARRAY;
      if (p->size) {
        p->data.items = malloc(p->size * sizeof(json_t *));
      }
      break;
    default: abort();
  }

  while (data->stackpos) {
    json_builder_t *t = top(data);
    if (t->pos < t->size) {
      /* need to unpack more */
      break;
    }
    json_t *val = NULL;
    switch (t->type) {
      case JSON_OBJECT:
        val = json_object();
        for (size_t i = 0; i < t->size; i += 2) {
          json_t *k = t->data.items[i], *v = t->data.items[i + 1];
          /* Only use string keys in the tests */
          assert(k->type == JSON_STRING);
          const char *str = json_string_value(k);
          json_object_set_new(val, str + (*str == 'e' ? 5 : 2), v);
          json_decref(k);
        }
        free(t->data.items);
        break;
      case JSON_ARRAY:
        val = json_array();
        for (size_t i = 0; i < t->size; i ++) {
          json_array_append_new(val, t->data.items[i]);
        }
        free(t->data.items);
        break;
      case JSON_STRING:
        val = json_string_nocheck(t->data.buf);
        free(t->data.buf);
        break;
      default: abort();
    }
    /* pop and merge with the parent */
    data->stackpos--;
    merge_json(data, val);
  }
}

static void unpack_buf(mpack_unpacker_t *unpacker, const char *buf, size_t buflen)
{
  unpacker_test_data_t *data = (unpacker_test_data_t *)unpacker;

  while (!data->unpacked && buflen) {
    mpack_token_t *tok = mpack_unpack(unpacker, &buf, &buflen);
    if (tok) {
      process_token(data, tok);
    }
  }
}

#include "fixtures.inl"


START_TEST (unpack)
{
  /* Each unpack test is executed multiple times, with each feeding data in
   * chunks of different sizes. */
  static const size_t chunksizes[] =
     {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 30, 60, SIZE_MAX};

  struct fixture *f = fixtures + _i;
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
    unpacker_test_data_t *data = calloc(1, sizeof(*data));
    mpack_unpacker_init(&data->unpacker);


    for (size_t j = 0; !data->unpacked; j = MIN(j + cs, fmsgpacklen - 1)) {
      unpack_buf(&data->unpacker, (const char *)fmsgpack + j,
          MIN(cs, fmsgpacklen - j));
    }

    char *json = json_dumps(data->unpacked, JSON_COMPACT
                                          | JSON_PRESERVE_ORDER
                                          | JSON_ENCODE_ANY);
    json_decref(data->unpacked);
    ck_assert_str_eq(json, fjson);
    free(json);
    free(data);
  }
}
END_TEST

static Suite *unpack_suite(void)
{
  Suite *s;
  TCase *tc_core;

  s = suite_create("unpack");
  tc_core = tcase_create("core");

  tcase_add_loop_test(tc_core, unpack, 0, fixture_count);
  suite_add_tcase(s, tc_core);

  return s;
}

int main(void)
{
  Suite *s = unpack_suite();
  SRunner *sr = srunner_create(s);
  srunner_run_all(sr, CK_NORMAL);
  int number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
