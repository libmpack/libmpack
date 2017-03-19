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

#include "fixtures.h"
#include "tap.h"

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
# define MPACK_API static
# include "../build/mpack.c"
#else
# include "../build/mpack.h"
#endif

static char buf[0xffffff];
static size_t bufpos;
static bool number_conv = false;
static bool throw = false;

static mpack_data_t d(void *p)
{
  mpack_data_t rv;
  rv.p = p;
  return rv;
}

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

static void unparse_enter(mpack_parser_t *parser, mpack_node_t *node)
{
  mpack_node_t *parent = MPACK_PARENT_NODE(node);
  char *p = parent ? parent->data[0].p : parser->data.p;

  if (parent && parent->tok.type > MPACK_TOKEN_MAP) {
    node->tok = mpack_pack_chunk(p, parent->tok.length);
    p += parent->tok.length;
    goto end;
  }

  while (*p == ' ' || *p == ',' || *p == ':') {
    p++;
  }

  switch (*p) {
    case 'n':
      node->tok = mpack_pack_nil();
      p += 4;
      break;
    case 'f':
      node->tok = mpack_pack_boolean(false);
      p += 5;
      break;
    case 't':
      node->tok = mpack_pack_boolean(true);
      p += 4;
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
      if (throw) {
        MPACK_THROW(parser);
      }
      char *p2 = p;
      double d = strtod(p, &p2);
      size_t l = (size_t)(p2 - p);
      char tmp[256];
      memcpy(tmp, p, l);
      tmp[l] = 0;
      p = p2;
      if (number_conv) {
        node->tok = mpack_pack_number(d);
      } else {
        if (strchr(tmp, '.') || strchr(tmp, 'e')) {
          node->tok = mpack_pack_float_fast(d);
          {
            /* test both pack_float implementations */
            mpack_token_t tok = mpack_pack_float_compat(d);
            (void)(tok);
            assert(node->tok.data.value.lo == tok.data.value.lo &&
                node->tok.data.value.hi == tok.data.value.hi);
          }
        } else {
          node->tok = mpack_pack_sint((mpack_sintmax_t)strtoll(tmp, NULL, 10));
        }
      }
      break;
    }
    case '"': {
      p++;
      char *s2 = strchr(p, '"');
      mpack_uint32_t len = (mpack_uint32_t)(s2 - p);
      switch (*p) {
        case 's':
          node->tok = mpack_pack_str(len - 2);
          p += 2;
          break;
        case 'b':
          node->tok = mpack_pack_bin(len - 2);
          p += 2;
          break;
        case 'e':
          node->tok = mpack_pack_ext((int)strtol(p + 2, NULL, 16), len - 5);
          p += 5;
          break;
        default:
          node->tok = mpack_pack_str(len);
          break;
      }
      break;
    }
    case '[':
      p++;
      node->tok = mpack_pack_array(item_count(p));
      break;
    case '{':
      p++;
      node->tok = mpack_pack_map(item_count(p));
      break;
  }

end:
  node->data[0].p = p;
  if (parent) parent->data[0].p = p;
}

static void unparse_exit(mpack_parser_t *parser, mpack_node_t *node)
{
  (void)(parser);
  mpack_node_t *parent = MPACK_PARENT_NODE(node);
  char *p = node->data[0].p;

  switch (node->tok.type) {
    case MPACK_TOKEN_BIN:
    case MPACK_TOKEN_STR:
    case MPACK_TOKEN_EXT:
    case MPACK_TOKEN_ARRAY:
    case MPACK_TOKEN_MAP: p++;
    default: break;
  }

  node->data[0].p = p;
  if (parent) parent->data[0].p = p;
}

