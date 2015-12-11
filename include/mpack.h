#ifndef MPACK_H
#define MPACK_H

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

#ifndef MPACK_DEFAULT_STACK_SIZE
# define MPACK_DEFAULT_STACK_SIZE 32
#endif  /* MPACK_STACK_MAX_SIZE */

typedef enum {
  MPACK_TOKEN_NIL,
  MPACK_TOKEN_BOOLEAN,
  MPACK_TOKEN_UINT,
  MPACK_TOKEN_SINT,
  MPACK_TOKEN_FLOAT,
  MPACK_TOKEN_CHUNK,
  MPACK_TOKEN_BIN,
  MPACK_TOKEN_STR,
  MPACK_TOKEN_EXT,
  MPACK_TOKEN_ARRAY,
  MPACK_TOKEN_MAP
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

typedef struct mpack_unpacker_s mpack_unpacker_t;

void mpack_unpacker_init(mpack_unpacker_t *unpacker);
mpack_token_t *mpack_unpack(mpack_unpacker_t *unpacker, const char **buf,
    size_t *buflen);

typedef struct mpack_unpack_state_s {
  int code;
  mpack_token_t token;
  /* number of bytes remaining when unpacking values or byte arrays.
   * number of items remaining when unpacking arrays or maps. */
  mpack_uint32_t remaining;
} mpack_unpack_state_t;

struct mpack_unpacker_s {
  void *data;
  mpack_token_t *result;
  size_t stackpos;
  int error_code;
  mpack_unpack_state_t stack[MPACK_DEFAULT_STACK_SIZE];
};

void mpack_write(mpack_token_t t, char **b, size_t *bl);


#if __STDC_VERSION__ < 199901L
# ifdef __GNUC__
#  define FUNUSED __attribute__((unused))
# else
#  define FUNUSED
# endif
/* pack */
static mpack_token_t mpack_pack_nil(void) FUNUSED;
static mpack_token_t mpack_pack_boolean(unsigned v) FUNUSED;
static mpack_token_t mpack_pack_uint(mpack_uintmax_t v) FUNUSED;
static mpack_token_t mpack_pack_sint(mpack_sintmax_t v) FUNUSED;
static mpack_token_t mpack_pack_float(double v) FUNUSED;
static mpack_token_t mpack_pack_str(mpack_uint32_t l) FUNUSED;
static mpack_token_t mpack_pack_bin(mpack_uint32_t l) FUNUSED;
static mpack_token_t mpack_pack_ext(int type, mpack_uint32_t l) FUNUSED;
static mpack_token_t mpack_pack_array(mpack_uint32_t l) FUNUSED;
static mpack_token_t mpack_pack_map(mpack_uint32_t l) FUNUSED;
/* unpack */
static int mpack_unpack_boolean(mpack_token_t *t) FUNUSED;
static mpack_uintmax_t mpack_unpack_uint(mpack_token_t *t) FUNUSED;
static mpack_sintmax_t mpack_unpack_sint(mpack_token_t *t) FUNUSED;
static double mpack_unpack_float(mpack_token_t *t) FUNUSED;
#undef FUNUSED
#define inline
#endif

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

static inline mpack_token_t mpack_pack_float(double v)
{
  mpack_token_t rv;
  rv.type = MPACK_TOKEN_FLOAT;
  if (((double)(float)v) == (double)v) {
    rv.length = 4;
    rv.data.value = pack_ieee754(v, 23, 8);
  } else {
    rv.length = 8;
    rv.data.value = pack_ieee754(v, 52, 11);
  }
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

static inline int mpack_unpack_boolean(mpack_token_t *t)
{
  return t->data.value.lo || t->data.value.hi;
}

static inline mpack_uintmax_t mpack_unpack_uint(mpack_token_t *t)
{
  return (((mpack_uintmax_t)t->data.value.hi << 31) << 1) | t->data.value.lo;
}

static inline mpack_sintmax_t mpack_unpack_sint(mpack_token_t *t)
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

static inline double mpack_unpack_float(mpack_token_t *t)
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

#endif  /* MPACK_H */
