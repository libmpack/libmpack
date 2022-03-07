#ifndef UV_UTIL_H_
#define UV_UTIL_H_

#include <uv.h>

#include "util.h"

typedef struct loop_state_s loop_state_t;

typedef void (*connection_callback)(loop_state_t* state, uv_stream_t *stream,
    void *data);
typedef void (*read_callback)(
    loop_state_t* state,
    uv_stream_t *stream,
    char *data,
    size_t len
    );

struct loop_state_s {
  uv_loop_t loop;
  connection_callback on_connection;
  read_callback on_read; 
};

void stream_write(uv_stream_t *stream, cvector_vector_type(char) data);
void stream_close(uv_stream_t *stream);
void loop_init(loop_state_t *state);
void loop_connect(
    loop_state_t *state,
    connection_callback on_connection,
    read_callback on_read,
    void *data
    );
void loop_listen(
    loop_state_t *state,
    connection_callback on_connection,
    read_callback on_read,
    void *data
    );
void loop_run(loop_state_t *state);
void loop_cleanup(loop_state_t *state);

#endif  /* UTIL_H_ */