static void parse_enter(mpack_parser_t *parser, mpack_node_t *node)
{
  (void)(parser);
  mpack_node_t *parent = MPACK_PARENT_NODE(node);
  mpack_token_t *t = &node->tok;
  mpack_token_t *p = parent ? &parent->tok : NULL;

  switch (t->type) {
    case MPACK_TOKEN_NIL:
      w("null"); break;
    case MPACK_TOKEN_BOOLEAN:
      w(mpack_unpack_boolean(*t) ? "true" : "false"); break;
    case MPACK_TOKEN_UINT:
      if (throw) MPACK_THROW(parser);
      if (number_conv) goto nconv;
      w("%" UFORMAT, mpack_unpack_uint(*t)); break;
    case MPACK_TOKEN_SINT:
      if (number_conv) goto nconv;
      w("%" SFORMAT, mpack_unpack_sint(*t)); break;
    case MPACK_TOKEN_FLOAT: {
      if (number_conv) goto nconv;
      /* test both unpack_float implementations */
      double d = mpack_unpack_float_fast(*t),d2 = mpack_unpack_float_compat(*t);
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
      if (!p || p->type != MPACK_TOKEN_MAP || parent->key_visited) {
        if (t->type == MPACK_TOKEN_EXT) {
          w("e:%02x:", t->data.ext_type);
        } else {
          w(t->type == MPACK_TOKEN_BIN ? "b:" : "s:");
        }
      }
      break;
    case MPACK_TOKEN_ARRAY:
      w("["); break;
    case MPACK_TOKEN_MAP:
      w("{"); break;
  }
  return;

  double d;
nconv:
  d = mpack_unpack_number(*t);
  w("%.*g", 17, d);
}

static void parse_exit(mpack_parser_t *parser, mpack_node_t *node)
{
  (void)(parser);
  mpack_node_t *parent = MPACK_PARENT_NODE(node);
  mpack_token_t *t = &node->tok;
  mpack_token_t *p = parent ? &parent->tok : NULL;

  switch (t->type) {
    case MPACK_TOKEN_BIN:
    case MPACK_TOKEN_STR:
    case MPACK_TOKEN_EXT:
      w("\""); break;
    case MPACK_TOKEN_ARRAY:
      w("]"); break;
    case MPACK_TOKEN_MAP:
      w("}"); break;
    default: break;
  }

  if (p && p->type < MPACK_TOKEN_BIN && parent->pos < p->length) {
    w(p->type == MPACK_TOKEN_MAP ? (parent->key_visited ? ":" : ",") : ",");
  }
}

/* Each unpack/pack test is executed multiple times, with each feeding data in
 * chunks of different sizes. */
static const size_t chunksizes[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, SIZE_MAX};

static void fixture_test(const struct fixture *ff, int fixture_idx)
{
  const struct fixture *f = ff + fixture_idx;
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
    char *b;
    size_t bl;
    int s;
    /* unpack test */
    bufpos = 0;
    mpack_parser_init(&parser, 0);
    b = (char *)fmsgpack;
    bl = cs;

    do {
      s = mpack_parse(&parser, (const char **)&b, &bl, parse_enter, parse_exit);
      if (s) {
        assert(s == MPACK_EOF);
        bl = cs;
      }
    } while (s);

    is(buf, fjson, cs == SIZE_MAX ?
        "unpack '%s' in a single step" :
        "unpack '%s' in steps of %zu", repr, cs);

    /* pack test */
    mpack_parser_init(&parser, 0);
    b = buf;
    bl = MIN(cs, sizeof(buf));
    parser.data.p = fjson;
    do {
      s = mpack_unparse(&parser, &b, &bl, unparse_enter, unparse_exit);
      if (s) {
        assert(s == MPACK_EOF);
        bl = cs;
      }
    } while (s);

    cmp_mem(buf, fmsgpack, fmsgpacklen, cs == SIZE_MAX ?
        "pack '%s' in a single step" :
        "pack '%s' in steps of %zu", repr, cs);
  }
}

