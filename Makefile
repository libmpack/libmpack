# Some parts of this Makefile were taken or adapted from libunibilium:
# https://github.com/mauke/unibilium

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

XCFLAGS += -Wall -Wextra -Wconversion -Wstrict-prototypes -pedantic

ifeq ($(ANSI),1)
  # test helper to validate that the library works when compiled as c90
  # (without native 32-bit integers, for example)
  XCFLAGS += -DFORCE_32BIT_INTS -ansi
else
  XCFLAGS += -std=c99
endif

NAME    := mpack
MAJOR   := 1
MINOR   := 0
PATCH   := 3
VERSION := $(MAJOR).$(MINOR).$(PATCH)

LT_REVISION=0
LT_CURRENT=0
LT_AGE=0

PREFIX  ?= /usr/local
LIBDIR  ?= $(PREFIX)/lib
INCDIR  ?= $(PREFIX)/include
SRCDIR  ?= src
TESTDIR ?= test
BINDIR  ?= build
OUTDIR  ?= $(BINDIR)/$(config)

SRC     := core.c conv.c object.c rpc.c
SRC     := $(addprefix $(SRCDIR)/,$(SRC))
HDRS    := $(SRC:.c=.h)
OBJ     := $(addprefix $(OUTDIR)/,$(SRC:.c=.lo))
LIBRARY := lib$(NAME).la
LIB     := $(OUTDIR)/$(LIBRARY)
TSRC    := $(wildcard $(TESTDIR)/*.c) $(TESTDIR)/deps/tap/tap.c
TOBJ    := $(addprefix $(OUTDIR)/,$(TSRC:.c=.lo))
TEXE    := $(OUTDIR)/run-tests
AMALG   := $(BINDIR)/$(NAME).c
AMALG_H := $(AMALG:.c=.h)
COVOUT  := $(OUTDIR)/gcov.txt
PROFOUT := $(OUTDIR)/gprof.txt

TEST_FILTER_OUT := --coverage -ansi -std=c99

.PHONY: all
all: lib-bin test-bin

include .config/$(config).mk

.PHONY: tools
tools: $(COMPILER) $(RUNNER)

.PHONY: amalgamation
amalgamation: $(AMALG)

.PHONY: lib-bin
lib-bin: tools $(LIB)

.PHONY: test-bin
test-bin: lib-bin $(TEXE)

.PHONY: test
test: test-bin
	@$(RUNNER) $(TEXE)

.PHONY: gdb
gdb: test-bin
	gdb -x .gdb $(TEXE)

.PHONY: coverage
coverage: tools $(COVOUT)
	cat $(COVOUT)

.PHONY: profile
profile: tools $(PROFOUT)
	cat $(PROFOUT)

.PHONY: compile_commands.json
compile_commands.json:
	rm -f $(BINDIR)/compile_commands.json
	$(MAKE) config=$(config) clean
	bear $(MAKE) config=$(config)
	mv compile_commands.json $(BINDIR)

.PHONY: install
install: install-inc install-lib
	$(LIBTOOL) --mode=finish '$(DESTDIR)$(LIBDIR)'

.PHONY: install-inc
install-inc: $(AMALG_H) mpack.pc.in
	mkdir -p '$(DESTDIR)$(INCDIR)'
	install -m644 $(AMALG_H) '$(DESTDIR)$(INCDIR)'
	mkdir -p '$(DESTDIR)$(LIBDIR)/pkgconfig'
	sed 's,@VERSION@,$(VERSION),;s,@LIBDIR@,$(LIBDIR),;s,@INCDIR@,$(INCDIR),' <mpack.pc.in >'$(DESTDIR)$(LIBDIR)/pkgconfig/mpack.pc'

.PHONY: install-lib
install-lib: $(LIB)
	mkdir -p '$(DESTDIR)$(LIBDIR)'
	$(LIBTOOL) --mode=install cp $(LIB) '$(DESTDIR)$(LIBDIR)/$(LIBRARY)'

.PHONY: clean
clean:
	rm -rf $(BINDIR)/$(config)

$(TOBJ): XCFLAGS := $(filter-out $(TEST_FILTER_OUT),$(XCFLAGS)) \
	-std=gnu99 -Wno-conversion -Wno-unused-parameter

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

$(OUTDIR)/%.lo: %.c $(AMALG)
	@echo compile $< =\> $@
	@$(LIBTOOL) --mode=compile --tag=CC $(CC) $(XCFLAGS) $(CFLAGS) -o $@ -c $<

$(LIB): $(OBJ)
	@echo link $^ =\> $@
	@$(LIBTOOL) --mode=link --tag=CC $(CC) $(XLDFLAGS) $(LDFLAGS) \
		-rpath '$(LIBDIR)' \
		-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) -o $@ $^

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
