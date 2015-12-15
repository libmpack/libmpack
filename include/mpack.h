#ifndef MPACK_H
#define MPACK_H

#ifdef __GNUC__
# define FPURE __attribute__((pure))
# define FUNUSED __attribute__((unused))
# define FNONULL(n) __attribute__((nonnull(n)))
#else
# define FPURE
# define FUNUSED
# define FNONULL(n)
#endif
#include <limits.h>

#if UINT_MAX == 0xffffffff
typedef int mpack_sint32_t;
typedef unsigned int mpack_uint32_t;
#elif ULONG_MAX == 0xffffffff
typedef long mpack_sint32_t;
typedef unsigned long mpack_uint32_t;
#else
# error "can't find unsigned 32-bit integer type"
#endif

#define MPACK_BIG_ENDIAN (*(mpack_uint32_t *)"\xff\0\0\0" == 0xff000000)

#define MPACK_SWAP_VALUE(val)                                  \
  do {                                                         \
    mpack_uint32_t lo = val.lo;                                \
    val.lo = val.hi;                                           \
    val.hi = lo;                                               \
  } while (0)

#if ULLONG_MAX == 0xffffffffffffffff
typedef long long mpack_sintmax_t;
typedef unsigned long long mpack_uintmax_t;
#elif UINT64_MAX == 0xffffffffffffffff
typedef int64_t mpack_sintmax_t;
typedef uint64_t mpack_uintmax_t;
#else
typedef mpack_sint32_t mpack_sintmax_t;
typedef mpack_uint32_t mpack_uintmax_t;
#endif

typedef struct mpack_value_s {
  mpack_uint32_t lo, hi;
} mpack_value_t;


#include <assert.h>
#include <stddef.h>

#define MPACK_MAX_TOKEN_SIZE 12

typedef enum {
  MPACK_TOKEN_NIL       = 1,
  MPACK_TOKEN_BOOLEAN   = 2,
  MPACK_TOKEN_UINT      = 3,
  MPACK_TOKEN_SINT      = 4,
  MPACK_TOKEN_FLOAT     = 5,
  MPACK_TOKEN_CHUNK     = 6,
  MPACK_TOKEN_ARRAY     = 7,
  MPACK_TOKEN_MAP       = 8,
  MPACK_TOKEN_BIN       = 9,
  MPACK_TOKEN_STR       = 10,
  MPACK_TOKEN_EXT       = 11
} mpack_token_type_t;


typedef struct mpack_token_s {
  mpack_token_type_t type;  /* Type of token */
  mpack_uint32_t length;    /* Byte length for str/bin/ext/chunk/float/int/uint.
                               Item count for array/map. */
  union {
    mpack_value_t value;    /* 32-bit parts of primitives (bool,int,float) */
    const char *chunk_ptr;  /* Chunk of data from str/bin/ext */
    int ext_type;           /* Type field for ext tokens */
  } data;
} mpack_token_t;

typedef struct mpack_reader_s {
  char pending[MPACK_MAX_TOKEN_SIZE];
  size_t pending_cnt;
  mpack_uint32_t passthrough;
} mpack_reader_t;

typedef struct mpack_writer_s {
  const mpack_token_t *pending;
  size_t pending_written;
} mpack_writer_t;

void mpack_reader_init(mpack_reader_t *r);
void mpack_writer_init(mpack_writer_t *w);
size_t mpack_read(const char **b, size_t *bl, mpack_token_t *tb, size_t tbl,
    mpack_reader_t *r);
size_t mpack_write(char **b, size_t *bl, const mpack_token_t *tb, size_t tbl,
    mpack_writer_t *w);

#ifndef MPACK_C
/* Don't define pack/unpack functions when included form mpack.c */

#ifndef bool
# define bool int
#endif

