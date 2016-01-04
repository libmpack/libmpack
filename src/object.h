#ifndef MPACK_OBJECT_H
#define MPACK_OBJECT_H

#include "core.h"

#ifndef MPACK_MAX_OBJECT_DEPTH
# define MPACK_MAX_OBJECT_DEPTH 32
#endif

#define MPACK_PARENT_NODE(n) (((n) - 1)->pos == (size_t)-1 ? NULL : (n) - 1)

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
  mpack_node_t items[MPACK_MAX_OBJECT_DEPTH + 1];
} mpack_walker_t;

typedef struct mpack_parser_s {
  mpack_reader_t reader;
  mpack_walker_t walker;
} mpack_parser_t;

void mpack_parser_init(mpack_parser_t *p);
int mpack_parse(mpack_parser_t *p, const char **b, size_t *bl,
    mpack_node_t **node);

typedef struct mpack_unparser_s {
  mpack_writer_t writer;
  mpack_walker_t walker;
} mpack_unparser_t;

void mpack_unparser_init(mpack_unparser_t *u);
int mpack_unparse(mpack_unparser_t *u, char **b, size_t *bl,
    mpack_node_t **node);
#endif  /* MPACK_OBJECT_H */
