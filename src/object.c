#include "object.h"

enum {
  WALK_STATE_ENTER = 1,
  WALK_STATE_EXIT,
  WALK_STATE_VISIT
};

static void mpack_walker_init(mpack_walker_t *w);
static int mpack_walker_full(mpack_walker_t *w);
static mpack_node_t *mpack_walker_push(mpack_walker_t *w);
static mpack_node_t *mpack_walker_pop(mpack_walker_t *w);

#define WALK(walker, visit, target, visit_on_enter)                        \
  do {                                                                     \
    if (!(walker)->state) {                                                \
      (walker)->state = WALK_STATE_ENTER;                                  \
      return MPACK_OK;                                                     \
    }                                                                      \
                                                                           \
    for (;;) {                                                             \
      if ((walker)->state == WALK_STATE_ENTER) {                           \
                                                                           \
        if (mpack_walker_full(walker)) return MPACK_NOMEM;                 \
        *node = mpack_walker_push(walker);                                 \
        if (visit_on_enter) {                                              \
          int fail = visit(target, buf, buflen, &(*node)->tok);            \
          if (fail) { (walker)->size--; return fail; }                     \
          (walker)->state = WALK_STATE_EXIT;                               \
        } else {                                                           \
          (walker)->state = WALK_STATE_VISIT;                              \
        }                                                                  \
        return MPACK_NODE_ENTER;                                           \
      } else if (!visit_on_enter && (walker)->state == WALK_STATE_VISIT) { \
        int fail = visit(target, buf, buflen, &(*node)->tok);              \
        if (fail) return fail;                                             \
        (walker)->state = WALK_STATE_EXIT;                                 \
      } else {                                                             \
        assert((walker)->state == WALK_STATE_EXIT);                        \
        *node = mpack_walker_pop(walker);                                  \
                                                                           \
        if (*node) {                                                       \
          if (!(walker)->size) (walker)->state = 0;                        \
          return MPACK_NODE_EXIT;                                          \
        } else {                                                           \
          (walker)->state = WALK_STATE_ENTER;                              \
        }                                                                  \
      }                                                                    \
    }                                                                      \
  } while (0)

void mpack_parser_init(mpack_parser_t *parser)
{
  mpack_walker_init(&parser->walker);
  mpack_reader_init(&parser->reader);
}

int mpack_parse(mpack_parser_t *parser, const char **buf, size_t *buflen,
    mpack_node_t **node)
{
  WALK(&parser->walker, mpack_read, &parser->reader, 1);
}

void mpack_unparser_init(mpack_unparser_t *unparser)
{
  mpack_walker_init(&unparser->walker);
  mpack_writer_init(&unparser->writer);
}

int mpack_unparse(mpack_unparser_t *unparser, char **buf, size_t *buflen,
    mpack_node_t **node)
{
  WALK(&unparser->walker, mpack_write, &unparser->writer, 0);
}

static void mpack_walker_init(mpack_walker_t *walker)
{
  walker->state = WALK_STATE_ENTER;
  walker->capacity = MPACK_MAX_OBJECT_DEPTH;
  walker->size = 0;
  walker->items[0].pos = (size_t)-1;
}

static int mpack_walker_full(mpack_walker_t *walker)
{
  return walker->size == walker->capacity;
}

static mpack_node_t *mpack_walker_push(mpack_walker_t *walker)
{
  mpack_node_t *top;
  assert(walker->size < walker->capacity);
  top = walker->items + walker->size + 1;
  top->data = NULL;
  top->pos = 0;
  /* increase size and invoke callback, passing parent node if any */
  walker->size++;
  return top;
}

static mpack_node_t *mpack_walker_pop(mpack_walker_t *walker)
{
  mpack_node_t *top, *parent;
  assert(walker->size);
  top = walker->items + walker->size;

  if (top->tok.type > MPACK_TOKEN_CHUNK && top->pos < top->tok.length) {
    /* continue processing children */
    return NULL;
  }

  parent = MPACK_PARENT_NODE(top);
  if (parent) {
    /* we use parent->tok.length to keep track of how many children remain.
     * update it to reflect the processed node. */
    parent->pos += top->tok.type == MPACK_TOKEN_CHUNK ? top->tok.length : 1;
  }

  walker->size--;
  return top;
}

