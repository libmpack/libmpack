#ifndef UTIL_H_
#define UTIL_H_

#include <stdlib.h>

/* c-vector library from: https://github.com/eteran/c-vector */
#define cvector_clib_malloc xmalloc
#define cvector_clib_calloc xcalloc
#define cvector_clib_realloc xrealloc
#define CVECTOR_LOGARITHMIC_GROWTH
#include "cvector.h"

#define container_of(ptr, type, member) ((type *)( \
    (char *)ptr - offsetof(type, member)  \
    ))

#define array_size(arr) \
  ((sizeof(arr)/sizeof((arr)[0])) / \
   ((size_t)(!(sizeof(arr) % sizeof((arr)[0])))))

void die(const char *msg, ...);
void *xmalloc(size_t size);
void *xcalloc(size_t item_count, size_t item_size);
void *xrealloc(void *ptr, size_t size);

void *xstrdup(const char *s);
int parse_long(const char *in, long *out);
int parse_double(const char *in, double *out);


#endif  /* UTIL_H_ */