static void signed_positive_packs_with_unsigned_format(void)
{
  mpack_token_t tokbuf[0xff];
  size_t tokbufpos = 0;
  char mpackbuf[256];
  char *buf = mpackbuf;
  size_t buflen = sizeof(mpackbuf);
  mpack_tokbuf_t writer = MPACK_TOKBUF_INITIAL_VALUE;
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
  mpack_tokbuf_t reader;
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
  mpack_tokbuf_init(&reader);
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
    mpack_unpack_uint(toks[0]),
    mpack_unpack_uint(toks[1]),
    mpack_unpack_uint(toks[2]),
#ifndef FORCE_32BIT_INTS
    mpack_unpack_uint(toks[3])
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
  const uint8_t input[] = {0xc1};
  const char *inp = (const char *)input;
  size_t inplen = sizeof(input);
  mpack_parser_t parser;
  mpack_parser_init(&parser, 0);
  int res = mpack_parse(&parser, &inp, &inplen, parse_enter, parse_exit);
  ok(res == MPACK_ERROR, "0xc1 returns MPACK_ERROR");
}

static void parsing_very_deep_objects_returns_enomem(void)
{
  bufpos = 0;
  MPACK_PARSER_STRUCT(2) p2;
  MPACK_PARSER_STRUCT(3) p3;
  mpack_parser_init((mpack_parser_t *)&p2, 2);
  mpack_parser_init((mpack_parser_t *)&p3, 3);
  const uint8_t input[] = {0x91, 0x91, 0x01};  /* [[1]] */
  const char *b = (const char *)input;
  size_t bl = sizeof(input);
  ok(mpack_parse((mpack_parser_t *)&p2, &b, &bl, parse_enter, parse_exit)
      == MPACK_NOMEM && b == (char *)input + 2 && bl == 1,
  "parsing very deep objects return MPACK_ENOMEM");
  mpack_parser_copy((mpack_parser_t *)&p3, (mpack_parser_t *)&p2);
  ok(mpack_parse((mpack_parser_t *)&p3, &b, &bl, parse_enter, parse_exit)
      == MPACK_OK && b == (char *)input + 3 && bl == 0);
  is(buf, "[[1]]");
}

static void unparsing_very_deep_objects_returns_enomem(void)
{
  MPACK_PARSER_STRUCT(2) p2;
  MPACK_PARSER_STRUCT(3) p3;
  mpack_parser_init((mpack_parser_t *)&p2, 2);
  mpack_parser_init((mpack_parser_t *)&p3, 3);
  char input[] = "[[1]]";
  uint8_t buf[16];
  char *b = (char *)buf;
  size_t bl = sizeof(buf);
  p2.data.p = input;
  ok(mpack_unparse((mpack_parser_t *)&p2, &b, &bl, unparse_enter, unparse_exit)
      == MPACK_NOMEM && b == (char *)buf + 2 && bl == 14,
  "unparsing very deep objects return MPACK_ENOMEM");
  const uint8_t e2[] = {0x91, 0x91};
  cmp_mem(e2, buf, 2);
  mpack_parser_copy((mpack_parser_t *)&p3, (mpack_parser_t *)&p2);
  ok(mpack_unparse((mpack_parser_t *)&p3, &b, &bl, unparse_enter, unparse_exit)
      == MPACK_OK && b == (char *)buf + 3 && bl == 13);
  const uint8_t e3[] = {0x91, 0x91, 0x01};
  cmp_mem(e3, buf, 3);
}

static void parse_throw(void)
{
  bufpos = 0;
  mpack_parser_t parser;
  mpack_parser_init(&parser, 0);
  const uint8_t input[] = {0x91, 0x91, 0x01};  /* [[1]] */
  const char *b = (const char *)input;
  size_t bl = sizeof(input);
  ok(mpack_parse((mpack_parser_t *)&parser, &b, &bl, parse_enter, parse_exit)
      == MPACK_OK);
  b = (const char *)input;
  bl = sizeof(input);
  throw = true;
  ok(mpack_parse((mpack_parser_t *)&parser, &b, &bl, parse_enter, parse_exit)
      == MPACK_EXCEPTION);
  b = (const char *)input;
  bl = sizeof(input);
  throw = false;
  ok(mpack_parse((mpack_parser_t *)&parser, &b, &bl, parse_enter, parse_exit)
      == MPACK_EXCEPTION, "throw will invalidate the parser");
}

