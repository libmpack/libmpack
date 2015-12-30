#include "object.h"

#define PARENT(s, n) ((s)->items < n ? n - 1 : NULL)

static void mpack_walker_init(mpack_walker_t *w);
static int mpack_walker_full(mpack_walker_t *w);
static mpack_node_t *mpack_walker_push(mpack_walker_t *w);
static mpack_node_t *mpack_walker_pop(mpack_walker_t *w);

void mpack_parser_init(mpack_parser_t *parser)
{
  mpack_walker_init(&parser->walker);
  mpack_reader_init(&parser->reader);
}

int mpack_parse(mpack_parser_t *parser, const char **buf, size_t *buflen,
    mpack_node_t **node, mpack_node_t **parent)
{
  if (parser->walker.state == MPACK_OK) {
    parser->walker.state = MPACK_NODE_ENTER;
    return MPACK_OK;
  }

  for (;;) {
    if (parser->walker.state == MPACK_NODE_ENTER) {
      int fail;

      if (mpack_walker_full(&parser->walker)) {
        return MPACK_NOMEM;
      }

      *node = mpack_walker_push(&parser->walker);

      if ((fail = mpack_read(&parser->reader, buf, buflen, &(*node)->tok))) {
        parser->walker.size--;
        return fail;
      }

      *parent = PARENT(&parser->walker, *node);
      parser->walker.state = MPACK_NODE_EXIT;
      return MPACK_NODE_ENTER;
    } else {
      assert(parser->walker.state == MPACK_NODE_EXIT);
      *node = mpack_walker_pop(&parser->walker);

      if (*node) {
        *parent = PARENT(&parser->walker, *node);
        if (*parent == NULL) parser->walker.state = MPACK_OK;
        return MPACK_NODE_EXIT;
      } else {
        parser->walker.state = MPACK_NODE_ENTER;
      }
    }
  }
}

static void mpack_walker_init(mpack_walker_t *walker)
{
  walker->state = MPACK_NODE_ENTER;
  walker->capacity = MPACK_MAX_OBJECT_DEPTH;
  walker->size = 0;
}

static int mpack_walker_full(mpack_walker_t *walker)
{
  return walker->size == walker->capacity;
}

static mpack_node_t *mpack_walker_push(mpack_walker_t *walker)
{
  mpack_node_t *top;
  assert(walker->size < walker->capacity);
  top = walker->items + walker->size;
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
  top = walker->items + walker->size - 1;

  if (top->tok.type > MPACK_TOKEN_CHUNK && top->pos < top->tok.length) {
    /* continue processing children */
    return NULL;
  }

  parent = PARENT(walker, top);
  if (parent) {
    /* we use parent->tok.length to keep track of how many children remain.
     * update it to reflect the processed node. */
    parent->pos += top->tok.type == MPACK_TOKEN_CHUNK ? top->tok.length : 1;
  }

  walker->size--;
  return top;
}

