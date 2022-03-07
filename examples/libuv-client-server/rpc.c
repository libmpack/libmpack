#include <assert.h>
#include <string.h>

#include "rpc.h"
#include "util.h"
#include "uv_util.h"

typedef struct callback_with_arg_s {
  rpc_response_callback callback;
  void *callback_arg;
} callback_with_arg_t;

static void object_destroy_array(cvector_vector_type(object_t) items) {
  object_t *it;
  for (it = cvector_begin(items); it != cvector_end(items); it++) {
    object_destroy(*it);
  }
  cvector_free(items);
}

static void object_destroy_map(cvector_vector_type(kv_t) pairs) {
  kv_t *it;
  for (it = cvector_begin(pairs); it != cvector_end(pairs); it++) {
    object_destroy(it->key);
    object_destroy(it->val);
  }
  cvector_free(pairs);
}

void object_destroy(object_t obj) {
  switch (obj.type) {
    case ARRAY:
      object_destroy_array(obj.data.a);
      break;
    case MAP:
      object_destroy_map(obj.data.m);
      break;
    case STR:
      free(obj.data.s);
      break;
    default:
      /* nothing to do */
      break;
  }
}

static void message_reset(rpc_session_t *session) {
  object_destroy(session->method_or_error);
  object_destroy(session->args_or_result);
  session->state = RECEIVE_HEADER;
  session->msgtype = 0;
}

static void parse_enter(mpack_parser_t *parser, mpack_node_t *node) {
  mpack_node_t *parent;
  rpc_session_t *session;
  object_t *target;

  parent = MPACK_PARENT_NODE(node);

  if (parent) {
    if (parent->tok.type == MPACK_TOKEN_MAP) {
      kv_t *pairs = parent->data[0].p;
      if (parent->key_visited) {
        target = &pairs->val;
      } else {
        target = &pairs->key;
      }
    } else {
      target = (object_t *)parent->data[0].p + parent->pos;
    }
  } else {
    session = container_of(parser, rpc_session_t, unpacker);

    switch (session->state) {
      case RECEIVE_METHOD_OR_ERROR:
        target = &session->method_or_error;
        break;
      case RECEIVE_ARGS_OR_RESULT:
        target = &session->args_or_result;
        break;
      default:
        die("unexpected rpc state");
        break;
    }
  }

  switch (node->tok.type) {
    case MPACK_TOKEN_NIL:
      target->type = NIL;
      break;
    case MPACK_TOKEN_BOOLEAN:
      target->type = BOOLEAN;
      target->data.b = mpack_unpack_boolean(node->tok);
      break;
    case MPACK_TOKEN_UINT:
    case MPACK_TOKEN_SINT:
    case MPACK_TOKEN_FLOAT:
      target->type = NUMBER;
      target->data.n = mpack_unpack_number(node->tok);
      break;
    case MPACK_TOKEN_CHUNK:
      memcpy(
          (char *)parent->data[0].p + parent->pos,
          node->tok.data.chunk_ptr,
          node->tok.length);
      break;
    case MPACK_TOKEN_BIN:
    case MPACK_TOKEN_STR:
    case MPACK_TOKEN_EXT:
      target->type = STR;
      target->data.s = xmalloc(node->tok.length + 1);
      /* add NUL terminator */
      target->data.s[node->tok.length] = 0;
      /* set data pointer to the allocated buffer */
      node->data[0].p = target->data.s;
      break;
    case MPACK_TOKEN_ARRAY:
      target->type = ARRAY;
      cvector_reserve(target->data.a, node->tok.length);
      cvector_set_size(target->data.a, node->tok.length);
      /* set data pointer to the allocated array */
      node->data[0].p = target->data.a;
      break;
    case MPACK_TOKEN_MAP:
      target->type = MAP;
      cvector_reserve(target->data.m, node->tok.length);
      cvector_set_size(target->data.a, node->tok.length);
      /* set data pointer to the allocated array */
      node->data[0].p = target->data.m;
      break;
  }
}

static void parse_exit(mpack_parser_t *parser, mpack_node_t *node) {
  /* nothing to do */
}

