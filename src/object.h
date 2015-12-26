#ifndef MPACK_OBJECT_H
#define MPACK_OBJECT_H

#include "core.h"

#ifndef MPACK_MAX_OBJECT_DEPTH
# define MPACK_MAX_OBJECT_DEPTH 32
#endif

typedef struct mpack_parser_s mpack_parser_t;
typedef struct mpack_node_s mpack_node_t;

typedef void(*mpack_parse_cb)(mpack_parser_t *parser, mpack_node_t *parent,
    mpack_node_t *node);

struct mpack_node_s {
  mpack_token_t tok;
  size_t pos;
  void *data;
};

struct mpack_parser_s {
  mpack_reader_t reader;
  mpack_parse_cb cb;
  size_t size, capacity;
  mpack_node_t stack[MPACK_MAX_OBJECT_DEPTH];
};

void mpack_parser_init(mpack_parser_t *p, mpack_parse_cb cb);
void mpack_parser_init_capacity(mpack_parser_t *p, mpack_parse_cb cb,
    size_t md);
int mpack_parse(mpack_parser_t *p, const char **b, size_t *bl);


#endif  /* MPACK_OBJECT_H */
