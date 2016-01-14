#include <string.h>

#include "core.h"

#define UNUSED(p) (void)p;
#define ADVANCE(buf, buflen) ((*buflen)--, (unsigned char)*((*buf)++))
#define TLEN(val, range_start) ((mpack_uint32_t)(1 << (val - range_start)))
#ifndef MIN
# define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#endif

static int mpack_rtoken(const char **buf, size_t *buflen,
    mpack_token_t *tok);
static int mpack_rpending(const char **b, size_t *nl, mpack_reader_t *r);
static int mpack_rvalue(mpack_token_type_t t, mpack_uint32_t l,
    const char **b, size_t *bl, mpack_token_t *tok);
static int mpack_rblob(mpack_token_type_t t, mpack_uint32_t l,
    const char **b, size_t *bl, mpack_token_t *tok);
static int mpack_wtoken(const mpack_token_t *tok, char **b, size_t *bl);
static int mpack_wpending(char **b, size_t *bl, const mpack_token_t *tok,
    mpack_writer_t *w);
static int mpack_wpint(char **b, size_t *bl, mpack_value_t v);
static int mpack_wnint(char **b, size_t *bl, mpack_value_t v);
static int mpack_wfloat(char **b, size_t *bl, const mpack_token_t *v);
static int mpack_wstr(char **buf, size_t *buflen, mpack_uint32_t len);
static int mpack_wbin(char **buf, size_t *buflen, mpack_uint32_t len);
static int mpack_wext(char **buf, size_t *buflen, int type,
    mpack_uint32_t len);
static int mpack_warray(char **buf, size_t *buflen, mpack_uint32_t len);
static int mpack_wmap(char **buf, size_t *buflen, mpack_uint32_t len);
static int mpack_w1(char **b, size_t *bl, mpack_uint32_t v);
static int mpack_w2(char **b, size_t *bl, mpack_uint32_t v);
static int mpack_w4(char **b, size_t *bl, mpack_uint32_t v);
static mpack_value_t mpack_byte(unsigned char b);
static int mpack_value(mpack_token_type_t t, mpack_uint32_t l,
    mpack_value_t v, mpack_token_t *tok);
static int mpack_blob(mpack_token_type_t t, mpack_uint32_t l, int et,
    mpack_token_t *tok);

MPACK_API void mpack_reader_init(mpack_reader_t *reader)
{
  reader->ppos = 0;
  reader->plen = 0;
  reader->passthrough = 0;
}

MPACK_API void mpack_writer_init(mpack_writer_t *writer)
{
  writer->pending = NULL;
}

MPACK_API int mpack_read(mpack_reader_t *reader, const char **buf,
    size_t *buflen, mpack_token_t *tok)
{
  int status;
  size_t initial_ppos, ptrlen, advanced;
  const char *ptr, *ptr_save;
  assert(*buf && *buflen);

  if (reader->passthrough) {
    /* pass data from str/bin/ext directly as a MPACK_TOKEN_CHUNK, adjusting
     * *buf and *buflen */
    tok->type = MPACK_TOKEN_CHUNK;
    tok->data.chunk_ptr = *buf;
    tok->length = MIN((mpack_uint32_t)*buflen, reader->passthrough);
    reader->passthrough -= tok->length;
    *buf += tok->length;
    *buflen -= tok->length;
    goto done;
  }

  initial_ppos = reader->ppos;

  if (reader->plen) {
    if (!mpack_rpending(buf, buflen, reader)) {
      return MPACK_EOF;
    }
    ptr = reader->pending;
    ptrlen = reader->ppos;
  } else {
    ptr = *buf;
    ptrlen = *buflen;
  }

  ptr_save = ptr;

  if ((status = mpack_rtoken(&ptr, &ptrlen, tok))) {
    if (status != MPACK_EOF) return MPACK_ERROR;
    /* need more data */
    assert(!reader->plen);
    /* read the remainder of *buf to reader->pending so it can be parsed
     * later with more data. only required when reader->plen == 0 or else
     * it would have been done already. */
    reader->plen = tok->length + 1;
    assert(reader->plen <= sizeof(reader->pending));
    reader->ppos = 0;
    status = mpack_rpending(buf, buflen, reader);
    assert(!status);
    return MPACK_EOF;
  }

  advanced = (size_t)(ptr - ptr_save) - initial_ppos;
  reader->plen = reader->ppos = 0;
  *buflen -= advanced; 
  *buf += advanced;

  if (tok->type > MPACK_TOKEN_MAP) {
    reader->passthrough = tok->length;
  }

done:
  return MPACK_OK;
}

