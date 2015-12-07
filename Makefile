config ?= debug
arch ?= $(shell uname -m | grep 64 > /dev/null && echo x64 || echo x32)
premake_config := $(config)_$(arch)

FETCH ?= curl -L -o -
SYSTEM ?= $(shell uname -s)

ifeq "$(SYSTEM)" "Darwin"
PREMAKE_PLAT := macosx
else
PREMAKE_PLAT := unix
endif

PREMAKE_URL_PREFIX := https://github.com/premake/premake-core/releases/download
PREMAKE_VERSION ?= 5.0.0-alpha6
PREMAKE_FETCH_URL := $(PREMAKE_URL_PREFIX)/v$(PREMAKE_VERSION)/premake-$(PREMAKE_VERSION)-src.zip
PREMAKE_FALLBACK_DIR ?= $(shell pwd)/.deps/usr/bin
PREMAKE_GET_CMD = mkdir -p .deps/src/premake && \
									 cd .deps/src/premake && \
									 $(FETCH) $(PREMAKE_FETCH_URL) > premake-src.zip && \
									 unzip premake-src.zip && \
									 cd premake-$(PREMAKE_VERSION) && \
									 cd build/gmake.$(PREMAKE_PLAT) && \
									 make config=release && \
									 mkdir -p $(PREMAKE_FALLBACK_DIR) && \
									 cp ../../bin/release/premake5 $(PREMAKE_FALLBACK_DIR)/premake5


PREMAKE_BIN ?= $(shell which premake5 2> /dev/null)


ifeq "$(strip $(PREMAKE_BIN))" ""
PREMAKE_BIN := $(PREMAKE_FALLBACK_DIR)/premake5
NEED_FETCH := $(shell [ -x $(PREMAKE_BIN) ] || echo "y")
ifeq "$(NEED_FETCH)" "y"
PREMAKE_BIN_TGT = $(PREMAKE_GET_CMD)
endif
endif

# ifneq "$(strip $(findstring clang,$(CC)))" ""
# SYMBOLIZER := $(shell which $(subst clang,llvm-symbolizer,$(CC)))
# export ASAN_OPTIONS := log_path=asan:detect_leaks=1
# export ASAN_SYMBOLIZER_PATH := $(SYMBOLIZER)
# export MSAN_OPTIONS := log_path=msan
# export MSAN_SYMBOLIZER_PATH := $(SYMBOLIZER)
# export UBSAN_OPTIONS := log_path=ubsan
# export UBSAN_SYMBOLIZER_PATH := $(SYMBOLIZER)
# endif

-include local.mk

all: bin

$(PREMAKE_BIN):
	$(PREMAKE_BIN_TGT)

export CK_FORK := no

coverage:
	find build/ -type f -name '*.gcda' -print0 | xargs -0 rm -f && \
		$(MAKE) config=coverage arch=$(arch) test && \
		cd build && \
		find obj/$(arch)/coverage/mpack -type f -name '*.o' -print0 | xargs -0 gcov

profile:
	$(MAKE) config=profile arch=$(arch) test && \
		gprof build/bin/$(arch)/profile/mpack-test gmon.out > gprof.txt && \
		rm gmon.out && echo profiler output in gprof.txt

test: test-bin
	./build/bin/$(arch)/$(config)/mpack-test

gdb: test-bin
	gdb -x .gdb ./build/bin/$(arch)/$(config)/mpack-test

test-bin: bin
	cd build && $(MAKE) config=$(premake_config) mpack-test

bin: build
	cd build && $(MAKE) config=$(premake_config) mpack

build: $(PREMAKE_BIN)
	$(PREMAKE_BIN) gmake

clean:
	rm -rf build

.PHONY: test coverage gdb bin clean
