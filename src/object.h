#ifndef MPACK_OBJECT_H
#define MPACK_OBJECT_H

#include "core.h"

#ifndef MPACK_MAX_OBJECT_DEPTH
# define MPACK_MAX_OBJECT_DEPTH 32
#endif

typedef struct mpack_node_s {
  mpack_token_t tok;
  size_t pos;
  void *data;
} mpack_node_t;

typedef struct mpack_stack_s mpack_stack_t;
typedef void(*mpack_stack_cb)(mpack_stack_t *stack, mpack_node_t *parent,
    mpack_node_t *node);

struct mpack_stack_s {
  mpack_stack_cb cb;
  size_t size, capacity;
  mpack_node_t items[MPACK_MAX_OBJECT_DEPTH];
};

void mpack_stack_init(mpack_stack_t *s, mpack_stack_cb cb);
int mpack_stack_full(mpack_stack_t *stack);
void mpack_stack_push(mpack_stack_t *stack, mpack_token_t *tok);
void mpack_stack_pop_processed(mpack_stack_t *stack);

typedef struct mpack_parser_s {
  mpack_stack_t stack;
  mpack_reader_t reader;
} mpack_parser_t;

void mpack_parser_init(mpack_parser_t *p, mpack_stack_cb cb);
int mpack_parse(mpack_parser_t *p, const char **b, size_t *bl);

#endif  /* MPACK_OBJECT_H */
