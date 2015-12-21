#include <string.h>

#include "mpack_core.h"

#define UNUSED(p) (void)p;
#define ADVANCE(buf, buflen) ((*buflen)--, (unsigned char)*((*buf)++))
#define TLEN(val, range_start) ((mpack_uint32_t)(1 << (val - range_start)))
#ifndef MIN
# define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#endif

enum {
  MPACK_ERROR = -1,
  MPACK_OK = 0,
  MPACK_EOF = 1
};

static int mpack_read_token(const char **buf, size_t *buflen,
    mpack_token_t *tok);
static int value(mpack_token_type_t t, mpack_uint32_t l,
    mpack_value_t v, mpack_token_t *tok);
static int blob(mpack_token_type_t t, mpack_uint32_t l, int et,
    mpack_token_t *tok);
static int read_value(mpack_token_type_t t, mpack_uint32_t l,
    const char **b, size_t *bl, mpack_token_t *tok);
static int read_blob(mpack_token_type_t t, mpack_uint32_t l,
    const char **b, size_t *bl, mpack_token_t *tok);
static int mpack_write_token(const mpack_token_t *tok, char **b, size_t *bl);
static int mpack_write_pending(char **b, size_t *bl, const mpack_token_t *tok,
    mpack_writer_t *w);
static int write1(char **b, size_t *bl, mpack_uint32_t v);
static int write2(char **b, size_t *bl, mpack_uint32_t v);
static int write4(char **b, size_t *bl, mpack_uint32_t v);
static int write_pint(char **b, size_t *bl, mpack_value_t v);
static int write_nint(char **b, size_t *bl, mpack_value_t v);
static int write_float(char **b, size_t *bl, const mpack_token_t *v);
static int write_str(char **buf, size_t *buflen, mpack_uint32_t len);
static int write_bin(char **buf, size_t *buflen, mpack_uint32_t len);
static int write_ext(char **buf, size_t *buflen, int type, mpack_uint32_t len);
static int write_array(char **buf, size_t *buflen, mpack_uint32_t len);
static int write_map(char **buf, size_t *buflen, mpack_uint32_t len);
static mpack_value_t byte(unsigned char b);

void mpack_reader_init(mpack_reader_t *reader)
{
  reader->pending_cnt = 0;
  reader->passthrough = 0;
}

void mpack_writer_init(mpack_writer_t *writer)
{
  writer->pending = NULL;
}

size_t mpack_read(const char **buf, size_t *buflen, mpack_token_t *tokbuf,
    size_t tokbuflen, mpack_reader_t *reader)
{
  /* TODO(tarruda): Function needs cleanup */
  mpack_token_t *tok = tokbuf, *tokend = tokbuf + tokbuflen;
  assert(*buflen && !MPACK_ERRORED(*buflen));

  for (; *buflen && tok < tokend; tok++) {
    int status;
    size_t ppos, ptrlen;
    const char *ptr, *ptr_save;

    if (reader->passthrough) {
      /* pass data from str/bin/ext directly as a MPACK_TOKEN_CHUNK, adjusting
       * *buf and *buflen */
      tok->type = MPACK_TOKEN_CHUNK;
      tok->data.chunk_ptr = *buf;
      tok->length = MIN((mpack_uint32_t)*buflen, reader->passthrough);
      reader->passthrough -= tok->length;
      *buf += tok->length;
      *buflen -= tok->length;
      continue;
    }

    ppos = reader->pending_cnt;

    if (ppos) {
      /* If there's pending data, concatenate with *buf so it can be parsed as a
       * whole */
      size_t count = MIN(sizeof(reader->pending) - ppos, *buflen);
      memcpy(reader->pending + ppos, *buf, count);
      reader->pending_cnt += count;
      ptr = reader->pending;
      ptrlen = reader->pending_cnt;
    } else {
      ptr = *buf;
      ptrlen = *buflen;
    }

    ptr_save = ptr;

    if ((status = mpack_read_token(&ptr, &ptrlen, tok))) {
      if (status == MPACK_ERROR) {
        return MPACK_EREAD;
      }
      /* need more data */
      if (!ppos) {
        /* copy the remainder of *buf to reader->pending so it can be parsed
         * later with more data. only necessary if !ppos since otherwise it
         * would have been copied already. */
        assert(*buflen < sizeof(reader->pending));
        memcpy(reader->pending + reader->pending_cnt, *buf, *buflen);
        reader->pending_cnt += *buflen;
      }
      *buf += *buflen;
      *buflen = 0;
      break;
    }

    reader->pending_cnt = 0;
    *buflen -= (size_t)(ptr - ptr_save) - ppos; 
    *buf = ptr;

    if (tok->type > MPACK_TOKEN_MAP) {
      reader->passthrough = tok->length;
    }
  }

  return (size_t)(tok - tokbuf);
}