#if __STDC_VERSION__ < 199901L
# define inline
/* pack */
static mpack_token_t mpack_pack_nil(void) FUNUSED FPURE;
static mpack_token_t mpack_pack_boolean(unsigned v) FUNUSED FPURE;
static mpack_token_t mpack_pack_uint(mpack_uintmax_t v) FUNUSED FPURE;
static mpack_token_t mpack_pack_sint(mpack_sintmax_t v) FUNUSED FPURE;
static mpack_token_t mpack_pack_float_compat(double v) FUNUSED FPURE;
static mpack_token_t mpack_pack_float_fast(double v) FUNUSED FPURE;
static mpack_token_t mpack_pack_chunk(const char *p, mpack_uint32_t l)
  FUNUSED FPURE FNONULL(1);
static mpack_token_t mpack_pack_str(mpack_uint32_t l) FUNUSED FPURE;
static mpack_token_t mpack_pack_bin(mpack_uint32_t l) FUNUSED FPURE;
static mpack_token_t mpack_pack_ext(int type, mpack_uint32_t l) FUNUSED FPURE;
static mpack_token_t mpack_pack_array(mpack_uint32_t l) FUNUSED FPURE;
static mpack_token_t mpack_pack_map(mpack_uint32_t l) FUNUSED FPURE;
/* unpack */
static bool mpack_unpack_boolean(mpack_token_t *t)
  FUNUSED FPURE FNONULL(1);
static mpack_uintmax_t mpack_unpack_uint(mpack_token_t *t)
  FUNUSED FPURE FNONULL(1);
static mpack_sintmax_t mpack_unpack_sint(mpack_token_t *t)
  FUNUSED FPURE FNONULL(1);
static double mpack_unpack_float(mpack_token_t *t)
  FUNUSED FPURE FNONULL(1);
#endif  /* __STDC_VERSION__ < 199901L */

#define POW2(n) \
  ((double)(1 << (n / 2)) * (double)(1 << (n / 2)) * (double)(1 << (n % 2)))

static inline mpack_token_t mpack_pack_nil(void)
{
  mpack_token_t rv;
  rv.type = MPACK_TOKEN_NIL;
  return rv;
}

static inline mpack_token_t mpack_pack_boolean(unsigned v)
{
  mpack_token_t rv;
  rv.type = MPACK_TOKEN_BOOLEAN;
  rv.data.value.lo = v ? 1 : 0;
  rv.data.value.hi = 0;
  return rv;
}

static inline mpack_token_t mpack_pack_uint(mpack_uintmax_t v)
{
  mpack_token_t rv;
  rv.data.value.lo = v & 0xffffffff;
  rv.data.value.hi = (mpack_uint32_t)((v >> 31) >> 1);
  rv.type = MPACK_TOKEN_UINT;
  return rv;
}

static inline mpack_token_t mpack_pack_sint(mpack_sintmax_t v)
{
  if (v < 0) {
    mpack_token_t rv;
    mpack_uintmax_t tc = -((mpack_uintmax_t)(v + 1)) + 1;
    tc = ~tc + 1;
    rv = mpack_pack_uint(tc);
    rv.type = MPACK_TOKEN_SINT;
    return rv;
  }

  return mpack_pack_uint((mpack_uintmax_t)v);
}

static mpack_value_t pack_ieee754(double v, unsigned mantbits, unsigned expbits)
{
  mpack_value_t rv;
  mpack_sint32_t exponent, bias = (1 << (expbits - 1)) - 1;
  mpack_uint32_t sign;
  double mant;

  if (v == 0) {
    rv.lo = 0;
    rv.hi = 0;
    goto end;
  }

  if (v < 0) sign = 1, mant = -v;
  else sign = 0, mant = v;

  exponent = 0;
  while (mant >= 2.0) mant /= 2.0, exponent++;
  while (mant < 1.0 && exponent > -(bias - 1)) mant *= 2.0, exponent--;

  if (mant < 1.0) exponent = -bias; /* subnormal value */
  else mant = mant - 1.0; /* remove leading 1 */
  exponent += bias;
  mant *= POW2(mantbits);

  if (mantbits == 52) {
    rv.hi = (mpack_uint32_t)(mant / POW2(32));
    rv.lo = (mpack_uint32_t)(mant - rv.hi * POW2(32));
    rv.hi |= ((mpack_uint32_t)exponent << 20) | (sign << 31);
  } else if (mantbits == 23) {
    rv.hi = 0;
    rv.lo = (mpack_uint32_t)mant;
    rv.lo |= ((mpack_uint32_t)exponent << 23) | (sign << 31);
  } else {
    assert(0);
  }

end:
  return rv;
}

