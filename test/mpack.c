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

#ifdef TEST_AMALGAMATION
# include "../build/mpack.c"
#else
# include "../build/mpack.h"
#endif

bool done;
char buf[0xffffff];
size_t bufpos;
mpack_token_t tokbuf[0xffffff];
size_t tokbufpos;
mpack_writer_t writer;

static void w(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  bufpos += (size_t)vsnprintf(buf + bufpos, sizeof(buf) - bufpos, fmt, ap);
  va_end(ap);
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

void parse_json(char **s)
{
  while (**s == ' ' || **s == ',') {
    (*s)++;
  }

  switch (**s) {
    case 'n':
      tokbuf[tokbufpos++] = mpack_pack_nil();
      *s += 4;
      break;
    case 'f':
      *s += 5;
      tokbuf[tokbufpos++] = mpack_pack_boolean(false);
      break;
    case 't':
      *s += 4;
      tokbuf[tokbufpos++] = mpack_pack_boolean(true);
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
        tokbuf[tokbufpos++] = mpack_pack_float_fast(d);
        {
          /* test both pack_float implementations */
          mpack_token_t tok = tokbuf[tokbufpos - 1];
          mpack_token_t tok2 = mpack_pack_float_compat(d);
          (void)(tok);
          (void)(tok2);
          assert(tok2.data.value.lo == tok.data.value.lo &&
              tok2.data.value.hi == tok.data.value.hi);
        }
      } else {
        tokbuf[tokbufpos++] = mpack_pack_sint((mpack_sintmax_t)strtoll(tmp, NULL, 10));
      }
      break;
    }
    case '"': {
      (*s)++;
      char *s2 = strchr(*s, '"');
      mpack_uint32_t len = (mpack_uint32_t)(s2 - *s);
      switch (**s) {
        case 's':
          tokbuf[tokbufpos++] = mpack_pack_str(len - 2);
          tokbuf[tokbufpos++] = mpack_pack_chunk(*s + 2, len - 2);
          break;
        case 'b':
          tokbuf[tokbufpos++] = mpack_pack_bin(len - 2);
          tokbuf[tokbufpos++] = mpack_pack_chunk(*s + 2, len - 2);
          break;
        case 'e':
          tokbuf[tokbufpos++] = mpack_pack_ext((int)strtol(*s + 2, NULL, 16), len - 5);
          tokbuf[tokbufpos++] = mpack_pack_chunk(*s + 5, len - 5);
          break;
        default:
          tokbuf[tokbufpos++] = mpack_pack_str(len);
          tokbuf[tokbufpos++] = mpack_pack_chunk(*s, len);
          break;
      }
      *s = s2 + 1;
      break;
    }
    case '[': {
      (*s)++;
      tokbuf[tokbufpos++] = mpack_pack_array(item_count(*s));
      while (**s != ']') {
        parse_json(s);
      }
      (*s)++;
      break;
    }
    case '{': {
      (*s)++;
      tokbuf[tokbufpos++] = mpack_pack_map(item_count(*s));
      while (**s != '}') {
        parse_json(s);
        (*s)++;
        parse_json(s);
      }
      (*s)++;
      break;
    }
  }
} 

