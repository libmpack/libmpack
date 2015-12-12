#include <assert.h>

#include "mpack.h"

#define UNUSED(p) (void)p;
#define ADVANCE(buf, buflen) ((*buflen)--, (unsigned char)*((*buf)++))
#define TLEN(val, range_start) ((mpack_uint32_t)(1 << (val - range_start)))
#ifndef MIN
# define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#endif

static mpack_token_t value(mpack_token_type_t t, mpack_uint32_t l,
    mpack_value_t v);
static mpack_token_t blob(mpack_token_type_t t, mpack_uint32_t l, int et);
static mpack_token_t yield(void);
static mpack_token_t read_value(mpack_token_type_t t, mpack_uint32_t l,
    const char **b, size_t *bl);
static mpack_token_t read_blob(mpack_token_type_t t, mpack_uint32_t l,
    const char **b, size_t *bl);
static mpack_value_t byte(unsigned char b);
/* write helpers */
static void write1(char **b, size_t *bl, mpack_uint32_t v);
static void write2(char **b, size_t *bl, mpack_uint32_t v);
static void write4(char **b, size_t *bl, mpack_uint32_t v);
static void write_pint(char **b, size_t *bl, mpack_value_t v);
static void write_nint(char **b, size_t *bl, mpack_value_t v);
static void write_float(char **b, size_t *bl, mpack_token_t v);
static void write_str(char **buf, size_t *buflen, mpack_uint32_t len);
static void write_bin(char **buf, size_t *buflen, mpack_uint32_t len);
static void write_ext(char **buf, size_t *buflen, int type, mpack_uint32_t len);
static void write_array(char **buf, size_t *buflen, mpack_uint32_t len);
static void write_map(char **buf, size_t *buflen, mpack_uint32_t len);

mpack_token_t mpack_read(const char **buf, size_t *buflen)
{
  unsigned char t = ADVANCE(buf, buflen);
  if (t < 0x80) {
    /* positive fixint */
    return value(MPACK_TOKEN_UINT, 1, byte(t));
  } else if (t < 0x90) {
    /* fixmap */
    return blob(MPACK_TOKEN_MAP, (t & 0xf) * (unsigned)2, 0);
  } else if (t < 0xa0) {
    /* fixarray */
    return blob(MPACK_TOKEN_ARRAY, t & 0xf, 0);
  } else if (t < 0xc0) {
    /* fixstr */
    return blob(MPACK_TOKEN_STR, t & 0x1f, 0);
  } else if (t < 0xe0) {
    switch (t) {
      case 0xc0:  /* nil */
        return value(MPACK_TOKEN_NIL, 0, byte(0));
      case 0xc2:  /* false */
        return value(MPACK_TOKEN_BOOLEAN, 1, byte(0));
      case 0xc3:  /* true */
        return value(MPACK_TOKEN_BOOLEAN, 1, byte(1));
      case 0xc4:  /* bin 8 */
      case 0xc5:  /* bin 16 */
      case 0xc6:  /* bin 32 */
        return read_blob(MPACK_TOKEN_BIN, TLEN(t, 0xc4), buf, buflen);
      case 0xc7:  /* ext 8 */
      case 0xc8:  /* ext 16 */
      case 0xc9:  /* ext 32 */
        return read_blob(MPACK_TOKEN_EXT, TLEN(t, 0xc7), buf, buflen);
      case 0xca:  /* float 32 */
      case 0xcb:  /* float 64 */
        return read_value(MPACK_TOKEN_FLOAT, TLEN(t, 0xc8), buf, buflen);
      case 0xcc:  /* uint 8 */
      case 0xcd:  /* uint 16 */
      case 0xce:  /* uint 32 */
      case 0xcf:  /* uint 64 */
        return read_value(MPACK_TOKEN_UINT, TLEN(t, 0xcc), buf, buflen);
      case 0xd0:  /* int 8 */
      case 0xd1:  /* int 16 */
      case 0xd2:  /* int 32 */
      case 0xd3:  /* int 64 */
        return read_value(MPACK_TOKEN_SINT, TLEN(t, 0xd0), buf, buflen);
      case 0xd4:  /* fixext 1 */
      case 0xd5:  /* fixext 2 */
      case 0xd6:  /* fixext 4 */
      case 0xd7:  /* fixext 8 */
      case 0xd8:  /* fixext 16 */
        if (*buflen) {
          mpack_token_t rv;
          rv.type = MPACK_TOKEN_EXT;
          rv.length = TLEN(t, 0xd4);
          rv.data.ext_type = ADVANCE(buf, buflen);
          return rv;
        }
        return yield();
      case 0xd9:  /* str 8 */
      case 0xda:  /* str 16 */
      case 0xdb:  /* str 32 */
        return read_blob(MPACK_TOKEN_STR, TLEN(t, 0xd9), buf, buflen);
      case 0xdc:  /* array 16 */
      case 0xdd:  /* array 32 */
        return read_blob(MPACK_TOKEN_ARRAY, TLEN(t, 0xdb), buf, buflen);
      case 0xde:  /* map 16 */
      case 0xdf:  /* map 32 */
        return read_blob(MPACK_TOKEN_MAP, TLEN(t, 0xdd), buf, buflen);
      default:
        return yield();
    }
  } else {
    /* negative fixint */
    return value(MPACK_TOKEN_SINT, 1, byte(t));
  }
}