size_t mpack_write(char **buf, size_t *buflen, const mpack_token_t *tokbuf,
    size_t tokbuflen, mpack_writer_t *writer)
{
  const mpack_token_t *tok = tokbuf, *tokend = tokbuf + tokbuflen;
  assert(*buflen);

  if (writer->pending && mpack_write_pending(buf, buflen, tok, writer)) {
    tok++;
    writer->pending = NULL;
  }

  for (; *buflen && tok < tokend; tok++) {
    int status;
    char *ptr = *buf;
    size_t ptrlen = *buflen;

    if ((status = mpack_write_token(tok, &ptr, &ptrlen))) {
      assert(status == MPACK_EOF);
      /* not enough space in *buf, write whatever we can and mark
       * the token as pending to be finished in a future call */
      writer->pending_written = 0;
      writer->pending = tok;
      (void)mpack_write_pending(buf, buflen, tok, writer);
      break;
    }

    *buflen -= (size_t)(ptr - *buf);
    *buf = ptr;
  }

  return (size_t)(tok - tokbuf);
}

static int mpack_read_token(const char **buf, size_t *buflen,
    mpack_token_t *tok)
{
  unsigned char t = ADVANCE(buf, buflen);
  if (t < 0x80) {
    /* positive fixint */
    return value(MPACK_TOKEN_UINT, 1, byte(t), tok);
  } else if (t < 0x90) {
    /* fixmap */
    return blob(MPACK_TOKEN_MAP, (t & 0xf) * (unsigned)2, 0, tok);
  } else if (t < 0xa0) {
    /* fixarray */
    return blob(MPACK_TOKEN_ARRAY, t & 0xf, 0, tok);
  } else if (t < 0xc0) {
    /* fixstr */
    return blob(MPACK_TOKEN_STR, t & 0x1f, 0, tok);
  } else if (t < 0xe0) {
    switch (t) {
      case 0xc0:  /* nil */
        return value(MPACK_TOKEN_NIL, 0, byte(0), tok);
      case 0xc2:  /* false */
        return value(MPACK_TOKEN_BOOLEAN, 1, byte(0), tok);
      case 0xc3:  /* true */
        return value(MPACK_TOKEN_BOOLEAN, 1, byte(1), tok);
      case 0xc4:  /* bin 8 */
      case 0xc5:  /* bin 16 */
      case 0xc6:  /* bin 32 */
        return read_blob(MPACK_TOKEN_BIN, TLEN(t, 0xc4), buf, buflen, tok);
      case 0xc7:  /* ext 8 */
      case 0xc8:  /* ext 16 */
      case 0xc9:  /* ext 32 */
        return read_blob(MPACK_TOKEN_EXT, TLEN(t, 0xc7), buf, buflen, tok);
      case 0xca:  /* float 32 */
      case 0xcb:  /* float 64 */
        return read_value(MPACK_TOKEN_FLOAT, TLEN(t, 0xc8), buf, buflen, tok);
      case 0xcc:  /* uint 8 */
      case 0xcd:  /* uint 16 */
      case 0xce:  /* uint 32 */
      case 0xcf:  /* uint 64 */
        return read_value(MPACK_TOKEN_UINT, TLEN(t, 0xcc), buf, buflen, tok);
      case 0xd0:  /* int 8 */
      case 0xd1:  /* int 16 */
      case 0xd2:  /* int 32 */
      case 0xd3:  /* int 64 */
        return read_value(MPACK_TOKEN_SINT, TLEN(t, 0xd0), buf, buflen, tok);
      case 0xd4:  /* fixext 1 */
      case 0xd5:  /* fixext 2 */
      case 0xd6:  /* fixext 4 */
      case 0xd7:  /* fixext 8 */
      case 0xd8:  /* fixext 16 */
        if (*buflen) {
          tok->type = MPACK_TOKEN_EXT;
          tok->length = TLEN(t, 0xd4);
          tok->data.ext_type = ADVANCE(buf, buflen);
          return MPACK_OK;
        }
        return MPACK_EOF;
      case 0xd9:  /* str 8 */
      case 0xda:  /* str 16 */
      case 0xdb:  /* str 32 */
        return read_blob(MPACK_TOKEN_STR, TLEN(t, 0xd9), buf, buflen, tok);
      case 0xdc:  /* array 16 */
      case 0xdd:  /* array 32 */
        return read_blob(MPACK_TOKEN_ARRAY, TLEN(t, 0xdb), buf, buflen, tok);
      case 0xde:  /* map 16 */
      case 0xdf:  /* map 32 */
        return read_blob(MPACK_TOKEN_MAP, TLEN(t, 0xdd), buf, buflen, tok);
      default:
        return MPACK_ERROR;
    }
  } else {
    /* negative fixint */
    return value(MPACK_TOKEN_SINT, 1, byte(t), tok);
  }
}

