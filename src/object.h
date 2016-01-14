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

typedef struct mpack_walker_s {
  void *data;
  int state;
  size_t size, capacity;
  mpack_node_t items[MPACK_MAX_OBJECT_DEPTH + 1];
} mpack_walker_t;

typedef void(*mpack_walk_cb)(mpack_walker_t *w, mpack_node_t *n);

typedef struct mpack_parser_s {
  mpack_reader_t reader;
  mpack_walker_t walker;
} mpack_parser_t;

MPACK_API void mpack_parser_init(mpack_parser_t *p);
MPACK_API int mpack_parse(mpack_parser_t *p, const char **b, size_t *bl,
    mpack_walk_cb enter_cb, mpack_walk_cb exit_cb);

typedef struct mpack_unparser_s {
  mpack_writer_t writer;
  mpack_walker_t walker;
} mpack_unparser_t;

MPACK_API void mpack_unparser_init(mpack_unparser_t *u);
MPACK_API int mpack_unparse(mpack_unparser_t *u, char **b, size_t *bl,
    mpack_walk_cb enter_cb, mpack_walk_cb exit_cb);


#endif  /* MPACK_OBJECT_H */