static void unparse_enter(mpack_parser_t *parser, mpack_node_t *node) {
  rpc_session_t *session;
  mpack_node_t *parent = MPACK_PARENT_NODE(node);
  object_t current;

  if (parent) {
    if (parent->tok.type ==MPACK_TOKEN_STR) {
      char *str = parent->data[0].p;
      node->tok = mpack_pack_chunk(str, parent->tok.length);
      return;
    } else if (parent->tok.type == MPACK_TOKEN_ARRAY) {
      object_t *items = parent->data[0].p;
      current = items[parent->pos];
    } else if (parent->tok.type == MPACK_TOKEN_MAP) {
      kv_t *pairs = parent->data[0].p;
      kv_t pair = pairs[parent->pos];
      current = parent->key_visited ? pair.val : pair.key;
    }
  } else {
    /* root object */
    session = container_of(parser, rpc_session_t, packer);
    current = session->pack_root;
  }

  switch (current.type) {
    case NIL:
      node->tok = mpack_pack_nil();
      break;
    case BOOLEAN:
      node->tok = mpack_pack_boolean(current.data.b);
      break;
    case NUMBER:
      node->tok = mpack_pack_number(current.data.n);
      break;
    case STR:
      node->data[0].p = current.data.s;
      node->tok = mpack_pack_str((mpack_uint32_t)strlen(current.data.s));
      break;
    case ARRAY:
      node->data[0].p = current.data.a;
      node->tok = mpack_pack_array((mpack_uint32_t)cvector_size(current.data.a));
      break;
    case MAP:
      node->data[0].p = current.data.m;
      node->tok = mpack_pack_map((mpack_uint32_t)cvector_size(current.data.m));
      break;
  }
}

static void unparse_exit(mpack_parser_t *parser, mpack_node_t *node) {
  /* nothing to do */
}

void session_init(
    rpc_session_t *session,
    rpc_request_callback on_request,
    rpc_notification_callback on_notification) {
  memset(session, 0, sizeof *session);
  mpack_rpc_session_init(&session->rpc, 0);
  mpack_parser_init(&session->unpacker, 0);
  mpack_parser_init(&session->packer, 0);
  session->on_request = on_request;
  session->on_notification = on_notification;
  message_reset(session);
}

int session_receive(rpc_session_t *session, const char *data, size_t len) {
  callback_with_arg_t *cbarg;

  while (len) {
    int status;

    switch (session->state) {
      case RECEIVE_HEADER:
        status = mpack_rpc_receive(&session->rpc, &data, &len, &session->msg);

        if (status >= MPACK_RPC_REQUEST && status <= MPACK_RPC_NOTIFICATION) {
          session->msgtype = status;
          session->state = RECEIVE_METHOD_OR_ERROR;
        } else if (status == MPACK_EOF) {
          /* need more data */
          return MPACK_EOF;
        } else  {
          return MPACK_ERROR;
        }
        break;
      case RECEIVE_METHOD_OR_ERROR:
      case RECEIVE_ARGS_OR_RESULT:
        status = mpack_parse(&session->unpacker, &data, &len,
            parse_enter, parse_exit);

        if (status == MPACK_OK) {
          if (session->state == RECEIVE_METHOD_OR_ERROR) {
            session->state = RECEIVE_ARGS_OR_RESULT;
          } else {
            goto message_complete;
          }
        } else if (status == MPACK_EOF) {
          /* need more data */
          return MPACK_EOF;
        } else {
          return MPACK_ERROR;
        }
        break;
      default:
        die("unexpected rpc state");
        break;
    }
    continue;

message_complete:
    switch (session->msgtype) {
      case MPACK_RPC_REQUEST:
        session->on_request(
            session,
            session->method_or_error,
            session->args_or_result,
            session->msg.id);
        break;
      case MPACK_RPC_NOTIFICATION:
        session->on_notification(
            session,
            session->method_or_error,
            session->args_or_result);
        break;
      case MPACK_RPC_RESPONSE:
        cbarg = session->msg.data.p;
        cbarg->callback(
            session,
            session->method_or_error,
            session->args_or_result,
            cbarg->callback_arg);
        free(cbarg);
        break;
      default:
        die("unexpected rpc state");
        break;
    }
    message_reset(session);
  }

  return MPACK_EOF;
}

