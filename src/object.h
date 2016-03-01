#ifndef MPACK_OBJECT_H
#define MPACK_OBJECT_H

#include "core.h"

#ifndef MPACK_MAX_OBJECT_DEPTH
# define MPACK_MAX_OBJECT_DEPTH 32
#endif

#define MPACK_PARENT_NODE(n) (((n) - 1)->pos == (size_t)-1 ? NULL : (n) - 1)

enum {
  MPACK_NOMEM = MPACK_ERROR + 1
};

typedef struct mpack_node_s {
  mpack_token_t tok;
  size_t pos;
  void *data;
} mpack_node_t;

#define MPACK_PARSER_STRUCT(c)    \
  struct mpack_parser_##c##_s {   \
    void *data;                   \
    size_t size, capacity;        \
    int status;                   \
    mpack_tokbuf_t tokbuf;        \
    mpack_node_t items[c + 1];    \
  }

typedef MPACK_PARSER_STRUCT(MPACK_MAX_OBJECT_DEPTH) mpack_parser_t;
typedef void(*mpack_walk_cb)(mpack_parser_t *w, mpack_node_t *n);

MPACK_API void mpack_parser_init(mpack_parser_t *p) FUNUSED FNONULL;

MPACK_API int mpack_parse_tok(mpack_parser_t *walker, mpack_token_t tok,
    mpack_walk_cb enter_cb, mpack_walk_cb exit_cb)
  FUNUSED FNONULL_ARG((1,3,4));
MPACK_API int mpack_unparse_tok(mpack_parser_t *walker, mpack_token_t *tok,
    mpack_walk_cb enter_cb, mpack_walk_cb exit_cb)
  FUNUSED FNONULL_ARG((1,2,3,4));

MPACK_API int mpack_parse(mpack_parser_t *parser, const char **b, size_t *bl,
    mpack_walk_cb enter_cb, mpack_walk_cb exit_cb)
  FUNUSED FNONULL_ARG((1,2,3,4,5));
MPACK_API int mpack_unparse(mpack_parser_t *parser, char **b, size_t *bl,
    mpack_walk_cb enter_cb, mpack_walk_cb exit_cb)
  FUNUSED FNONULL_ARG((1,2,3,4,5));

#endif  /* MPACK_OBJECT_H */
