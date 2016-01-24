#include "object.h"

enum {
  WALK_STATE_PUSH = 1,
  WALK_STATE_VISIT,
  WALK_STATE_POP
};

static int mpack_walker_full(mpack_walker_t *w);
static mpack_node_t *mpack_walker_push(mpack_walker_t *w);
static mpack_node_t *mpack_walker_pop(mpack_walker_t *w);

MPACK_API void mpack_walker_init(mpack_walker_t *walker)
{
  walker->capacity = MPACK_MAX_OBJECT_DEPTH;
  walker->size = 0;
  walker->state = WALK_STATE_PUSH;
  walker->items[0].pos = (size_t)-1;
}

MPACK_API int mpack_walk(mpack_walker_t *walker, mpack_walk_cb enter_cb,
    mpack_walk_cb exit_cb, void *data)
{
  mpack_node_t *n;

  if (walker->state == WALK_STATE_PUSH) {
    if (mpack_walker_full(walker)) return MPACK_NOMEM;
    n = mpack_walker_push(walker);
    walker->state = WALK_STATE_VISIT;
  } else if (walker->state == WALK_STATE_VISIT) {
    n = walker->items + walker->size;
    enter_cb(walker, n, data);
    walker->state = WALK_STATE_POP;
  } else {
    assert(walker->state == WALK_STATE_POP);
    while ((n = mpack_walker_pop(walker))) {
      exit_cb(walker, n, data);
      if (!walker->size) return MPACK_OK;
    }
    walker->state = WALK_STATE_PUSH;
  }

  return MPACK_WALKED;
}

#define MPACK_WALK_LOOP(visit_state, visit)                                   \
  do {                                                                        \
    int status;                                                               \
    while (*buflen) {                                                         \
      if (walker->state == visit_state) {                                     \
        mpack_node_t *top = walker->items + walker->size;                     \
        if ((status = visit(tokbuf, buf, buflen, &top->tok))) return status;  \
      }                                                                       \
      status = mpack_walk(walker, enter_cb, exit_cb, d);                      \
      if (status != MPACK_WALKED) return status;                              \
    }                                                                         \
    return MPACK_EOF;                                                         \
  } while (0)

MPACK_API int mpack_parse(mpack_walker_t *walker, mpack_tokbuf_t *tokbuf,
    mpack_walk_cb enter_cb, mpack_walk_cb exit_cb, const char **buf,
    size_t *buflen, void *d)
{
  MPACK_WALK_LOOP(WALK_STATE_VISIT, mpack_read);
}

MPACK_API int mpack_unparse(mpack_walker_t *walker, mpack_tokbuf_t *tokbuf,
    mpack_walk_cb enter_cb, mpack_walk_cb exit_cb, char **buf, size_t *buflen,
    void *d)
{
  MPACK_WALK_LOOP(WALK_STATE_POP, mpack_write);
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