MPACK_API int mpack_write(mpack_writer_t *writer, char **buf, size_t *buflen,
    const mpack_token_t *tok)
{
  int status;
  char *ptr;
  size_t ptrlen;
  assert(*buf && *buflen);

  if (writer->pending) {
    if (mpack_wpending(buf, buflen, tok, writer)) {
      writer->pending = NULL;
      return MPACK_OK;
    } else {
      assert(*buflen == 0);
      return MPACK_EOF;
    }
  }

  ptr = *buf;
  ptrlen = *buflen;

  if ((status = mpack_wtoken(tok, &ptr, &ptrlen))) {
    if (status != MPACK_EOF) return status;
    /* not enough space in *buf, write whatever we can and mark
     * the token as pending to be finished in a future call */
    writer->pending_written = 0;
    writer->pending = tok;
    status = mpack_wpending(buf, buflen, tok, writer);
    assert(!status && *buflen == 0);
    return MPACK_EOF;
  }

  *buflen -= (size_t)(ptr - *buf);
  *buf = ptr;
  return MPACK_OK;
}

static int mpack_rtoken(const char **buf, size_t *buflen,
    mpack_token_t *tok)
{
  unsigned char t = ADVANCE(buf, buflen);
  if (t < 0x80) {
    /* positive fixint */
    return mpack_value(MPACK_TOKEN_UINT, 1, mpack_byte(t), tok);
  } else if (t < 0x90) {
    /* fixmap */
    return mpack_blob(MPACK_TOKEN_MAP, (t & 0xf) * (unsigned)2, 0, tok);
  } else if (t < 0xa0) {
    /* fixarray */
    return mpack_blob(MPACK_TOKEN_ARRAY, t & 0xf, 0, tok);
  } else if (t < 0xc0) {
    /* fixstr */
    return mpack_blob(MPACK_TOKEN_STR, t & 0x1f, 0, tok);
  } else if (t < 0xe0) {
    switch (t) {
      case 0xc0:  /* nil */
        return mpack_value(MPACK_TOKEN_NIL, 0, mpack_byte(0), tok);
      case 0xc2:  /* false */
        return mpack_value(MPACK_TOKEN_BOOLEAN, 1, mpack_byte(0), tok);
      case 0xc3:  /* true */
        return mpack_value(MPACK_TOKEN_BOOLEAN, 1, mpack_byte(1), tok);
      case 0xc4:  /* bin 8 */
      case 0xc5:  /* bin 16 */
      case 0xc6:  /* bin 32 */
        return mpack_rblob(MPACK_TOKEN_BIN, TLEN(t, 0xc4), buf, buflen, tok);
      case 0xc7:  /* ext 8 */
      case 0xc8:  /* ext 16 */
      case 0xc9:  /* ext 32 */
        return mpack_rblob(MPACK_TOKEN_EXT, TLEN(t, 0xc7), buf, buflen, tok);
      case 0xca:  /* float 32 */
      case 0xcb:  /* float 64 */
        return mpack_rvalue(MPACK_TOKEN_FLOAT, TLEN(t, 0xc8), buf, buflen, tok);
      case 0xcc:  /* uint 8 */
      case 0xcd:  /* uint 16 */
      case 0xce:  /* uint 32 */
      case 0xcf:  /* uint 64 */
        return mpack_rvalue(MPACK_TOKEN_UINT, TLEN(t, 0xcc), buf, buflen, tok);
      case 0xd0:  /* int 8 */
      case 0xd1:  /* int 16 */
      case 0xd2:  /* int 32 */
      case 0xd3:  /* int 64 */
        return mpack_rvalue(MPACK_TOKEN_SINT, TLEN(t, 0xd0), buf, buflen, tok);
      case 0xd4:  /* fixext 1 */
      case 0xd5:  /* fixext 2 */
      case 0xd6:  /* fixext 4 */
      case 0xd7:  /* fixext 8 */
      case 0xd8:  /* fixext 16 */
        if (*buflen == 0) {
          /* require only one extra byte for the type code */
          tok->length = 1;
          return MPACK_EOF;
        }
        tok->length = TLEN(t, 0xd4);
        tok->type = MPACK_TOKEN_EXT;
        tok->data.ext_type = ADVANCE(buf, buflen);
        return MPACK_OK;
      case 0xd9:  /* str 8 */
      case 0xda:  /* str 16 */
      case 0xdb:  /* str 32 */
        return mpack_rblob(MPACK_TOKEN_STR, TLEN(t, 0xd9), buf, buflen, tok);
      case 0xdc:  /* array 16 */
      case 0xdd:  /* array 32 */
        return mpack_rblob(MPACK_TOKEN_ARRAY, TLEN(t, 0xdb), buf, buflen, tok);
      case 0xde:  /* map 16 */
      case 0xdf:  /* map 32 */
        return mpack_rblob(MPACK_TOKEN_MAP, TLEN(t, 0xdd), buf, buflen, tok);
      default:
        return MPACK_ERROR;
    }
  } else {
    /* negative fixint */
    return mpack_value(MPACK_TOKEN_SINT, 1, mpack_byte(t), tok);
  }
}

