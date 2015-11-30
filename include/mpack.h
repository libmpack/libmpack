#ifndef MPACK_H
#define MPACK_H

#include <limits.h>

#if UINT_MAX == 0xffffffff
typedef int mpack_int32_t;
typedef unsigned int mpack_uint32_t;
#elif ULONG_MAX == 0xffffffff
typedef long mpack_int32_t;
typedef unsigned long mpack_uint32_t;
#else
# error "can't find unsigned 32-bit integer type"
#endif

typedef struct mpack_value_s {
  mpack_uint32_t lo, hi;
} mpack_value_t;

#include <stddef.h>

#ifndef MPACK_DEFAULT_STACK_SIZE
# define MPACK_DEFAULT_STACK_SIZE 32
#endif  /* MPACK_STACK_MAX_SIZE */

#define MPACK_FLOAT(t) ((float)MPACK_DOUBLE(t))
#define MPACK_DOUBLE(t) (t->data.f64)
#define MPACK_INT32(t) ((mpack_int32_t)MPACK_UINT32(t))
#define MPACK_UINT32(t) (t->data.value.lo)
#define MPACK_INT64(t) ((int64_t)MPACK_UINT64(t))
#define MPACK_UINT64(t) (                 \
  (((uint64_t)t->data.value.hi) << 32) |  \
  ((uint64_t)t->data.value.lo))

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
  size_t length;            /* Byte length for str/bin/ext/chunk/float/int/uint.
                               Item count for array/map. */
  union {
    mpack_value_t value;    /* 32-bit parts of primitives (bool,int,float) */
    double f64;
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
  size_t remaining;
} mpack_unpack_state_t;

struct mpack_unpacker_s {
  void *data;
  mpack_token_t *result;
  size_t stackpos;
  int error_code, le;
  mpack_unpack_state_t stack[MPACK_DEFAULT_STACK_SIZE];
};

#endif  /* MPACK_H */
