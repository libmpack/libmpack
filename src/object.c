#include "object.h"

enum {
  WALK_STATE_ENTER = 1,
  WALK_STATE_EXIT,
  WALK_STATE_VISIT
};

static int mpack_walker_full(mpack_walker_t *w);
static mpack_node_t *mpack_walker_push(mpack_walker_t *w);
static mpack_node_t *mpack_walker_pop(mpack_walker_t *w);

#define WALK(walker, visit, visit_on_enter, enter_cb, exit_cb)      \
  do {                                                                      \
    int fail;                                                               \
    mpack_node_t *n;                                                        \
                                                                            \
    while (*buflen) {                                                       \
      if ((walker)->state == WALK_STATE_ENTER) {                            \
        if (mpack_walker_full(walker)) return MPACK_NOMEM;                  \
        n = mpack_walker_push(walker);                                      \
        if (visit_on_enter) {                                               \
          fail = visit(&(walker)->tokbuf, buf, buflen, &n->tok);            \
          if (fail) { (walker)->size--; return fail; }                      \
          (walker)->state = WALK_STATE_EXIT;                                \
        } else {                                                            \
          (walker)->state = WALK_STATE_VISIT;                               \
        }                                                                   \
        enter_cb(walker, n);                                                \
      } else if (!visit_on_enter && (walker)->state == WALK_STATE_VISIT) {  \
        n = (walker)->items + (walker)->size;                               \
        fail = visit(&(walker)->tokbuf, buf, buflen, &n->tok);              \
        if (fail) return fail;                                              \
        (walker)->state = WALK_STATE_EXIT;                                  \
      } else {                                                              \
        assert((walker)->state == WALK_STATE_EXIT);                         \
        n = mpack_walker_pop(walker);                                       \
                                                                            \
        if (n) {                                                            \
          exit_cb(walker, n);                                               \
          if (!(walker)->size) {                                            \
            (walker)->state = WALK_STATE_ENTER;                             \
            return MPACK_OK;                                                \
          }                                                                 \
        } else {                                                            \
          (walker)->state = WALK_STATE_ENTER;                               \
        }                                                                   \
      }                                                                     \
    }                                                                       \
  } while (0)

MPACK_API void mpack_walker_init(mpack_walker_t *walker)
{
  mpack_tokbuf_init(&walker->tokbuf);
  walker->state = WALK_STATE_ENTER;
  walker->capacity = MPACK_MAX_OBJECT_DEPTH;
  walker->size = 0;
  walker->items[0].pos = (size_t)-1;
}

MPACK_API int mpack_parse(mpack_walker_t *walker, const char **buf,
    size_t *buflen, mpack_walk_cb enter_cb, mpack_walk_cb exit_cb)
{
  WALK(walker, mpack_read, 1, enter_cb, exit_cb);
  return MPACK_EOF;
}

MPACK_API int mpack_unparse(mpack_walker_t *walker, char **buf,
    size_t *buflen, mpack_walk_cb enter_cb, mpack_walk_cb exit_cb)
{
  WALK(walker, mpack_write, 0, enter_cb, exit_cb);
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

