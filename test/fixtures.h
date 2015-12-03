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

extern const int fixture_count;
extern const struct fixture fixtures[];

#endif /* MPACK_TEST_FIXTURES_H */
