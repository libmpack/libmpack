#include "uv_rpc.h"
#include "rpc.h"
#include "util.h"
#include "uv_util.h"

typedef struct callback_with_arg_s {
  loop_rpc_response_callback callback;
  void *callback_arg;
} callback_with_arg_t;

typedef struct loop_rpc_callbacks_s {
  loop_rpc_connection_callback on_connection;
  loop_rpc_request_callback on_request;
  loop_rpc_notification_callback on_notification;
  int free_on_connection;
} loop_rpc_callbacks_t;

static void on_rpc_request(
    rpc_session_t *rpc_session,
    object_t method,
    object_t args,
    mpack_uint32_t request_id)
{
  loop_rpc_session_t *session;

  session = container_of(rpc_session, loop_rpc_session_t, session);
  session->on_request(session, method, args, request_id);
}

static void on_rpc_notification(
    rpc_session_t *rpc_session,
    object_t method,
    object_t args)
{
  loop_rpc_session_t *session;

  session = container_of(rpc_session, loop_rpc_session_t, session);
  session->on_notification(session, method, args);
}

static void connection_established(
    loop_state_t *state,
    uv_stream_t *stream,
    void *data) {
  loop_rpc_session_t *session;
  loop_rpc_callbacks_t *callbacks = data;

  session = xmalloc(sizeof *session);
  session->stream = stream;
  session_init(&session->session, on_rpc_request, on_rpc_notification);
  session->on_request = callbacks->on_request;
  session->on_notification = callbacks->on_notification;

  stream->data = session;

  if (callbacks->on_connection) {
    callbacks->on_connection(session);
  }

  if (callbacks->free_on_connection) {
    free(callbacks);
  }
}

static void data_received(loop_state_t *state, uv_stream_t *stream, char *data,
    size_t len) {
  rpc_session_t *session = stream->data;

  if (session_receive(session, data, len) == MPACK_ERROR) {
    stream_close(stream);
  }

  free(data);
}

static loop_rpc_callbacks_t *alloc_callbacks(
    loop_rpc_connection_callback on_connection,
    loop_rpc_request_callback on_request,
    loop_rpc_notification_callback on_notification,
    int free_on_connection
    )
{
  loop_rpc_callbacks_t *callbacks = xmalloc(sizeof *callbacks);
  callbacks->on_connection = on_connection;
  callbacks->on_request = on_request;
  callbacks->on_notification = on_notification;
  callbacks->free_on_connection = free_on_connection;
  return callbacks;
}

static void on_rpc_response(
    rpc_session_t *rpc_session, object_t error, object_t result, void *arg) {
  loop_rpc_session_t *session;
  callback_with_arg_t *cbarg;

  cbarg = arg;
  session = container_of(rpc_session, loop_rpc_session_t, session);
  cbarg->callback(session, error, result, cbarg->callback_arg);
  free(cbarg);
}

void loop_rpc_connect(
    loop_state_t *state,
    loop_rpc_connection_callback on_connection,
    loop_rpc_request_callback on_request,
    loop_rpc_notification_callback on_notification
    )
{
  loop_connect(state, connection_established, data_received, alloc_callbacks(
        on_connection, on_request, on_notification, 1));
}

void loop_rpc_listen(
    loop_state_t *state,
    loop_rpc_connection_callback on_connection,
    loop_rpc_request_callback on_request,
    loop_rpc_notification_callback on_notification
    )
{
  loop_listen(state, connection_established, data_received, alloc_callbacks(
        on_connection, on_request, on_notification, 0));
}

void loop_rpc_request(
    loop_rpc_session_t *session,
    object_t method,
    object_t args,
    loop_rpc_response_callback on_response,
    void *data)
{
  callback_with_arg_t *cbarg;

  cbarg = xmalloc(sizeof *cbarg);
  cbarg->callback = on_response;
  cbarg->callback_arg = data;

  stream_write(session->stream,
      session_request(&session->session, method, args, on_rpc_response, cbarg));
  object_destroy(method);
  object_destroy(args);
}

void loop_rpc_reply(
    loop_rpc_session_t *session,
    object_t error,
    object_t result,
    mpack_uint32_t id)
{
  stream_write(session->stream,
      session_reply(&session->session, error, result, id));
}

void loop_rpc_session_destroy(loop_rpc_session_t *session)
{
  stream_close(session->stream);
  free(session);
}
