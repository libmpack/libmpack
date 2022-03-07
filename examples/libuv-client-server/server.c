#include <mpack.h>

#include "util.h"
#include "uv_rpc.h"

static void on_request(
    loop_rpc_session_t *session,
    object_t method,
    object_t args,
    mpack_uint32_t request_id) {
  object_t *it;

  printf("RECEIVED REQUEST: %s\n", method.data.s);
  for (it = cvector_begin(args.data.a); it != cvector_end(args.data.a); it++) {
    printf("ARG: %f\n", it->data.n);
  }

  loop_rpc_reply(session, method, args, request_id);
}

static void on_notification(
    loop_rpc_session_t *session,
    object_t method,
    object_t args) {
  printf("RECEIVED NOTIFICATION\n");
}

int main(void) {
  loop_state_t state;

  loop_init(&state);
  loop_rpc_listen(&state, NULL, on_request, on_notification);
  loop_run(&state);
  loop_cleanup(&state);

  return 0;
}