void mpack_write(mpack_token_t tok, char **buf, size_t *buflen)
{
  switch (tok.type) {
    case MPACK_TOKEN_NIL:
      write1(buf, buflen, 0xc0);
      break;
    case MPACK_TOKEN_BOOLEAN:
      write1(buf, buflen, tok.data.value.lo ? 0xc3 : 0xc2);
      break;
    case MPACK_TOKEN_UINT:
      write_pint(buf, buflen, tok.data.value);
      break;
    case MPACK_TOKEN_SINT:
      write_nint(buf, buflen, tok.data.value);
      break;
    case MPACK_TOKEN_FLOAT:
      write_float(buf, buflen, tok);
      break;
    case MPACK_TOKEN_BIN:
      write_bin(buf, buflen, tok.length);
      break;
    case MPACK_TOKEN_STR:
      write_str(buf, buflen, tok.length);
      break;
    case MPACK_TOKEN_EXT:
      write_ext(buf, buflen, tok.data.ext_type, tok.length);
      break;
    case MPACK_TOKEN_ARRAY:
      write_array(buf, buflen, tok.length);
      break;
    case MPACK_TOKEN_MAP:
      write_map(buf, buflen, tok.length);
      break;
    default:
      assert(0);
      break;
  }
}

static void write_pint(char **buf, size_t *buflen, mpack_value_t val)
{
  mpack_uint32_t hi = val.hi;
  mpack_uint32_t lo = val.lo;
  if (hi) {
    /* uint 64 */
    write1(buf, buflen, 0xcf);
    write4(buf, buflen, hi);
    write4(buf, buflen, lo);
  } else if (lo > 0xffff) {
    /* uint 32 */
    write1(buf, buflen, 0xce);
    write4(buf, buflen, lo);
  } else if (lo > 0xff) {
    /* uint 16 */
    write1(buf, buflen, 0xcd);
    write2(buf, buflen, lo);
  } else if (lo > 0x7f) {
    /* uint 8 */
    write1(buf, buflen, 0xcc);
    write1(buf, buflen, lo);
  } else {
    write1(buf, buflen, lo);
  }
}

static void write_nint(char **buf, size_t *buflen, mpack_value_t val)
{
  mpack_uint32_t hi = val.hi;
  mpack_uint32_t lo = val.lo;

  if (lo < 0x80000000) {
    /* int 64 */
    write1(buf, buflen, 0xd3);
    write4(buf, buflen, hi);
    write4(buf, buflen, lo);
  } else if (lo < 0xffff7fff) {
    /* int 32 */
    write1(buf, buflen, 0xd2);
    write4(buf, buflen, lo);
  } else if (lo < 0xffffff7f) {
    /* int 16 */
    write1(buf, buflen, 0xd1);
    write2(buf, buflen, lo);
  } else if (lo < 0xffffffe0) {
    /* int 8 */
    write1(buf, buflen, 0xd0);
    write1(buf, buflen, lo);
  } else {
    /* negative fixint */
    write1(buf, buflen, (mpack_uint32_t)(0x100 + lo));
  }
}

static void write_float(char **buf, size_t *buflen, mpack_token_t tok)
{
  if (tok.length == 4) {
    write1(buf, buflen, 0xca);
    write4(buf, buflen, tok.data.value.lo);
  } else if (tok.length == 8) {
    write1(buf, buflen, 0xcb);
    write4(buf, buflen, tok.data.value.hi);
    write4(buf, buflen, tok.data.value.lo);
  }
}

static void write_str(char **buf, size_t *buflen, mpack_uint32_t len)
{
  if (len < 0x20) {
    write1(buf, buflen, 0xa0 | len);
  } else if (len < 0x100) {
    write1(buf, buflen, 0xd9);
    write1(buf, buflen, len);
  } else if (len < 0x10000) {
    write1(buf, buflen, 0xda);
    write2(buf, buflen, len);
  } else {
    write1(buf, buflen, 0xdb);
    write4(buf, buflen, len);
  }
}

static void write_bin(char **buf, size_t *buflen, mpack_uint32_t len)
{
  if (len < 0x100) {
    write1(buf, buflen, 0xc4);
    write1(buf, buflen, len);
  } else if (len < 0x10000) {
    write1(buf, buflen, 0xc5);
    write2(buf, buflen, len);
  } else {
    write1(buf, buflen, 0xc6);
    write4(buf, buflen, len);
  }
}

