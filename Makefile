config ?= debug
SYSTEM ?= $(shell uname -s)

ifeq ($(SYSTEM),Darwin)
  LIBTOOL ?= glibtool
else
  LIBTOOL ?= libtool
endif

ifneq ($(VERBOSE),1)
  LIBTOOL += --quiet
endif

ifneq "$(strip $(findstring clang,$(CC)))" ""
  # When CC is set to clang-${VERSION}, it is very likely that other llvm
  # tools are installed with the same version suffix, so use it for
  # llvm-symbolizer
  SYMBOLIZER ?= $(shell which $(subst clang,llvm-symbolizer,$(CC)))
endif

SYMBOLIZER ?= /usr/bin/llvm-symbolizer

.PHONY: all gdb lib-bin test-bin tools amalgamation test coverage profile clean \
	compile_commands.json

all: lib-bin test-bin

XCFLAGS += -Wall -Wextra -Wconversion -Wstrict-prototypes -pedantic

ifeq ($(ANSI),1)
  # test helper to validate that the library works when compiled as c90
  # (without native 32-bit integers, for example)
  XCFLAGS += -DFORCE_32BIT_INTS -ansi
else
  XCFLAGS += -std=c99
endif

NAME    := mpack
MAJOR   := 0
MINOR   := 0
PATCH   := 0
VERSION := $(MAJOR).$(MINOR).$(PATCH)

PREFIX  ?= /usr/local
LIBDIR  ?= $(PREFIX)/lib
INCDIR  ?= $(PREFIX)/include
SRCDIR  ?= src
TESTDIR ?= test
BINDIR  ?= build
OUTDIR  ?= $(BINDIR)/$(config)

SRC     := core.c conv.c object.c
SRC     := $(addprefix $(SRCDIR)/,$(SRC))
HDRS    := $(SRC:.c=.h)
OBJ     := $(addprefix $(OUTDIR)/,$(SRC:.c=.lo))
LIB     := $(OUTDIR)/lib$(NAME).la
TSRC    := $(wildcard $(TESTDIR)/*.c) $(TESTDIR)/deps/tap/tap.c
TOBJ    := $(addprefix $(OUTDIR)/,$(TSRC:.c=.lo))
TEXE    := $(OUTDIR)/run-tests
AMALG   := $(BINDIR)/$(NAME).c
AMALG_H := $(AMALG:.c=.h)
COVOUT  := $(OUTDIR)/gcov.txt
PROFOUT := $(OUTDIR)/gprof.txt

TEST_FILTER_OUT := --coverage -ansi -std=c99

include .config/$(config).mk

$(TOBJ): XCFLAGS := $(filter-out $(TEST_FILTER_OUT),$(XCFLAGS)) \
	-std=gnu99 -Wno-conversion -Wno-unused-parameter

tools: $(COMPILER) $(RUNNER)

amalgamation: $(AMALG)

lib-bin: tools $(LIB)

test-bin: lib-bin $(TEXE)

test: test-bin
	@$(RUNNER) $(TEXE)

gdb: test-bin
	gdb -x .gdb $(TEXE)

coverage: tools $(COVOUT)
	cat $<

profile: tools $(PROFOUT)
	cat $<

compile_commands.json:
	rm -f $(BINDIR)/compile_commands.json
	$(MAKE) config=$(config) clean
	bear -- $(MAKE) config=$(config)
	mv compile_commands.json $(BINDIR)

$(COVOUT): $(SRC) $(TSRC)
	find $(OUTDIR) -type f -name '*.gcda' -print0 | xargs -0 rm -f
	$(MAKE) CFLAGS='-DNDEBUG -g --coverage' LDFLAGS=--coverage config=$(config) test
	find $(OUTDIR)/src -type f -name '*.o' -print0 | \
		xargs -0 gcov -lp > $@
	rm *.c.gcov

$(PROFOUT): $(SRC) $(TSRC)
	$(MAKE) CFLAGS=-pg LDFLAGS=-pg config=$(config) test
	gprof $(OUTDIR)/run-tests gmon.out > $@
	rm gmon.out

clean:
	rm -rf $(BINDIR)/$(config)

$(OUTDIR)/%.lo: %.c $(AMALG)
	@echo compile $< =\> $@
	@$(LIBTOOL) --mode=compile --tag=CC $(CC) $(XCFLAGS) $(CFLAGS) -o $@ -c $<

$(LIB): $(OBJ)
	@echo link $^ =\> $@
	@$(LIBTOOL) --mode=link --tag=CC $(CC) $(XLDFLAGS) $(LDFLAGS) -o $@ $^

$(TEXE): $(LIB) $(TOBJ)
	@echo link $^ =\> $@
	@$(LIBTOOL) --mode=link --tag=CC $(CC) $(XLDFLAGS) $(LDFLAGS) -lm -g -O \
		-o $@ $(LIB) $(TOBJ)

$(AMALG_H): $(HDRS)
	mkdir -p $(BINDIR)
	cat $^ | sed '/^#include "/d' > $@

$(AMALG): $(AMALG_H) $(SRC)
	mkdir -p $(BINDIR)
	cat $^ | sed '/^#include "/d' > $@