static void unparse_throw(void)
{
  mpack_parser_t parser;
  mpack_parser_init(&parser, 0);
  char input[] = "[[1]]";
  uint8_t buf[16];
  char *b = (char *)buf;
  size_t bl = sizeof(buf);
  parser.data.p = input;
  ok(mpack_unparse((mpack_parser_t *)&parser, &b, &bl, unparse_enter,
        unparse_exit) == MPACK_OK);
  b = (char *)buf;
  bl = sizeof(buf);
  throw = true;
  ok(mpack_unparse((mpack_parser_t *)&parser, &b, &bl, unparse_enter,
        unparse_exit) == MPACK_EXCEPTION);
  b = (char *)buf;
  bl = sizeof(buf);
  throw = false;
  ok(mpack_unparse((mpack_parser_t *)&parser, &b, &bl, unparse_enter,
        unparse_exit) == MPACK_EXCEPTION, "throw will invalidate the parser");
}

static void does_not_write_invalid_tokens(void)
{
  mpack_tokbuf_t writer;
  mpack_tokbuf_init(&writer);
  mpack_token_t tok, tok2;
  tok.type = 9999;
  char buf[64], *ptr = buf;
  size_t ptrlen = sizeof(buf);
  tok2 = mpack_pack_float_compat(5.5);
  tok2.length = 5;
  ok(mpack_write(&writer, &ptr, &ptrlen, &tok) == MPACK_ERROR,
      "does not write invalid tokens 1");
  mpack_tokbuf_init(&writer);
  ok(mpack_write(&writer, &ptr, &ptrlen, &tok2) == MPACK_ERROR,
      "does not write invalid tokens 2");
}

#define MSGPACK_BUFLEN 0xff

static void to_msgpack(const char *json, uint8_t **buf)
{
  size_t buflen = MSGPACK_BUFLEN;
  mpack_parser_t parser;

  mpack_parser_init(&parser, 0);
  parser.data.p = (char *)json;
  if (mpack_unparse(&parser, (char **)buf, &buflen, unparse_enter, unparse_exit) != MPACK_OK) abort();
}

static void rpc_copy_session_maintains_state(void)
{
  int d1, d2, d3;
  mpack_rpc_session_t *s2 = malloc(sizeof(MPACK_RPC_SESSION_STRUCT(2)));
  mpack_rpc_session_t *s3 = malloc(sizeof(MPACK_RPC_SESSION_STRUCT(3)));
  mpack_rpc_session_init(s2, 2);
  mpack_rpc_session_init(s3, 3);
  mpack_token_t tok;
  while (mpack_rpc_request_tok(s2, &tok, d(&d1)) != MPACK_OK);
  while (mpack_rpc_request_tok(s2, &tok, d(&d2)) != MPACK_OK);
  ok(mpack_rpc_request_tok(s2, &tok, d(NULL)) == MPACK_NOMEM);
  mpack_rpc_session_copy(s3, s2);
  while (mpack_rpc_request_tok(s3, &tok, d(&d3)) != MPACK_OK);
  size_t bl = 0xff;
  uint8_t buf[0xff];
  const char *b = (const char *)buf;
  mpack_rpc_message_t msg;
  to_msgpack("[1, 0, null, null]", (uint8_t **)&b);
  b = (const char *)buf;
  ok(mpack_rpc_receive(s3, &b, &bl, &msg) == MPACK_RPC_RESPONSE
      && msg.data.p == &d1);
  b = (const char *)buf;
  to_msgpack("[1, 1, null, null]", (uint8_t **)&b);
  b = (const char *)buf;
  ok(mpack_rpc_receive(s3, &b, &bl, &msg) == MPACK_RPC_RESPONSE
      && msg.data.p == &d2);
  b = (const char *)buf;
  to_msgpack("[1, 2, null, null]", (uint8_t **)&b);
  b = (const char *)buf;
  ok(mpack_rpc_receive(s3, &b, &bl, &msg) == MPACK_RPC_RESPONSE);
  free(s2);
  free(s3);
}