static void write_ext(char **buf, size_t *buflen, int type, mpack_uint32_t len)
{
  mpack_uint32_t t;
  assert(type >= 0 && type < 0x80);
  t = (mpack_uint32_t)type;
  switch (len) {
    case 1: write1(buf, buflen, 0xd4); write1(buf, buflen, t); break;
    case 2: write1(buf, buflen, 0xd5); write1(buf, buflen, t); break;
    case 4: write1(buf, buflen, 0xd6); write1(buf, buflen, t); break;
    case 8: write1(buf, buflen, 0xd7); write1(buf, buflen, t); break;
    case 16: write1(buf, buflen, 0xd8); write1(buf, buflen, t); break;
    default:
      if (len < 0x100) {
        write1(buf, buflen, 0xc7);
        write1(buf, buflen, len);
        write1(buf, buflen, t);
      } else if (len < 0x10000) {
        write1(buf, buflen, 0xc8);
        write2(buf, buflen, len);
        write1(buf, buflen, t);
      } else {
        write1(buf, buflen, 0xc9);
        write4(buf, buflen, len);
        write1(buf, buflen, t);
      }
      break;
  }
}

static void write_array(char **buf, size_t *buflen, mpack_uint32_t len)
{
  if (len < 0x10) {
    write1(buf, buflen, 0x90 | len);
  } else if (len < 0x10000) {
    write1(buf, buflen, 0xdc);
    write2(buf, buflen, len);
  } else {
    write1(buf, buflen, 0xdd);
    write4(buf, buflen, len);
  }
}

static void write_map(char **buf, size_t *buflen, mpack_uint32_t len)
{
  if (len < 0x10) {
    write1(buf, buflen, 0x80 | len);
  } else if (len < 0x10000) {
    write1(buf, buflen, 0xde);
    write2(buf, buflen, len);
  } else {
    write1(buf, buflen, 0xdf);
    write4(buf, buflen, len);
  }
}

static mpack_token_t yield(void)
{
  mpack_token_t rv;
  rv.type = MPACK_TOKEN_NONE;
  return rv;
}

static mpack_token_t value(mpack_token_type_t type, mpack_uint32_t length,
    mpack_value_t value)
{
  mpack_token_t rv;
  rv.type = type;
  rv.length = length;
  rv.data.value = value;
  return rv;
}

static mpack_token_t blob(mpack_token_type_t type, mpack_uint32_t length,
    int ext_type)
{
  mpack_token_t rv;
  rv.type = type;
  rv.length = length;
  rv.data.ext_type = ext_type;
  return rv;
}

static mpack_value_t byte(unsigned char byte)
{
  mpack_value_t rv;
  rv.lo = byte;
  rv.hi = 0;
  return rv;
}

static mpack_token_t read_value(mpack_token_type_t type,
    mpack_uint32_t remaining, const char **buf, size_t *buflen)
{
  mpack_token_t token;

  if (*buflen < remaining) {
    return yield();
  }

  token = value(type, remaining, byte(0));

  while (remaining) {
    mpack_uint32_t byte = ADVANCE(buf, buflen), byte_idx, byte_shift;
    byte_idx = (mpack_uint32_t)--remaining;
    byte_shift = (byte_idx % 4) * 8;
    token.data.value.lo |= byte << byte_shift;
    if (remaining == 4) {
      /* unpacked the first half of a 8-byte value, shift what was parsed to the
       * "hi" field and reset "lo" for the trailing 4 bytes. */
      token.data.value.hi = token.data.value.lo;
      token.data.value.lo = 0;
    }
  }

  return token;
}

static mpack_token_t read_blob(mpack_token_type_t type, mpack_uint32_t tlen,
    const char **buf, size_t *buflen)
{
  mpack_token_t rv;
  size_t required = tlen + (type == MPACK_TOKEN_EXT ? 1 : 0);

  if (*buflen < required) {
    return yield();
  }

  rv.type = type;
  rv.length = read_value(MPACK_TOKEN_UINT, tlen, buf, buflen).data.value.lo;
  if (type == MPACK_TOKEN_EXT) {
    rv.data.ext_type = ADVANCE(buf, buflen);
  } else if (type == MPACK_TOKEN_MAP) {
    rv.length *= 2;
  }
  return rv;
}

static void write1(char **b, size_t *bl, mpack_uint32_t v)
{
  assert(*bl); (*bl)--;
  *(*b)++ = (char)(v & 0xff);
}

static void write2(char **b, size_t *bl, mpack_uint32_t v)
{
  assert(*bl > 1); *bl -= 2;
  *(*b)++ = (char)((v >> 8) & 0xff);
  *(*b)++ = (char)(v & 0xff);
}

static void write4(char **b, size_t *bl, mpack_uint32_t v)
{
  assert(*bl > 3); *bl -= 4;
  *(*b)++ = (char)((v >> 24) & 0xff);
  *(*b)++ = (char)((v >> 16) & 0xff);
  *(*b)++ = (char)((v >> 8) & 0xff);
  *(*b)++ = (char)(v & 0xff);
}