static void parse_cb(mpack_stack_t *stack, mpack_node_t *parent,
    mpack_node_t *node)
{
  (void)(stack);
  mpack_token_t *t = &node->tok;
  mpack_token_t *p = parent ? &parent->tok : NULL;

  switch (t->type) {
    case MPACK_TOKEN_NIL:
      w("null"); break;
    case MPACK_TOKEN_BOOLEAN:
      w(mpack_unpack_boolean(t) ? "true" : "false"); break;
    case MPACK_TOKEN_UINT:
      w("%" UFORMAT, mpack_unpack_uint(t)); break;
    case MPACK_TOKEN_SINT:
      w("%" SFORMAT, mpack_unpack_sint(t)); break;
    case MPACK_TOKEN_FLOAT: {
      /* test both unpack_float implementations */
      double d = mpack_unpack_float_fast(t), d2 = mpack_unpack_float_compat(t);
      (void)(d2);
      assert(d == d2);
      w("%.*g", 17, d);
      if (round(d) == d && (fabs(d) < 10e3)) {
        /* Need a trailing .0 to be parsed as float in the packer tests */
        w(".0");
      }
      break;
    } 
    case MPACK_TOKEN_CHUNK:
      w("%.*s", t->length, t->data.chunk_ptr); break;
    case MPACK_TOKEN_BIN:
    case MPACK_TOKEN_STR:
    case MPACK_TOKEN_EXT:
      w("\"");
      if (node->data) {
        node->data = 0;
      } else {
        if (!p || p->type != MPACK_TOKEN_MAP || parent->pos % 2) {
          if (t->type == MPACK_TOKEN_EXT) {
            w("e:%02x:", t->data.ext_type);
          } else {
            w(t->type == MPACK_TOKEN_BIN ? "b:" : "s:");
          }
        }
        node->data = (void *)1;
      }
      break;
    case MPACK_TOKEN_ARRAY:
    case MPACK_TOKEN_MAP:
      if (node->data) {
        w(t->type == MPACK_TOKEN_ARRAY ? "]" : "}");
        node->data = 0;
      } else {
        w(t->type == MPACK_TOKEN_ARRAY ? "[" : "{");
        node->data = (void *)1;
      }
      break;
  }

  if (!node->data && p && p->type < MPACK_TOKEN_BIN
      && parent->pos < p->length - 1) {
    w(p->type == MPACK_TOKEN_MAP ? (parent->pos % 2 ? "," : ":") : ",");
  }
}

/* Each unpack/pack test is executed multiple times, with each feeding data in
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

  char repr[32];
  snprintf(repr, sizeof(repr), "%s", fjson);
  for (size_t i = 0; i < ARRAY_SIZE(chunksizes); i++) {
    mpack_parser_t parser;
    size_t cs = chunksizes[i];
    /* unpack test */
    done = false;
    bufpos = 0;
    mpack_parser_init(&parser, parse_cb);
    char *b = (char *)fmsgpack;
    size_t bl = cs;

    while (mpack_parse(&parser, (const char **)&b, &bl) != MPACK_OK) {
      if (!bl) bl = cs;
    }

    is(buf, fjson, cs == SIZE_MAX ?
        "unpack '%s' in a single step" :
        "unpack '%s' in steps of %zu", repr, cs);

    /* pack test */
    char *fj = fjson;
    size_t pos = 0;
    tokbufpos = 0;
    parse_json(&fj);
    mpack_writer_init(&writer);
    b = buf;
    bl = MIN(cs, sizeof(buf));
    while (pos < tokbufpos) {
      if (mpack_write(&writer, &b, &bl, tokbuf + pos) == MPACK_OK) pos++;
      if (!bl) bl = cs;
    }

    cmp_mem(buf, fmsgpack, fmsgpacklen, cs == SIZE_MAX ?
        "pack '%s' in a single step" :
        "pack '%s' in steps of %zu", repr, cs);
  }
}

static void signed_positive_packs_with_unsigned_format(void)
{
  char mpackbuf[256];
  char *buf = mpackbuf;
  size_t buflen = sizeof(mpackbuf);
  mpack_writer_init(&writer);
  tokbufpos = 0;
  tokbuf[tokbufpos++] = mpack_pack_sint(0);
  tokbuf[tokbufpos++] = mpack_pack_sint(1);
  tokbuf[tokbufpos++] = mpack_pack_sint(0x7f);
  tokbuf[tokbufpos++] = mpack_pack_sint(0xff);
  tokbuf[tokbufpos++] = mpack_pack_sint(0xffff);
#ifndef FORCE_32BIT_INTS
  tokbuf[tokbufpos++] = mpack_pack_sint(0xffffffff);
  tokbuf[tokbufpos++] = mpack_pack_sint(0x7fffffffffffffff);
#endif
  for (size_t i = 0; i < tokbufpos; i++)
    mpack_write(&writer, &buf, &buflen, tokbuf + i);
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
  cmp_mem(mpackbuf, expected, sizeof(mpackbuf) - buflen,
      "signed positive packs with unsigned format");
}