static int mpack_rpending(const char **buf, size_t *buflen,
    mpack_reader_t *reader)
{
  size_t count;
  assert(reader->ppos < reader->plen);
  count = MIN(reader->plen - reader->ppos, *buflen);
  memcpy(reader->pending + reader->ppos, *buf, count);
  reader->ppos += count;
  if (reader->ppos < reader->plen) {
    /* consume buffer since no token will be parsed yet. */
    *buf += *buflen;
    *buflen = 0;
    return 0;
  }
  return 1;
}

static int mpack_rvalue(mpack_token_type_t type, mpack_uint32_t remaining,
    const char **buf, size_t *buflen, mpack_token_t *tok)
{
  if (*buflen < remaining) {
    tok->length = remaining;
    return MPACK_EOF;
  }

  mpack_value(type, remaining, mpack_byte(0), tok);

  while (remaining) {
    mpack_uint32_t byte = ADVANCE(buf, buflen), byte_idx, byte_shift;
    byte_idx = (mpack_uint32_t)--remaining;
    byte_shift = (byte_idx % 4) * 8;
    tok->data.value.lo |= byte << byte_shift;
    if (remaining == 4) {
      /* unpacked the first half of a 8-byte value, shift what was parsed to the
       * "hi" field and reset "lo" for the trailing 4 bytes. */
      tok->data.value.hi = tok->data.value.lo;
      tok->data.value.lo = 0;
    }
  }

  if (type == MPACK_TOKEN_SINT) {
    mpack_uint32_t hi = tok->data.value.hi;
    mpack_uint32_t lo = tok->data.value.lo;
    mpack_uint32_t msb = (tok->length == 8 && hi >> 31) ||
                         (tok->length == 4 && lo >> 31) ||
                         (tok->length == 2 && lo >> 15) ||
                         (tok->length == 1 && lo >> 7);
    if (!msb) {
      tok->type = MPACK_TOKEN_UINT;
    }
  }

  return MPACK_OK;
}

static int mpack_rblob(mpack_token_type_t type, mpack_uint32_t tlen,
    const char **buf, size_t *buflen, mpack_token_t *tok)
{
  mpack_token_t l;
  mpack_uint32_t required = tlen + (type == MPACK_TOKEN_EXT ? 1 : 0);

  if (*buflen < required) {
    tok->length = required;
    return MPACK_EOF;
  }

  l.data.value.lo = 0;
  mpack_rvalue(MPACK_TOKEN_UINT, tlen, buf, buflen, &l);
  tok->type = type;
  tok->length = l.data.value.lo;

  if (type == MPACK_TOKEN_EXT) {
    tok->data.ext_type = ADVANCE(buf, buflen);
  } else if (type == MPACK_TOKEN_MAP) {
    tok->length *= 2;
  }

  return MPACK_OK;
}

static int mpack_wtoken(const mpack_token_t *tok, char **buf,
    size_t *buflen)
{
  switch (tok->type) {
    case MPACK_TOKEN_NIL:
      return mpack_w1(buf, buflen, 0xc0);
    case MPACK_TOKEN_BOOLEAN:
      return mpack_w1(buf, buflen, tok->data.value.lo ? 0xc3 : 0xc2);
    case MPACK_TOKEN_UINT:
      return mpack_wpint(buf, buflen, tok->data.value);
    case MPACK_TOKEN_SINT:
      return mpack_wnint(buf, buflen, tok->data.value);
    case MPACK_TOKEN_FLOAT:
      return mpack_wfloat(buf, buflen, tok);
    case MPACK_TOKEN_BIN:
      return mpack_wbin(buf, buflen, tok->length);
    case MPACK_TOKEN_STR:
      return mpack_wstr(buf, buflen, tok->length);
    case MPACK_TOKEN_EXT:
      return mpack_wext(buf, buflen, tok->data.ext_type, tok->length);
    case MPACK_TOKEN_ARRAY:
      return mpack_warray(buf, buflen, tok->length);
    case MPACK_TOKEN_MAP:
      return mpack_wmap(buf, buflen, tok->length / 2);
    case MPACK_TOKEN_CHUNK:
      if (*buflen < tok->length) {
        return MPACK_EOF;
      }
      memcpy(*buf, tok->data.chunk_ptr, tok->length);
      *buf += tok->length;
      *buflen -= tok->length;
      return MPACK_OK;
    default:
      return MPACK_ERROR;
  }
}