static inline int fits_single(double v)
{
  const double float_max_abs = 3.4028234663852886e+38;
  const double float_min_abs = 1.4012984643248171e-45;
  double vabs = v < 0 ? -v : v;
  return vabs == 0 || (vabs >= float_min_abs && vabs <= float_max_abs);
}

static inline mpack_token_t mpack_pack_float_compat(double v)
{
  /* ieee754 single-precision limits to determine if "v" can be fully
   * represented in 4 bytes */
  mpack_token_t rv;

  if (fits_single(v)) {
    rv.length = 4;
    rv.data.value = pack_ieee754(v, 23, 8);
  } else {
    rv.length = 8;
    rv.data.value = pack_ieee754(v, 52, 11);
  }

  rv.type = MPACK_TOKEN_FLOAT;
  return rv;
}

static inline mpack_token_t mpack_pack_float_fast(double v)
{
  /* ieee754 single-precision limits to determine if "v" can be fully
   * represented in 4 bytes */
  mpack_token_t rv;

  if (fits_single(v)) {
    union {
      float f;
      mpack_uint32_t m;
    } conv;
    conv.f = (float)v;
    rv.length = 4;
    rv.data.value.lo = conv.m;
    rv.data.value.hi = 0;
  } else {
    union {
      double d;
      mpack_value_t m;
    } conv;
    conv.d = v;
    rv.length = 8;
    rv.data.value = conv.m;
    if (MPACK_BIG_ENDIAN) {
      MPACK_SWAP_VALUE(rv.data.value);
    }
  }

  rv.type = MPACK_TOKEN_FLOAT;
  return rv;
}

static inline mpack_token_t mpack_pack_chunk(const char *p, mpack_uint32_t l)
{
  mpack_token_t rv;
  rv.type = MPACK_TOKEN_CHUNK;
  rv.data.chunk_ptr = p;
  rv.length = l;
  return rv;
}

static inline mpack_token_t mpack_pack_str(mpack_uint32_t l)
{
  mpack_token_t rv;
  rv.type = MPACK_TOKEN_STR;
  rv.length = l;
  return rv;
}

static inline mpack_token_t mpack_pack_bin(mpack_uint32_t l)
{
  mpack_token_t rv;
  rv.type = MPACK_TOKEN_BIN;
  rv.length = l;
  return rv;
}

static inline mpack_token_t mpack_pack_ext(int t, mpack_uint32_t l)
{
  mpack_token_t rv;
  rv.type = MPACK_TOKEN_EXT;
  rv.length = l;
  rv.data.ext_type = t;
  return rv;
}

static inline mpack_token_t mpack_pack_array(mpack_uint32_t l)
{
  mpack_token_t rv;
  rv.type = MPACK_TOKEN_ARRAY;
  rv.length = l;
  return rv;
}

static inline mpack_token_t mpack_pack_map(mpack_uint32_t l)
{
  mpack_token_t rv;
  rv.type = MPACK_TOKEN_MAP;
  rv.length = l;
  return rv;
}

static inline bool mpack_unpack_boolean(const mpack_token_t *t)
{
  return t->data.value.lo || t->data.value.hi;
}

static inline mpack_uintmax_t mpack_unpack_uint(const mpack_token_t *t)
{
  return (((mpack_uintmax_t)t->data.value.hi << 31) << 1) | t->data.value.lo;
}

