cmake_minimum_required(VERSION 3.10)

project(LibmpackClientServer)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)

pkg_check_modules(PC_LIBUV REQUIRED libuv)
include_directories(SYSTEM ${PC_LIBUV_INCLUDE_DIRS})
# pkg_check_modules(PC_MPACK REQUIRED mpack)
# include_directories(SYSTEM ${PC_MPACK_INCLUDE_DIRS})

if(NOT MSVC)
  # workaround bug in libuv headers:
  # https://github.com/libuv/libuv/issues/1160#issuecomment-941216924
  add_definitions(-D_XOPEN_SOURCE=600)
  # add some good compiler flags
  add_compile_options(-Wall -Wextra -pedantic -Wno-unused-parameter
    -Wstrict-prototypes -std=c89 -Wshadow -Wconversion -Wmissing-prototypes)
endif()

set(COMMON_SRC
  ${PROJECT_SOURCE_DIR}/../../build/mpack.c
  util.c
  uv_util.c
  rpc.c
  uv_rpc.c)

add_executable(client
  ${COMMON_SRC}
  client.c)
target_link_libraries(client
  ${PC_LIBUV_LIBRARIES}
  ${PC_MPACK_LIBRARIES})

add_executable(server
  ${COMMON_SRC}
  server.c)
target_link_libraries(server
  ${PC_LIBUV_LIBRARIES}
  ${PC_MPACK_LIBRARIES})