static int value(mpack_token_type_t type, mpack_uint32_t length,
    mpack_value_t value, mpack_token_t *tok)
{
  tok->type = type;
  tok->length = length;
  tok->data.value = value;
  return MPACK_OK;
}

static int blob(mpack_token_type_t type, mpack_uint32_t length,
    int ext_type, mpack_token_t *tok)
{
  tok->type = type;
  tok->length = length;
  tok->data.ext_type = ext_type;
  return MPACK_OK;
}

static int read_value(mpack_token_type_t type, mpack_uint32_t remaining,
    const char **buf, size_t *buflen, mpack_token_t *tok)
{
  if (*buflen < remaining) {
    return MPACK_EOF;
  }

  value(type, remaining, byte(0), tok);

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

static int read_blob(mpack_token_type_t type, mpack_uint32_t tlen,
    const char **buf, size_t *buflen, mpack_token_t *tok)
{
  mpack_token_t l;
  size_t required = tlen + (type == MPACK_TOKEN_EXT ? 1 : 0);

  if (*buflen < required) {
    return MPACK_EOF;
  }

  l.data.value.lo = 0;
  read_value(MPACK_TOKEN_UINT, tlen, buf, buflen, &l);
  tok->type = type;
  tok->length = l.data.value.lo;

  if (type == MPACK_TOKEN_EXT) {
    tok->data.ext_type = ADVANCE(buf, buflen);
  } else if (type == MPACK_TOKEN_MAP) {
    tok->length *= 2;
  }

  return MPACK_OK;
}

static int mpack_write_token(const mpack_token_t *tok, char **buf,
    size_t *buflen)
{
  switch (tok->type) {
    case MPACK_TOKEN_NIL:
      return write1(buf, buflen, 0xc0);
    case MPACK_TOKEN_BOOLEAN:
      return write1(buf, buflen, tok->data.value.lo ? 0xc3 : 0xc2);
    case MPACK_TOKEN_UINT:
      return write_pint(buf, buflen, tok->data.value);
    case MPACK_TOKEN_SINT:
      return write_nint(buf, buflen, tok->data.value);
    case MPACK_TOKEN_FLOAT:
      return write_float(buf, buflen, tok);
    case MPACK_TOKEN_BIN:
      return write_bin(buf, buflen, tok->length);
    case MPACK_TOKEN_STR:
      return write_str(buf, buflen, tok->length);
    case MPACK_TOKEN_EXT:
      return write_ext(buf, buflen, tok->data.ext_type, tok->length);
    case MPACK_TOKEN_ARRAY:
      return write_array(buf, buflen, tok->length);
    case MPACK_TOKEN_MAP:
      return write_map(buf, buflen, tok->length);
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

static int mpack_write_pending(char **buf, size_t *buflen,
    const mpack_token_t *tok, mpack_writer_t *writer)
{
  int rv;
  char tmp[MPACK_MAX_TOKEN_SIZE], *ptr = tmp;
  size_t pending_len, count, tmplen, ptrlen = sizeof(tmp);
  assert(writer->pending == tok);

  if (tok->type == MPACK_TOKEN_CHUNK) {
    assert(writer->pending_written < tok->length);
    pending_len = tok->length - writer->pending_written;
    ptr = (char *)tok->data.chunk_ptr;
  } else {
    int status = mpack_write_token(tok, &ptr, &ptrlen);
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

static int write_pint(char **buf, size_t *buflen, mpack_value_t val)
{
  mpack_uint32_t hi = val.hi;
  mpack_uint32_t lo = val.lo;

  if (hi) {
    /* uint 64 */
    return write1(buf, buflen, 0xcf) ||
           write4(buf, buflen, hi)   ||
           write4(buf, buflen, lo);
  } else if (lo > 0xffff) {
    /* uint 32 */
    return write1(buf, buflen, 0xce) ||
           write4(buf, buflen, lo);
  } else if (lo > 0xff) {
    /* uint 16 */
    return write1(buf, buflen, 0xcd) ||
           write2(buf, buflen, lo);
  } else if (lo > 0x7f) {
    /* uint 8 */
    return write1(buf, buflen, 0xcc) ||
           write1(buf, buflen, lo);
  } else {
    return write1(buf, buflen, lo);
  }
}

static int write_nint(char **buf, size_t *buflen, mpack_value_t val)
{
  mpack_uint32_t hi = val.hi;
  mpack_uint32_t lo = val.lo;

  if (lo < 0x80000000) {
    /* int 64 */
    return write1(buf, buflen, 0xd3) ||
           write4(buf, buflen, hi)   ||
           write4(buf, buflen, lo);
  } else if (lo < 0xffff7fff) {
    /* int 32 */
    return write1(buf, buflen, 0xd2) ||
           write4(buf, buflen, lo);
  } else if (lo < 0xffffff7f) {
    /* int 16 */
    return write1(buf, buflen, 0xd1) ||
           write2(buf, buflen, lo);
  } else if (lo < 0xffffffe0) {
    /* int 8 */
    return write1(buf, buflen, 0xd0) ||
           write1(buf, buflen, lo);
  } else {
    /* negative fixint */
    return write1(buf, buflen, (mpack_uint32_t)(0x100 + lo));
  }
}

static int write_float(char **buf, size_t *buflen, const mpack_token_t *tok)
{
  if (tok->length == 4) {
    return write1(buf, buflen, 0xca) ||
           write4(buf, buflen, tok->data.value.lo);
  } else if (tok->length == 8) {
    return write1(buf, buflen, 0xcb) ||
           write4(buf, buflen, tok->data.value.hi) ||
           write4(buf, buflen, tok->data.value.lo);
  } else {
    return MPACK_ERROR;
  }
}

static int write_str(char **buf, size_t *buflen, mpack_uint32_t len)
{
  if (len < 0x20) {
    return write1(buf, buflen, 0xa0 | len);
  } else if (len < 0x100) {
    return write1(buf, buflen, 0xd9) ||
           write1(buf, buflen, len);
  } else if (len < 0x10000) {
    return write1(buf, buflen, 0xda) ||
           write2(buf, buflen, len);
  } else {
    return write1(buf, buflen, 0xdb) ||
           write4(buf, buflen, len);
  }
}

static int write_bin(char **buf, size_t *buflen, mpack_uint32_t len)
{
  if (len < 0x100) {
    return write1(buf, buflen, 0xc4) ||
           write1(buf, buflen, len);
  } else if (len < 0x10000) {
    return write1(buf, buflen, 0xc5) ||
           write2(buf, buflen, len);
  } else {
    return write1(buf, buflen, 0xc6) ||
           write4(buf, buflen, len);
  }
}

static int write_ext(char **buf, size_t *buflen, int type, mpack_uint32_t len)
{
  mpack_uint32_t t;
  assert(type >= 0 && type < 0x80);
  t = (mpack_uint32_t)type;
  switch (len) {
    case 1: write1(buf, buflen, 0xd4); return write1(buf, buflen, t);
    case 2: write1(buf, buflen, 0xd5); return write1(buf, buflen, t);
    case 4: write1(buf, buflen, 0xd6); return write1(buf, buflen, t);
    case 8: write1(buf, buflen, 0xd7); return write1(buf, buflen, t);
    case 16: write1(buf, buflen, 0xd8); return write1(buf, buflen, t);
    default:
      if (len < 0x100) {
        return write1(buf, buflen, 0xc7) ||
               write1(buf, buflen, len)  ||
               write1(buf, buflen, t);
      } else if (len < 0x10000) {
        return write1(buf, buflen, 0xc8) ||
               write2(buf, buflen, len)  ||
               write1(buf, buflen, t);
      } else {
        return write1(buf, buflen, 0xc9) ||
               write4(buf, buflen, len)  ||
               write1(buf, buflen, t);
      }
  }
}

static int write_array(char **buf, size_t *buflen, mpack_uint32_t len)
{
  if (len < 0x10) {
    return write1(buf, buflen, 0x90 | len);
  } else if (len < 0x10000) {
    return write1(buf, buflen, 0xdc) ||
           write2(buf, buflen, len);
  } else {
    return write1(buf, buflen, 0xdd) ||
           write4(buf, buflen, len);
  }
}

static int write_map(char **buf, size_t *buflen, mpack_uint32_t len)
{
  if (len < 0x10) {
    return write1(buf, buflen, 0x80 | len);
  } else if (len < 0x10000) {
    return write1(buf, buflen, 0xde) ||
           write2(buf, buflen, len);
  } else {
    return write1(buf, buflen, 0xdf) ||
           write4(buf, buflen, len);
  }
}

static int write1(char **b, size_t *bl, mpack_uint32_t v)
{
  if (!*bl) return MPACK_EOF;
  (*bl)--;
  *(*b)++ = (char)(v & 0xff);
  return MPACK_OK;
}

static int write2(char **b, size_t *bl, mpack_uint32_t v)
{
  if (*bl < 2) return MPACK_EOF;
  *bl -= 2;
  *(*b)++ = (char)((v >> 8) & 0xff);
  *(*b)++ = (char)(v & 0xff);
  return MPACK_OK;
}

static int write4(char **b, size_t *bl, mpack_uint32_t v)
{
  if (*bl < 4) return MPACK_EOF;
  *bl -= 4;
  *(*b)++ = (char)((v >> 24) & 0xff);
  *(*b)++ = (char)((v >> 16) & 0xff);
  *(*b)++ = (char)((v >> 8) & 0xff);
  *(*b)++ = (char)(v & 0xff);
  return MPACK_OK;
}

static mpack_value_t byte(unsigned char byte)
{
  mpack_value_t rv;
  rv.lo = byte;
  rv.hi = 0;
  return rv;
}

