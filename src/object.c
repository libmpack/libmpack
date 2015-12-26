#include "object.h"

void mpack_parser_init(mpack_parser_t *parser, mpack_parse_cb cb)
{
  mpack_parser_init_capacity(parser, cb, MPACK_MAX_OBJECT_DEPTH);
}

void mpack_parser_init_capacity(mpack_parser_t *parser, mpack_parse_cb cb,
    size_t capacity)
{
  mpack_reader_init(&parser->reader);
  parser->cb = cb;
  parser->capacity = capacity;
  parser->size = 0;
}

int mpack_parse(mpack_parser_t *parser, const char **buf, size_t *buflen)
{
  int status;

  while (*buflen) {
    mpack_node_t *top, *parent;

    if (parser->size == parser->capacity) {
      /* stack full */
      return MPACK_NOMEM;
    }

    top = parser->stack + parser->size;

    if ((status = mpack_read(&parser->reader, buf, buflen, &top->tok))) {
      return status;
    }

    /* push and invoke callback, passing parent node if any */
    top->data = NULL;
    top->pos = 0;
    parent = parser->stack < top ? top - 1 : NULL;
    parser->size++;
    parser->cb(parser, parent, top);

    do {
      if (top->tok.type > MPACK_TOKEN_CHUNK) {
        if (top->pos < top->tok.length) {
          /* continue parsing children */
          break;
        } else {
          /* empty container, invoke the callback again to notify that it is
           * finished */
          parser->cb(parser, parent, top);
        }
      }

      if (parent) {
        /* we use parent->tok.length to keep track of how many children remain.
         * update it to reflect the processed node. */
        parent->pos += top->tok.type == MPACK_TOKEN_CHUNK ? top->tok.length : 1;
      }

      /* pop and update top/parent */
      parser->size--;
      top = parent;
      parent = parser->stack < top ? top - 1 : NULL;
    } while (top && top->pos == top->tok.length);

    if (!parser->size) {
      /* finished */
      return MPACK_OK;
    }
  }

  return MPACK_EOF;
}