cvector_vector_type(char) session_request(
    rpc_session_t *session,
    object_t method,
    object_t args,
    rpc_response_callback callback,
    void *callback_arg) {
  callback_with_arg_t *cb_with_arg;
  mpack_data_t caller_data;
  char *bufptr;
  size_t bufremaining;
  size_t prev_capacity;
  int status;
  cvector_vector_type(char) outbuf = NULL;

  cvector_reserve(outbuf, 0xff);
  bufptr = outbuf;
  bufremaining = cvector_capacity(outbuf);

  cb_with_arg = xmalloc(sizeof *cb_with_arg);
  cb_with_arg->callback = callback;
  cb_with_arg->callback_arg = callback_arg;
  caller_data.p = cb_with_arg;

  status = mpack_rpc_request(&session->rpc, &bufptr, &bufremaining, caller_data);

  if (status != MPACK_OK) {
    /* Since there's enough space in the vector, we know for sure that MPACK_EOF
     * is not a possibility. */
    goto error;
  }

  session->pack_root = method;
  do {
    status = mpack_unparse(&session->packer, &bufptr, &bufremaining,
        unparse_enter, unparse_exit);
    if (status == MPACK_ERROR) {
      goto error;
    } else if (status == MPACK_EOF) {
      assert(bufremaining == 0);
      prev_capacity = cvector_capacity(outbuf);
      cvector_reserve(outbuf, cvector_compute_next_grow(cvector_capacity(outbuf)));
      bufremaining = cvector_capacity(outbuf) - prev_capacity;
      bufptr = outbuf + prev_capacity;
    }
  } while (status != MPACK_OK);

  session->pack_root = args;
  do {
    status = mpack_unparse(&session->packer, &bufptr, &bufremaining,
        unparse_enter, unparse_exit);
    if (status == MPACK_ERROR) {
      goto error;
    } else if (status == MPACK_EOF) {
      assert(bufremaining == 0);
      prev_capacity = cvector_capacity(outbuf);
      cvector_reserve(outbuf, cvector_compute_next_grow(cvector_capacity(outbuf)));
      bufremaining = cvector_capacity(outbuf) - prev_capacity;
      bufptr = outbuf + prev_capacity;
    }
  } while (status != MPACK_OK);
  
  cvector_set_size(outbuf, (size_t)(bufptr - outbuf));

  return outbuf;

error:
    cvector_free(outbuf);
    free(cb_with_arg);
    return NULL;
}

cvector_vector_type(char) session_reply(
    rpc_session_t *session,
    object_t error,
    object_t result,
    mpack_uint32_t id) {
  char *bufptr;
  size_t bufremaining;
  size_t prev_capacity;
  int status;
  cvector_vector_type(char) outbuf = NULL;

  cvector_reserve(outbuf, 0xff);
  bufptr = outbuf;
  bufremaining = cvector_capacity(outbuf);

  status = mpack_rpc_reply(&session->rpc, &bufptr, &bufremaining, id);

  if (status != MPACK_OK) {
    /* Since there's enough space in the vector, we know for sure that MPACK_EOF
     * is not a possibility. */
    goto error;
  }

  session->pack_root = error;
  do {
    status = mpack_unparse(&session->packer, &bufptr, &bufremaining,
        unparse_enter, unparse_exit);
    if (status == MPACK_ERROR) {
      goto error;
    } else if (status == MPACK_EOF) {
      assert(bufremaining == 0);
      prev_capacity = cvector_capacity(outbuf);
      cvector_reserve(outbuf, cvector_compute_next_grow(cvector_capacity(outbuf)));
      bufremaining = cvector_capacity(outbuf) - prev_capacity;
      bufptr = outbuf + prev_capacity;
    }
  } while (status != MPACK_OK);

  session->pack_root = result;
  do {
    status = mpack_unparse(&session->packer, &bufptr, &bufremaining,
        unparse_enter, unparse_exit);
    if (status == MPACK_ERROR) {
      goto error;
    } else if (status == MPACK_EOF) {
      assert(bufremaining == 0);
      prev_capacity = cvector_capacity(outbuf);
      cvector_reserve(outbuf, cvector_compute_next_grow(cvector_capacity(outbuf)));
      bufremaining = cvector_capacity(outbuf) - prev_capacity;
      bufptr = outbuf + prev_capacity;
    }
  } while (status != MPACK_OK);
  
  cvector_set_size(outbuf, (size_t)(bufptr - outbuf));

  return outbuf;

error:
    cvector_free(outbuf);
    return NULL;
}
