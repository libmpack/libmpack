#include <stdio.h>
#include <stdlib.h>

#include <uv.h>
#include <mpack.h>

#include "util.h"
#include "uv_rpc.h"

static void on_response(
    loop_rpc_session_t *session, object_t error, object_t result, void *arg) {
  stream_close(session->stream);
  printf("RECEIVED RESPONSE!\n");
}

static void connection_established(loop_rpc_session_t *session) {
  object_t method;
  object_t args;

  method.type = STR;
  method.data.s = xstrdup("HELLO");

  args.type = ARRAY;
  args.data.a = NULL;
  cvector_reserve(args.data.a, 2);
  args.data.a[0].type = NUMBER;
  args.data.a[0].data.n = 55;
  args.data.a[1].type = NUMBER;
  args.data.a[1].data.n = 56;
  cvector_set_size(args.data.a, 2);

  loop_rpc_request(session, method, args, on_response, NULL);
}

int main(void) {
  loop_state_t state;

  loop_init(&state);
  loop_rpc_connect(&state, connection_established, NULL, NULL);
  loop_run(&state);
  loop_cleanup(&state);

  return 0;
}
