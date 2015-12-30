#include "object.h"

void mpack_stack_init(mpack_stack_t *stack, mpack_stack_cb cb)
{
  stack->cb = cb;
  stack->capacity = MPACK_MAX_OBJECT_DEPTH;
  stack->size = 0;
}

int mpack_stack_full(mpack_stack_t *stack)
{
  return stack->size == stack->capacity;
}

void mpack_stack_push(mpack_stack_t *stack, mpack_token_t *tok)
{
  mpack_node_t *top;
  assert(stack->size < stack->capacity);
  top = stack->items + stack->size;
  top->tok = *tok;
  top->data = NULL;
  top->pos = 0;
  /* increase size and invoke callback, passing parent node if any */
  stack->size++;
  stack->cb(stack, stack->items < top ? top - 1 : NULL, top);
}

void mpack_stack_pop_processed(mpack_stack_t *stack)
{
  mpack_node_t *top, *parent;
  assert(stack->size);
  top = stack->items + stack->size - 1;
  parent = stack->items < top ? top - 1 : NULL;

  do {
    if (top->tok.type > MPACK_TOKEN_CHUNK) {
      if (top->pos < top->tok.length) {
        /* continue processing children */
        break;
      } else {
        /* finished processing container, invoke the callback again to notify
         * the stack client */
        stack->cb(stack, parent, top);
      }
    }

    if (parent) {
      /* we use parent->tok.length to keep track of how many children remain.
       * update it to reflect the processed node. */
      parent->pos += top->tok.type == MPACK_TOKEN_CHUNK ? top->tok.length : 1;
    }

    /* pop and update top/parent */
    stack->size--;
    top = parent;
    parent = stack->items < top ? top - 1 : NULL;
  } while (top && top->pos == top->tok.length);
}

void mpack_parser_init(mpack_parser_t *parser, mpack_stack_cb cb)
{
  mpack_stack_init(&parser->stack, cb);
  mpack_reader_init(&parser->reader);
}

int mpack_parse(mpack_parser_t *parser, const char **buf, size_t *buflen)
{
  while (*buflen) {
    int fail;
    mpack_token_t tok;

    if (mpack_stack_full(&parser->stack)) return MPACK_NOMEM;
    if ((fail = mpack_read(&parser->reader, buf, buflen, &tok))) return fail;

    mpack_stack_push(&parser->stack, &tok);
    mpack_stack_pop_processed(&parser->stack);

    if (!parser->stack.size) {
      /* finished */
      return MPACK_OK;
    }
  }

  return MPACK_EOF;
}
