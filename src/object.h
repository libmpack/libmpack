#ifndef MPACK_OBJECT_H
#define MPACK_OBJECT_H

#include "core.h"

#ifndef MPACK_MAX_OBJECT_DEPTH
# define MPACK_MAX_OBJECT_DEPTH 32
#endif

#define MPACK_PARENT_NODE(n) (((n) - 1)->pos == (size_t)-1 ? NULL : (n) - 1)

enum {
  MPACK_WALKED = MPACK_ERROR + 1,
  MPACK_NOMEM
};

typedef struct mpack_node_s {
  mpack_token_t tok;
  size_t pos;
  void *data;
} mpack_node_t;

typedef struct mpack_walker_s {
  size_t size, capacity;
  int state;
  mpack_node_t items[MPACK_MAX_OBJECT_DEPTH + 1];
} mpack_walker_t;

typedef void(*mpack_walk_cb)(mpack_walker_t *w, mpack_node_t *n, void *d);

MPACK_API void mpack_walker_init(mpack_walker_t *p) FUNUSED FNONULL;
MPACK_API int mpack_walk(mpack_walker_t *w, mpack_walk_cb enter_cb,
    mpack_walk_cb exit_cb, void *data) FUNUSED FNONULL_ARG((1,2,3));
MPACK_API int mpack_parse(mpack_walker_t *w, mpack_tokbuf_t *tb,
    mpack_walk_cb enter_cb, mpack_walk_cb exit_cb, const char **b, size_t *bl,
    void *data) FUNUSED FNONULL_ARG((1,2,3,4,5,6));
MPACK_API int mpack_unparse(mpack_walker_t *w, mpack_tokbuf_t *tb,
    mpack_walk_cb enter_cb, mpack_walk_cb exit_cb, char **b, size_t *bl,
    void *data) FUNUSED FNONULL_ARG((1,2,3,4,5,6));


#endif  /* MPACK_OBJECT_H */
