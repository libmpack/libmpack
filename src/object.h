#ifndef MPACK_OBJECT_H
#define MPACK_OBJECT_H

#include "core.h"

#ifndef MPACK_MAX_OBJECT_DEPTH
# define MPACK_MAX_OBJECT_DEPTH 32
#endif

enum {
  MPACK_NOMEM = MPACK_ERROR + 1,
  MPACK_NODE_ENTER,
  MPACK_NODE_EXIT
};

typedef struct mpack_node_s {
  mpack_token_t tok;
  size_t pos;
  void *data;
} mpack_node_t;

typedef struct mpack_walker_s {
  int state;
  size_t size, capacity;
  mpack_node_t items[MPACK_MAX_OBJECT_DEPTH];
} mpack_walker_t;

typedef struct mpack_parser_s {
  mpack_reader_t reader;
  mpack_walker_t walker;
} mpack_parser_t;

void mpack_parser_init(mpack_parser_t *p);
int mpack_parse(mpack_parser_t *p, const char **b, size_t *bl,
    mpack_node_t **node, mpack_node_t **parent);

#endif  /* MPACK_OBJECT_H */