/* unpack signed integer without relying on two's complement as internal
 * representation */
static inline mpack_sintmax_t mpack_unpack_sint(const mpack_token_t *t)
{
  mpack_uint32_t hi = t->data.value.hi;
  mpack_uint32_t lo = t->data.value.lo;
  mpack_uint32_t negative = (t->length == 8 && hi >> 31) ||
                            (t->length == 4 && lo >> 31) ||
                            (t->length == 2 && lo >> 15) ||
                            (t->length == 1 && lo >> 7);
  assert(t->length <= sizeof(mpack_sintmax_t));

  if (negative) {
    mpack_uintmax_t rv = lo;
    if (t->length == 8) {
      rv |= (((mpack_uintmax_t)hi) << 31) << 1;
    }
    /* reverse the two's complement so that lo/hi contain the absolute value.
     * note that we have to mask ~rv so that it reflects the two's complement
     * of the appropriate byte length */
    rv = (~rv & (((mpack_uintmax_t)1 << ((t->length * 8) - 1)) - 1)) + 1;
    /* negate and return the absolute value, making sure mpack_sintmax_t can
     * represent the positive cast. */
    return -((mpack_sintmax_t)(rv - 1)) - 1;
  }

  return (mpack_sintmax_t)mpack_unpack_uint(t);
}

static inline double mpack_unpack_float_compat(const mpack_token_t *t)
{
  mpack_uint32_t sign;
  mpack_sint32_t exponent, bias;
  unsigned mantbits;
  unsigned expbits;
  double mant;

  if (t->data.value.lo == 0 && t->data.value.hi == 0)
    /* nothing to do */
    return 0;

  if (t->length == 4) mantbits = 23, expbits = 8;
  else mantbits = 52, expbits = 11;
  bias = (1 << (expbits - 1)) - 1;

  /* restore sign/exponent/mantissa */
  if (mantbits == 52) {
    sign = t->data.value.hi >> 31;
    exponent = (t->data.value.hi >> 20) & ((1 << 11) - 1);
    mant = (t->data.value.hi & ((1 << 20) - 1)) * POW2(32);
    mant += t->data.value.lo;
  } else {
    sign = t->data.value.lo >> 31;
    exponent = (t->data.value.lo >> 23) & ((1 << 8) - 1);
    mant = t->data.value.lo & ((1 << 23) - 1);
  }

  mant /= POW2(mantbits);
  if (exponent) mant += 1.0; /* restore leading 1 */
  else exponent = 1; /* subnormal */
  exponent -= bias;

  /* restore original value */
  while (exponent > 0) mant *= 2.0, exponent--;
  while (exponent < 0) mant /= 2.0, exponent++;
  return mant * (sign ? -1 : 1);
}

static inline double mpack_unpack_float_fast(const mpack_token_t *t)
{
  if (t->length == 4) {
    union {
      float f;
      mpack_uint32_t m;
    } conv;
    conv.m = t->data.value.lo;
    return conv.f;
  } else {
    union {
      double d;
      mpack_value_t m;
    } conv;
    conv.m = t->data.value;
    
    if (MPACK_BIG_ENDIAN) {
      MPACK_SWAP_VALUE(conv.m);
    }

    return conv.d;
  }
}

#endif  /* MPACK_C */

#undef FPURE
#undef FUNUSED
#undef FNONULL

/* The mpack_{pack,unpack}_float_fast functions should work in 99% of the
 * platforms. When compiling for a platform where floats don't use ieee754 as
 * the internal format, pass
 * -Dmpack_{pack,unpack}_float=mpack_{pack,unpack}_float_compat to the
 *  compiler.*/
#ifndef mpack_pack_float
# define mpack_pack_float mpack_pack_float_fast
#endif
#ifndef mpack_unpack_float
# define mpack_unpack_float mpack_unpack_float_fast
#endif

#endif  /* MPACK_H */