static int mpack_wpending(char **buf, size_t *buflen,
    const mpack_token_t *tok, mpack_writer_t *writer)
{
  int rv;
  char tmp[MPACK_MAX_TOKEN_LEN], *ptr = tmp;
  size_t pending_len, count, tmplen, ptrlen = sizeof(tmp);
  assert(writer->pending == tok);

  if (tok->type == MPACK_TOKEN_CHUNK) {
    assert(writer->pending_written < tok->length);
    pending_len = tok->length - writer->pending_written;
    ptr = (char *)tok->data.chunk_ptr;
  } else {
    int status = mpack_wtoken(tok, &ptr, &ptrlen);
    UNUSED(status);
    assert(status == MPACK_OK);
    tmplen = sizeof(tmp) - ptrlen;
    assert(writer->pending_written < tmplen);
    pending_len = tmplen - writer->pending_written;
    ptr = tmp;
  }
  count = MIN(pending_len, *buflen);
  memcpy(*buf, ptr + writer->pending_written, count);
  rv = pending_len <= *buflen;
  writer->pending_written += count;
  *buflen -= count;
  *buf += count;
  return rv;
}

static int mpack_wpint(char **buf, size_t *buflen, mpack_value_t val)
{
  mpack_uint32_t hi = val.hi;
  mpack_uint32_t lo = val.lo;

  if (hi) {
    /* uint 64 */
    return mpack_w1(buf, buflen, 0xcf) ||
           mpack_w4(buf, buflen, hi)   ||
           mpack_w4(buf, buflen, lo);
  } else if (lo > 0xffff) {
    /* uint 32 */
    return mpack_w1(buf, buflen, 0xce) ||
           mpack_w4(buf, buflen, lo);
  } else if (lo > 0xff) {
    /* uint 16 */
    return mpack_w1(buf, buflen, 0xcd) ||
           mpack_w2(buf, buflen, lo);
  } else if (lo > 0x7f) {
    /* uint 8 */
    return mpack_w1(buf, buflen, 0xcc) ||
           mpack_w1(buf, buflen, lo);
  } else {
    return mpack_w1(buf, buflen, lo);
  }
}

static int mpack_wnint(char **buf, size_t *buflen, mpack_value_t val)
{
  mpack_uint32_t hi = val.hi;
  mpack_uint32_t lo = val.lo;

  if (lo < 0x80000000) {
    /* int 64 */
    return mpack_w1(buf, buflen, 0xd3) ||
           mpack_w4(buf, buflen, hi)   ||
           mpack_w4(buf, buflen, lo);
  } else if (lo < 0xffff7fff) {
    /* int 32 */
    return mpack_w1(buf, buflen, 0xd2) ||
           mpack_w4(buf, buflen, lo);
  } else if (lo < 0xffffff7f) {
    /* int 16 */
    return mpack_w1(buf, buflen, 0xd1) ||
           mpack_w2(buf, buflen, lo);
  } else if (lo < 0xffffffe0) {
    /* int 8 */
    return mpack_w1(buf, buflen, 0xd0) ||
           mpack_w1(buf, buflen, lo);
  } else {
    /* negative fixint */
    return mpack_w1(buf, buflen, (mpack_uint32_t)(0x100 + lo));
  }
}

static int mpack_wfloat(char **buf, size_t *buflen,
    const mpack_token_t *tok)
{
  if (tok->length == 4) {
    return mpack_w1(buf, buflen, 0xca) ||
           mpack_w4(buf, buflen, tok->data.value.lo);
  } else if (tok->length == 8) {
    return mpack_w1(buf, buflen, 0xcb) ||
           mpack_w4(buf, buflen, tok->data.value.hi) ||
           mpack_w4(buf, buflen, tok->data.value.lo);
  } else {
    return MPACK_ERROR;
  }
}

static int mpack_wstr(char **buf, size_t *buflen, mpack_uint32_t len)
{
  if (len < 0x20) {
    return mpack_w1(buf, buflen, 0xa0 | len);
  } else if (len < 0x100) {
    return mpack_w1(buf, buflen, 0xd9) ||
           mpack_w1(buf, buflen, len);
  } else if (len < 0x10000) {
    return mpack_w1(buf, buflen, 0xda) ||
           mpack_w2(buf, buflen, len);
  } else {
    return mpack_w1(buf, buflen, 0xdb) ||
           mpack_w4(buf, buflen, len);
  }
}

