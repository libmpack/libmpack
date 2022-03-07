#ifndef RPC_H_
#define RPC_H_

#include <mpack.h>

#include "util.h"

typedef struct object_s object_t;
typedef struct kv_s kv_t;
typedef struct rpc_session_s rpc_session_t;

typedef void (*rpc_request_callback)(
    rpc_session_t *session,
    object_t method,
    object_t args,
    mpack_uint32_t request_id);

typedef void (*rpc_notification_callback)(
    rpc_session_t *session,
    object_t method,
    object_t args);

typedef void (*rpc_response_callback)(
    rpc_session_t *session,
    object_t error,
    object_t result,
    void *callback_arg);

typedef enum {
  NIL       = 1,
  BOOLEAN   = 2,
  NUMBER    = 3,
  ARRAY     = 4,
  MAP       = 5,
  STR       = 6
} object_type_t;

struct object_s {
  object_type_t type;
  union {
    bool b;
    double n;
    char *s;
    cvector_vector_type(object_t) a;
    cvector_vector_type(kv_t) m;
  } data;
};

struct kv_s {
  object_t key, val;
};

enum {
  RECEIVE_HEADER = 0,
  RECEIVE_METHOD_OR_ERROR,
  RECEIVE_ARGS_OR_RESULT
};

struct rpc_session_s {
  mpack_rpc_session_t rpc;
  mpack_parser_t packer, unpacker;
  mpack_rpc_message_t msg;
  int msgtype;
  int state;
  object_t pack_root;
  object_t method_or_error;
  object_t args_or_result;
  cvector_vector_type(char) outbuf;
  rpc_request_callback on_request;
  rpc_notification_callback on_notification;
};

void session_init(
    rpc_session_t *session,
    rpc_request_callback on_request,
    rpc_notification_callback on_notification);

int session_receive(rpc_session_t *session, const char *data, size_t len);

cvector_vector_type(char) session_request(
    rpc_session_t *session,
    object_t method,
    object_t args,
    rpc_response_callback callback,
    void *callback_arg);

cvector_vector_type(char) session_reply(
    rpc_session_t *session,
    object_t error,
    object_t result,
    mpack_uint32_t id);

void object_destroy(object_t obj);


#endif  /* RPC_H_ */