static void positive_signed_format_unpacks_as_unsigned(void)
{
  mpack_reader_t reader;
  mpack_token_t toks[4];
  const uint8_t input[] = {
    0xd0, 0x7f,
    0xd1, 0x7f, 0xff,
    0xd2, 0x7f, 0xff, 0xff, 0xff,
#ifndef FORCE_32BIT_INTS
    0xd3, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
#endif
  };
  const char *inp = (const char *)input;
  size_t inplen = sizeof(input);
  mpack_reader_init(&reader);
  for (size_t i = 0; i < ARRAY_SIZE(toks) && inplen; i++)
    mpack_read(&reader, &inp, &inplen, toks + i);
  mpack_uintmax_t expected[] = {
    0x7f,
    0x7fff,
    0x7fffffff,
#ifndef FORCE_32BIT_INTS
    0x7fffffffffffffff
#endif
  };
  mpack_token_type_t expected_types[] = {
    MPACK_TOKEN_UINT,
    MPACK_TOKEN_UINT,
    MPACK_TOKEN_UINT,
#ifndef FORCE_32BIT_INTS
    MPACK_TOKEN_UINT,
#endif
  };
  mpack_uintmax_t actual[] = {
    mpack_unpack_uint(toks + 0),
    mpack_unpack_uint(toks + 1),
    mpack_unpack_uint(toks + 2),
#ifndef FORCE_32BIT_INTS
    mpack_unpack_uint(toks + 3)
#endif
  };
  mpack_token_type_t actual_types[] = {
    toks[0].type,
    toks[1].type,
    toks[2].type,
#ifndef FORCE_32BIT_INTS
    toks[3].type
#endif
  };
  cmp_mem(expected, actual, sizeof(expected),
      "positive signed format unpacks as unsigned");
  cmp_mem(expected_types, actual_types, sizeof(expected_types),
      "positive signed format unpacks as unsigned(tokens)");
}

static void unpacking_c1_returns_eread(void)
{
  mpack_reader_t reader;
  const uint8_t input[] = {0xc1};
  const char *inp = (const char *)input;
  size_t inplen = sizeof(input);
  mpack_token_t tok;
  mpack_reader_init(&reader);
  mpack_parser_t parser;
  mpack_parser_init(&parser, parse_cb);
  int res = mpack_read(&reader, &inp, &inplen, &tok),
      res2 = mpack_parse(&parser, &inp, &inplen);
  ok(res == MPACK_ERROR && res2 == MPACK_ERROR, "0xc1 returns MPACK_ERROR");
}

static void very_deep_objects_returns_enomem(void)
{
  mpack_parser_t parser;
  mpack_parser_init(&parser, parse_cb);
  parser.stack.capacity = 2;
  const uint8_t input[] = {0x91, 0x91, 0x01};  /* [[1]] */
  const char *inp = (const char *)input;
  size_t inplen = sizeof(input);
  ok(mpack_parse(&parser, &inp, &inplen) == MPACK_NOMEM && inplen == 1
      && inp == (const char *)input + 2,
      "very deep objects return MPACK_ENOMEM");
}

static void does_not_write_invalid_tokens(void)
{
  mpack_writer_t writer;
  mpack_writer_init(&writer);
  mpack_token_t tok, tok2;
  tok.type = 9999;
  char buf[64], *ptr = buf;
  size_t ptrlen = sizeof(buf);
  tok2 = mpack_pack_float_compat(5.5);
  tok2.length = 5;
  ok(mpack_write(&writer, &ptr, &ptrlen, &tok) == MPACK_ERROR,
      "does not write invalid tokens 1");
  mpack_writer_init(&writer);
  ok(mpack_write(&writer, &ptr, &ptrlen, &tok2) == MPACK_ERROR,
      "does not write invalid tokens 2");
}

int main(void)
{
  int extra_test_count = 7;
  plan(fixture_count * (int)ARRAY_SIZE(chunksizes) * 2 + extra_test_count);
  for (int i = 0; i < fixture_count; i++) {
    fixture_test(i);
  }
  signed_positive_packs_with_unsigned_format();
  positive_signed_format_unpacks_as_unsigned();
  unpacking_c1_returns_eread();
  very_deep_objects_returns_enomem();
  does_not_write_invalid_tokens();
  done_testing();
}
