## libmpack

[![Travis Build Status](https://travis-ci.org/libmpack/libmpack.svg?branch=master)](https://travis-ci.org/libmpack/libmpack)
[![Coverage Status](https://coveralls.io/repos/libmpack/libmpack/badge.svg?branch=master&service=github)](https://coveralls.io/github/libmpack/libmpack?branch=master)

### Introduction

libmpack is a small binary serialization/RPC library that implements both the
[msgpack](https://github.com/msgpack/msgpack/blob/master/spec.md) and
[msgpack-rpc](https://github.com/msgpack-rpc/msgpack-rpc/blob/master/spec.md)
specifications.

### Concepts

See [libmpack-python](https://github.com/libmpack/libmpack-python#usage) for an overview, in particular the [RPC](https://github.com/libmpack/libmpack-python#rpc) section explains the lifecycle of a libmpack Session.

### Rationale

While there's already a [msgpack-c](https://github.com/msgpack/msgpack-c)
implementation, it has a few problems that libmpack aims to address:

* It couples msgpack serialization format with a set of predefined C typedefs.
  This means the user almost always has to recursively convert the allocated
  structures into some other application-specific format, especially if binding
  to another language. libmpack serialization/deserialization API is
  callback-based, making it simple to serialize/deserialize directly from/to
  application-specific objects.

* It is not trivial to simply include its files into another project(eg: a lua C
  module) since it relies too much on C99 features and compilation with
  -Wconversion issues a bunch of warnings. You need to build the library and
  link against it, which can be cumbersome for a simple serialization library
  that is being embedded into other projects(lua or node.js modules for
  example). libmpack provides an amalgamation build(single source file
  containing all code that can be #included) and should compile cleanly as part
  of any C89 project. It won't produce any warnings for
  `-Wall`/`-Wextra`/`-Wconversion`.

* msgpack-c doesn't work without allocating memory. For example, you can't
  send/receive simple primitives without a `msgpack_sbuffer_t`(which is then
  dynamically extended by msgpack internal functions). libmpack does no
  allocation at all, and provides some helpers to simplify dynamic allocation by
  the user, if required.

* There's no msgpack-rpc implementation for C(much less one that can be reused
  in other languages). libmpack has a simple and flexible msgpack-rpc
  implementation that can be used to easily create distributed applications
  across any kind of transport. Unlike some msgpack-rpc libraries(ruby official
  implementation for example), libmpack has no coupling with any network
  sink/source, allowing it to be used with any event loop library or
  system-specific networking APIs.

Here's a few extras that may or not overlap with what msgpack-c provides:

* Fully incremental/iterative parse/serialization API with no backtracking,
  which simplifies working with split buffers.

* Portable C89 library with zero system dependencies. In fact, it only uses one
  function from C standard library: memcpy. libmpack can be used even in
  OS/kernel development(eg: communicate with userspace using
  netlink/msgpack-rpc).

* Endian aware: it should work unmodified regardless of the system's byte order,
  and its CI infrastructure automatically tests it on a big endian platform.

* Well tested, it should always have about 100% code coverage:
  https://coveralls.io/github/libmpack/libmpack?branch=master

* Relatively small footprint: The amalgamation(headers + code) is less than 2k
  lines of C. The whole library can be inlined when compiled with -O3(Though
  this depends on compiler and usage, eg: how many call sites for certain
  functions).
