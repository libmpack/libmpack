config ?= debug

-include local.mk

ifneq "$(strip $(findstring clang,$(CC)))" ""
SYMBOLIZER := $(shell which $(subst clang,llvm-symbolizer,$(CC)))
export ASAN_OPTIONS := log_path=asan:detect_leaks=1
export ASAN_SYMBOLIZER_PATH := $(SYMBOLIZER)
export MSAN_OPTIONS := log_path=msan
export MSAN_SYMBOLIZER_PATH := $(SYMBOLIZER)
export UBSAN_OPTIONS := log_path=ubsan
export UBSAN_SYMBOLIZER_PATH := $(SYMBOLIZER)
endif

export CK_FORK := no

test: bin
	./build/bin/$(config)/mpack-test

gdb: bin
	gdb -x .gdb ./build/bin/$(config)/mpack-test

bin: build
	cd build && $(MAKE) config=$(config)

build: premake5.lua
	premake5 gmake && premake5 export-compile-commands || true

clean:
	rm -rf build

.PHONY: test bin clean