static void rpc_request_id_wrap(void)
{
  mpack_rpc_session_t session;
  mpack_rpc_session_init(&session, 0);
  mpack_token_t tok;
  /* produce request 0 */
  while (mpack_rpc_request_tok(&session, &tok, d(NULL)) != MPACK_OK);
  ok(session.slots[0].used && session.slots[0].msg.id == 0);
  /* jump request id to 0xffffffff */
  session.request_id = 0xffffffff;
  while (mpack_rpc_request_tok(&session, &tok, d(NULL)) != MPACK_OK);
  ok(session.slots[31].used && session.slots[31].msg.id == 0xffffffff);
  /* wrap back to 0 which is taken, resulting in 1 being used instead */
  while (mpack_rpc_request_tok(&session, &tok, d(NULL)) != MPACK_OK);
  ok(session.slots[1].used && session.slots[1].msg.id == 1);
}

static int reqdata;
static void rpc_check_outgoing(mpack_rpc_session_t *session,
    struct rpc_message *m, size_t cs, bool invert)
{
  uint8_t buf[MSGPACK_BUFLEN];
  uint8_t *b = buf;
  uint8_t rbuf[MSGPACK_BUFLEN];
  char *ptr = (char *)rbuf;
  to_msgpack(m->payload + 2, &b);

  int status = MPACK_EOF;
  while (status == MPACK_EOF) {
    size_t bl = cs;
    if (m->type == MPACK_RPC_REQUEST || m->payload[3] == '0') {
      status = mpack_rpc_request(session, &ptr, &bl, d(&reqdata));
    } else if (m->type == MPACK_RPC_RESPONSE || m->payload[3] == '1') {
      status = mpack_rpc_reply(session, &ptr, &bl, m->id);
    } else if (m->type == MPACK_RPC_NOTIFICATION || m->payload[3] == '2') {
      status = mpack_rpc_notify(session, &ptr, &bl);
    } else return; /* inverted error checking, return */
  }
  if (m->type == MPACK_RPC_REQUEST) {
    to_msgpack(m->method, (uint8_t **)&ptr);
    to_msgpack(m->args, (uint8_t **)&ptr);
  } else if (m->type == MPACK_RPC_RESPONSE) {
    to_msgpack(m->error, (uint8_t **)&ptr);
    to_msgpack(m->result, (uint8_t **)&ptr);
  } else if (m->type == MPACK_RPC_NOTIFICATION) {
    to_msgpack(m->method, (uint8_t **)&ptr);
    to_msgpack(m->args, (uint8_t **)&ptr);
  } else {
    if (!invert)
      ok(status == m->type, "%s: expected error matched", m->payload);
    return;
  }
  cmp_mem(buf, rbuf, (size_t)((uint8_t *)ptr - rbuf),
      cs == SIZE_MAX ? "%s: outgoing message matches in a single step" :
      "%s: outgoing message matches in steps of %zu%s", m->payload, cs,
      invert ? " (inverted)" : "");
}

static const char *get_error_string(int type)
{
  if (type == MPACK_RPC_EARRAY) return "invalid array";
  if (type == MPACK_RPC_EARRAYL) return "invalid array length";
  else if (type == MPACK_RPC_ETYPE) return "invalid message type"; 
  else if (type == MPACK_RPC_EMSGID) return "invalid message id"; 
  else if (type == MPACK_RPC_ERESPID) return "unmatched response id"; else abort();
}

