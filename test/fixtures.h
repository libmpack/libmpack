#ifndef MPACK_TEST_FIXTURES_H
#define MPACK_TEST_FIXTURES_H

#include <stddef.h>
#include <stdint.h>

#ifndef MIN
# define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#endif
#define ARRAY_SIZE(arr) \
  ((sizeof(arr)/sizeof((arr)[0])) / \
   ((size_t)(!(sizeof(arr) % sizeof((arr)[0])))))

struct fixture {
  char *json;
  uint8_t *msgpack;
  size_t msgpacklen;
  void(*generator)(char **json,
                   uint8_t **msgpack,
                   size_t *msgpacklen,
                   size_t size); 
  size_t generator_size;
};

struct rpc_message {
  char *payload;
  int type;
  uint32_t id;
  char *method;
  char *args;
  char *error;
  char *result;
};

struct rpc_fixture {
  struct rpc_message *messages;
  size_t count;
  size_t capacity;
};

extern const int fixture_count;
extern const struct fixture fixtures[];

extern const int number_fixture_count;
extern const struct fixture number_fixtures[];

extern const int rpc_fixture_count;
extern const struct rpc_fixture rpc_fixtures[];

#endif /* MPACK_TEST_FIXTURES_H */