static int mpack_wbin(char **buf, size_t *buflen, mpack_uint32_t len)
{
  if (len < 0x100) {
    return mpack_w1(buf, buflen, 0xc4) ||
           mpack_w1(buf, buflen, len);
  } else if (len < 0x10000) {
    return mpack_w1(buf, buflen, 0xc5) ||
           mpack_w2(buf, buflen, len);
  } else {
    return mpack_w1(buf, buflen, 0xc6) ||
           mpack_w4(buf, buflen, len);
  }
}

static int mpack_wext(char **buf, size_t *buflen, int type,
    mpack_uint32_t len)
{
  mpack_uint32_t t;
  assert(type >= 0 && type < 0x80);
  t = (mpack_uint32_t)type;
  switch (len) {
    case 1: mpack_w1(buf, buflen, 0xd4); return mpack_w1(buf, buflen, t);
    case 2: mpack_w1(buf, buflen, 0xd5); return mpack_w1(buf, buflen, t);
    case 4: mpack_w1(buf, buflen, 0xd6); return mpack_w1(buf, buflen, t);
    case 8: mpack_w1(buf, buflen, 0xd7); return mpack_w1(buf, buflen, t);
    case 16: mpack_w1(buf, buflen, 0xd8); return mpack_w1(buf, buflen, t);
    default:
      if (len < 0x100) {
        return mpack_w1(buf, buflen, 0xc7) ||
               mpack_w1(buf, buflen, len)  ||
               mpack_w1(buf, buflen, t);
      } else if (len < 0x10000) {
        return mpack_w1(buf, buflen, 0xc8) ||
               mpack_w2(buf, buflen, len)  ||
               mpack_w1(buf, buflen, t);
      } else {
        return mpack_w1(buf, buflen, 0xc9) ||
               mpack_w4(buf, buflen, len)  ||
               mpack_w1(buf, buflen, t);
      }
  }
}

static int mpack_warray(char **buf, size_t *buflen, mpack_uint32_t len)
{
  if (len < 0x10) {
    return mpack_w1(buf, buflen, 0x90 | len);
  } else if (len < 0x10000) {
    return mpack_w1(buf, buflen, 0xdc) ||
           mpack_w2(buf, buflen, len);
  } else {
    return mpack_w1(buf, buflen, 0xdd) ||
           mpack_w4(buf, buflen, len);
  }
}

static int mpack_wmap(char **buf, size_t *buflen, mpack_uint32_t len)
{
  if (len < 0x10) {
    return mpack_w1(buf, buflen, 0x80 | len);
  } else if (len < 0x10000) {
    return mpack_w1(buf, buflen, 0xde) ||
           mpack_w2(buf, buflen, len);
  } else {
    return mpack_w1(buf, buflen, 0xdf) ||
           mpack_w4(buf, buflen, len);
  }
}

static int mpack_w1(char **b, size_t *bl, mpack_uint32_t v)
{
  if (!*bl) return MPACK_EOF;
  (*bl)--;
  *(*b)++ = (char)(v & 0xff);
  return MPACK_OK;
}

static int mpack_w2(char **b, size_t *bl, mpack_uint32_t v)
{
  if (*bl < 2) return MPACK_EOF;
  *bl -= 2;
  *(*b)++ = (char)((v >> 8) & 0xff);
  *(*b)++ = (char)(v & 0xff);
  return MPACK_OK;
}

static int mpack_w4(char **b, size_t *bl, mpack_uint32_t v)
{
  if (*bl < 4) return MPACK_EOF;
  *bl -= 4;
  *(*b)++ = (char)((v >> 24) & 0xff);
  *(*b)++ = (char)((v >> 16) & 0xff);
  *(*b)++ = (char)((v >> 8) & 0xff);
  *(*b)++ = (char)(v & 0xff);
  return MPACK_OK;
}

static int mpack_value(mpack_token_type_t type, mpack_uint32_t length,
    mpack_value_t value, mpack_token_t *tok)
{
  tok->type = type;
  tok->length = length;
  tok->data.value = value;
  return MPACK_OK;
}

static int mpack_blob(mpack_token_type_t type, mpack_uint32_t length,
    int ext_type, mpack_token_t *tok)
{
  tok->type = type;
  tok->length = length;
  tok->data.ext_type = ext_type;
  return MPACK_OK;
}

static mpack_value_t mpack_byte(unsigned char byte)
{
  mpack_value_t rv;
  rv.lo = byte;
  rv.hi = 0;
  return rv;
}