static void rpc_check_incoming(mpack_rpc_session_t *session,
    struct rpc_message *m, size_t cs, bool invert)
{
  uint8_t buf[MSGPACK_BUFLEN];
  uint8_t *b = buf;
  uint8_t dbuf[MSGPACK_BUFLEN];
  uint8_t *db = dbuf;
  const char *ptr = (const char *)buf;
  to_msgpack(m->payload + 2, &b);
  mpack_rpc_message_t msg;
  int type = MPACK_EOF;
  while (type == MPACK_EOF) {
    size_t bl = cs;
    type = mpack_rpc_receive(session, &ptr, &bl, &msg);
  }
  bool result;
  if (m->type == MPACK_RPC_REQUEST) {
    result = msg.id == m->id;
    to_msgpack(m->method, &db);
    to_msgpack(m->args, &db);
  } else if (m->type == MPACK_RPC_RESPONSE) {
    to_msgpack(m->error, &db);
    to_msgpack(m->result, &db);
    result = msg.id == m->id;
    result = msg.data.p == &reqdata;
  } else if (m->type == MPACK_RPC_NOTIFICATION) {
    result = true;
    to_msgpack(m->method, &db);
    to_msgpack(m->args, &db);
  } else {
    if (!invert)
      ok(type == m->type, "%s: expected error matched(%s)", m->payload,
          get_error_string(type));
    return;
  }

  result = result && type == m->type;
  ok(result && !memcmp(dbuf, ptr, (size_t)(db - dbuf)),
      cs == SIZE_MAX ? "%s: incoming message matches in a single step" :
      "%s: incoming message matches in steps of %zu%s", m->payload, cs,
      invert ? " (inverted)" : "");
}

static void rpc_fixture_test(int fixture_idx)
{
  const struct rpc_fixture *fixture = rpc_fixtures + fixture_idx;

  for (size_t i = 0; i < ARRAY_SIZE(chunksizes); i++) {
    for (size_t j = 0; j < 2; j++) { /* test the library from both endpoints */ 
      mpack_rpc_session_t session;
      mpack_rpc_session_init(&session, fixture->capacity);
      for (size_t k = 0; k < fixture->count; k++) {
        size_t cs = chunksizes[i];
        struct rpc_message *msg = fixture->messages + k;
        if (msg->payload[0] == '<') {
          if (j) rpc_check_outgoing(&session, msg, cs, j);
          else rpc_check_incoming(&session, msg, cs, j);
        } else if (msg->payload[1] == '>') {
          if (j) rpc_check_incoming(&session, msg, cs, j);
          else rpc_check_outgoing(&session, msg, cs, j);
        }
      }
    }
  }
}

int main(void)
{
  for (int i = 0; i < fixture_count; i++) {
    fixture_test(fixtures, i);
  }
  signed_positive_packs_with_unsigned_format();
  positive_signed_format_unpacks_as_unsigned();
  unpacking_c1_returns_eread();
  parsing_very_deep_objects_returns_enomem();
  unparsing_very_deep_objects_returns_enomem();
  parse_throw();
  unparse_throw();
  does_not_write_invalid_tokens();
  rpc_copy_session_maintains_state();
  rpc_request_id_wrap();
  number_conv = true;  /* test using mpack_{pack,unpack}_number to do the
                          numeric conversions */
  for (int i = 0; i < rpc_fixture_count; i++) {
    rpc_fixture_test(i);
  }
  for (int i = 0; i < number_fixture_count; i++) {
    fixture_test(number_fixtures, i);
  }
  /* test size macros */
  ok(sizeof(MPACK_PARSER_STRUCT(2)) == MPACK_PARSER_STRUCT_SIZE(2));
  ok(sizeof(MPACK_PARSER_STRUCT(3)) == MPACK_PARSER_STRUCT_SIZE(3));
  ok(sizeof(MPACK_PARSER_STRUCT(9)) == MPACK_PARSER_STRUCT_SIZE(9));
  ok(sizeof(MPACK_PARSER_STRUCT(45)) == MPACK_PARSER_STRUCT_SIZE(45));
  ok(sizeof(MPACK_RPC_SESSION_STRUCT(2)) == MPACK_RPC_SESSION_STRUCT_SIZE(2));
  ok(sizeof(MPACK_RPC_SESSION_STRUCT(3)) == MPACK_RPC_SESSION_STRUCT_SIZE(3));
  ok(sizeof(MPACK_RPC_SESSION_STRUCT(9)) == MPACK_RPC_SESSION_STRUCT_SIZE(9));
  ok(sizeof(MPACK_RPC_SESSION_STRUCT(45)) == MPACK_RPC_SESSION_STRUCT_SIZE(45));
  done_testing();
}
