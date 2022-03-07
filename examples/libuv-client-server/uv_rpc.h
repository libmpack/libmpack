#ifndef UV_RPC_H_
#define UV_RPC_H_

#include "rpc.h"
#include "uv_util.h"
#include <uv.h>

typedef struct loop_rpc_session_s loop_rpc_session_t;

typedef void (*loop_rpc_connection_callback)(loop_rpc_session_t *session);

typedef void (*loop_rpc_request_callback)(
    loop_rpc_session_t *session,
    object_t method,
    object_t args,
    mpack_uint32_t request_id
    );

typedef void (*loop_rpc_notification_callback)(
    loop_rpc_session_t *session,
    object_t method,
    object_t args
    );

typedef void (*loop_rpc_response_callback)(
    loop_rpc_session_t *session,
    object_t error,
    object_t result,
    void *callback_arg);

struct loop_rpc_session_s {
  rpc_session_t session;
  uv_stream_t *stream;
  loop_rpc_request_callback on_request;
  loop_rpc_notification_callback on_notification;
};

void loop_rpc_connect(
    loop_state_t *state,
    loop_rpc_connection_callback on_connection,
    loop_rpc_request_callback on_request,
    loop_rpc_notification_callback on_notification
    );

void loop_rpc_listen(
    loop_state_t *state,
    loop_rpc_connection_callback on_connection,
    loop_rpc_request_callback on_request,
    loop_rpc_notification_callback on_notification
    );

void loop_rpc_request(
    loop_rpc_session_t *session,
    object_t method,
    object_t args,
    loop_rpc_response_callback on_response,
    void *data);

void loop_rpc_reply(
    loop_rpc_session_t *session,
    object_t error,
    object_t result,
    mpack_uint32_t id);

void loop_rpc_session_destroy(loop_rpc_session_t *session);

#endif  /* UV_RPC_H_ */
