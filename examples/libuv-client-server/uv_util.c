#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

#include "util.h"
#include "uv_util.h"

#define DEFAUL_PORT 8000

typedef struct write_req_s {
  uv_write_t req;
  uv_buf_t buf;
} write_req_t;

static int get_port(void) {
  long parsed;
  const char *env_port = getenv("PORT");

  if (!env_port || parse_long(env_port, &parsed) || parsed >= 0xffff) {
    return DEFAUL_PORT;
  }

  return (int)parsed;
}

static void alloc_cb(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
  buf->base = xmalloc(suggested_size);
  buf->len = suggested_size;
}

static void close_cb(uv_handle_t* handle) {
  if (handle->data) {
    free(handle->data);
  }
  free(handle);
}

static void write_cb(uv_write_t* req, int status) {
  write_req_t* wr = (write_req_t*)req;

  if (wr->buf.base != NULL) {
    cvector_vector_type(char) data = wr->buf.base;
    cvector_free(data);
  }

  free(wr);

  if (status == 0) {
    return;
  }

  fprintf(stderr, "uv_write error: %s\n", uv_strerror(status));

  if (status == UV_ECANCELED) {
    return;
  }

  uv_close((uv_handle_t*)req->handle, close_cb);
}

static void shutdown_cb(uv_shutdown_t* req, int status) {
  if (status) {
    fprintf(stderr, "err: %s\n", uv_strerror(status));
  }
  uv_close((uv_handle_t*)req->handle, close_cb);
  free(req);
}

static void read_cb(uv_stream_t* handle, ssize_t nread, const uv_buf_t* buf) {
  loop_state_t *state;
  uv_shutdown_t* req;

  if (nread <= 0 && buf->base != NULL) {
    free(buf->base);
  }

  if (nread == 0) {
    /* EOF */
    return;
  }

  if (nread < 0) {
    fprintf(stderr, "err: %s\n", uv_strerror((int)nread));
    req = xmalloc(sizeof *req);
    if (uv_shutdown(req, handle, shutdown_cb)) {
      die("uv_shutdown failed");
    }

    return;
  }

  state = container_of(handle->loop, loop_state_t, loop);
  state->on_read(state, handle, buf->base, (size_t)nread);
}

static void connection_cb(uv_stream_t* server, int status) {
  loop_state_t *state;
  uv_tcp_t* stream;

  if (status < 0) {
    fprintf(stderr, "connection error %s\n", uv_strerror(status));
    return;
  }

  stream = xmalloc(sizeof *stream);

  if (uv_tcp_init(server->loop, stream)) {
    die("uv_tcp_init failed");
  }

  stream->data = server;

  if (uv_accept(server, (uv_stream_t*)stream)) {
    die("uv_accept failed");
  }

  if (uv_read_start((uv_stream_t*)stream, alloc_cb, read_cb)) {
    die("uv_read_start failed");
  }

  state = container_of(server->loop, loop_state_t, loop);

  if (state->on_connection) {
    state->on_connection(state, (uv_stream_t *)stream, server->data);
  }
}

static void connect_cb(uv_connect_t *req, int status) {
  loop_state_t *state;
  uv_stream_t* stream;
  void *data;

  if (status < 0) {
    fprintf(stderr, "failed to connect: %s\n", uv_strerror(status));
    return;
  }

  stream = req->handle;
  data = req->data;
  free(req);
  if (uv_read_start(stream, alloc_cb, read_cb)) {
    die("uv_read_start failed");
  }

  state = container_of(stream->loop, loop_state_t, loop);
  state->on_connection(state, stream, data);
}

static void walk_cb(uv_handle_t *handle, void *arg) {
  uv_close(handle, close_cb);
}

static void signal_cb(uv_signal_t *handle, int signum) {
  uv_walk(handle->loop, walk_cb, NULL);
}

void stream_close(uv_stream_t *stream) {
  uv_close((uv_handle_t *)stream, close_cb);
}

void stream_write(uv_stream_t *stream, cvector_vector_type(char) data) {
  write_req_t* wr;

  wr = xmalloc(sizeof *wr);
  wr->buf = uv_buf_init(data, (unsigned)cvector_size(data));
  if (uv_write(&wr->req, stream, &wr->buf, 1, write_cb)) {
    die("uv_write failed");
  }
}

void loop_init(loop_state_t *state) {
  memset(state, 0, sizeof *state);
  uv_loop_init(&state->loop);
}

void loop_connect(
    loop_state_t *state,
    connection_callback on_connection,
    read_callback on_read,
    void *data) {
  struct sockaddr_in addr;
  uv_tcp_t *client;
  uv_connect_t *req;

  if (state->on_read || state->on_connection) {
    die("callbacks already set");
  }

  state->on_read = on_read;
  state->on_connection = on_connection;

  req = xmalloc(sizeof *req);
  req->data = data;
  client = xmalloc(sizeof *client);

  if (uv_tcp_init(&state->loop, client)) {
    die("uv_tcp_init failed");
  }

  if (uv_ip4_addr("127.0.0.1", get_port(), &addr)) {
    die("uv_ip4_addr failed");
  }

  if (uv_tcp_connect(req, client, (const struct sockaddr *)&addr,
        connect_cb)) {
    die("uv_tcp_connect failed");
  }
}

void loop_listen(
    loop_state_t *state,
    connection_callback on_connection,
    read_callback on_read,
    void *data) {
  struct sockaddr_in addr;
  uv_signal_t *signal;
  uv_tcp_t *server;
  uv_loop_t *loop;

  if (state->on_read || state->on_connection) {
    die("callbacks already set");
  }
  state->on_read = on_read;
  state->on_connection = on_connection;

  loop = &state->loop;

  server = xcalloc(1, sizeof *server);
  signal = xcalloc(1, sizeof *signal);

  if (uv_signal_init(&state->loop, signal)) {
    die("uv_signal_init failed");
  }

  if (uv_signal_start(signal, signal_cb, SIGINT)) {
    die("uv_signal_start failed");
  }

  if (uv_tcp_init(loop, server)) {
    die("uv_tcp_init failed");
  }

  if (uv_ip4_addr("127.0.0.1", get_port(), &addr)) {
    die("uv_ip4_addr failed");
  }

  if (uv_tcp_bind(server, (const struct sockaddr*)&addr, 0)) {
    die("uv_tcp_bind failed");
  }

  if (uv_listen((uv_stream_t*)server, SOMAXCONN, connection_cb)) {
    die("uv_listen failed");
  }

  server->data = data;
}

void loop_run(loop_state_t *state) {
  uv_run(&state->loop, UV_RUN_DEFAULT);
}

void loop_cleanup(loop_state_t *state) {
  if (uv_loop_close(&state->loop)) {
    die("uv_loop_close failed");
  }
}
