#include "object.h"

static int mpack_walker_full(mpack_walker_t *w);
static mpack_node_t *mpack_walker_push(mpack_walker_t *w);
static mpack_node_t *mpack_walker_pop(mpack_walker_t *w);

MPACK_API void mpack_walker_init(mpack_walker_t *walker)
{
  walker->capacity = MPACK_MAX_OBJECT_DEPTH;
  walker->size = 0;
  walker->items[0].pos = (size_t)-1;
}

MPACK_API int mpack_walk(mpack_walker_t *walker, mpack_walk_cb enter_cb,
    mpack_walk_cb exit_cb)
{
  int status;
  mpack_node_t *n;

  if (mpack_walker_full(walker)) return MPACK_NOMEM;
  n = mpack_walker_push(walker);
  if ((status = enter_cb(walker, n))) {
    walker->size--;
    return status;
  }

  while ((n = mpack_walker_pop(walker))) {
    (void)exit_cb(walker, n);
    if (!walker->size) return MPACK_OK;
  }

  return MPACK_EOF;
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

