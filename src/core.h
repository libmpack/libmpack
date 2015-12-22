#ifndef MPACK_CORE_H
#define MPACK_CORE_H

#include <assert.h>
#include <limits.h>
#include <stddef.h>

#ifdef __GNUC__
# define FPURE __attribute__((pure))
# define FNONULL __attribute__((nonnull))
#else
# define FPURE
# define FNONULL
#endif

#if UINT_MAX == 0xffffffff
typedef int mpack_sint32_t;
typedef unsigned int mpack_uint32_t;
#elif ULONG_MAX == 0xffffffff
typedef long mpack_sint32_t;
typedef unsigned long mpack_uint32_t;
#else
# error "can't find unsigned 32-bit integer type"
#endif

typedef struct mpack_value_s {
  mpack_uint32_t lo, hi;
} mpack_value_t;


#define MPACK_ESIZE ((size_t)(-1))
#define MPACK_EREAD ((size_t)(-2))
#define MPACK_ERRORED(s) (s >= MPACK_EREAD)

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

#endif  /* MPACK_CORE_H */
