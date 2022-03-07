#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

void die(const char *msg, ...) {
  va_list ap;

  va_start(ap, msg);
  vfprintf(stderr, msg, ap);
  va_end(ap);
  fputc('\n', stderr);
  exit(1);
}

void *xmalloc(size_t s) {
  void *rv = malloc(s);

  if (!rv) {
    die("memory allocation failed");
  }

  return rv;
}

void *xcalloc(size_t item_count, size_t item_size) {
  void *rv = calloc(item_count, item_size);

  if (!rv) {
    die("memory allocation failed");
  }

  return rv;
}

void *xrealloc(void *ptr, size_t size) {
  void *rv = realloc(ptr, size);

  if (!rv) {
    die("memory allocation failed");
  }

  return rv;
}

void *xstrdup(const char *s) {
  void *rv = strdup(s);

  if (!rv) {
    die("memory allocation failed");
  }

  return rv;
}

int parse_long(const char *in, long *out)
{
    char *end;
    long val;
    errno = 0;
    val = strtol(in, &end, 10);
    if (end == in || *end != 0 || errno)  {
        return -1;
    }
    *out = val;
    return 0;
}

int parse_double(const char *in, double *out)
{
    char *end;
    double val;
    errno = 0;
    val = strtod(in, &end);
    if (end == in || *end != 0 || errno) {
        return -1;
    }
    *out = val;
    return 0;
}

