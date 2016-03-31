# makefile to setup environment for travis and development

# Lua-related configuration
LUA_VERSION ?= 5.1.5
LUA_VERSION_NOPATCH = $(shell echo -n $(LUA_VERSION) | sed 's!\([0-9]\.[0-9]\).[0-9]!\1!')
LUA_URL ?= https://github.com/lua/lua/releases/download/$(LUA_VERSION)/lua-$(LUA_VERSION).tar.gz
LUAROCKS_URL ?= https://github.com/keplerproject/luarocks/archive/v2.2.0.tar.gz
LUA_TARGET ?= linux

# deps location
DEPS_DIR ?= $(shell pwd)/.deps/$(LUA_VERSION)
DEPS_PREFIX ?= $(DEPS_DIR)/usr
DEPS_BIN ?= $(DEPS_PREFIX)/bin

# targets
LUA ?= $(DEPS_BIN)/lua
LUAROCKS ?= $(DEPS_BIN)/luarocks
BUSTED ?= $(DEPS_BIN)/busted
MPACK ?= $(DEPS_PREFIX)/lib/lua/$(LUA_VERSION_NOPATCH)/mpack.so

# Compilation
CC ?= gcc
CFLAGS := -ansi -O0 -g3 -fPIC -Wall -Wextra -Werror -Wconversion \
	-Wstrict-prototypes -Wno-unused-parameter -pedantic \
	-DMPACK_DEBUG_REGISTRY_LEAK

# Misc
# Options used by the 'valgrind' target, which runs the tests under valgrind
VALGRIND_OPTS ?= --error-exitcode=1 --log-file=valgrind.log --leak-check=yes \
	--track-origins=yes
# Command that will download a file and pipe it's contents to stdout
FETCH ?= curl -L -o -
# Command that will gunzip/untar a file from stdin to the current directory,
# stripping one directory component
UNTGZ ?= tar xfz - --strip-components=1


all: $(MPACK)

depsclean:
	rm -rf $(DEPS_DIR)

test: $(BUSTED) $(MPACK)
	$(BUSTED) -o gtest test.lua

valgrind: $(BUSTED) $(MPACK)
	eval $$($(LUAROCKS) path); \
	valgrind $(VALGRIND_OPTS) $(LUA) \
		$(DEPS_PREFIX)/lib/luarocks/rocks/busted/2.0.rc11-0/bin/busted test.lua

gdb: $(BUSTED) $(MPACK)
	eval $$($(LUAROCKS) path); \
	gdb -x .gdb --args $(LUA) \
		$(DEPS_PREFIX)/lib/luarocks/rocks/busted/2.0.rc11-0/bin/busted test.lua

$(MPACK): $(LUAROCKS) lmpack.c
	$(LUAROCKS) make CFLAGS='$(CFLAGS)'

$(BUSTED): $(LUAROCKS)
	$(LUAROCKS) install busted
	$(LUAROCKS) install inspect  # helpful for debugging

$(LUAROCKS): $(LUA)
	dir="$(DEPS_DIR)/src/luarocks"; \
	mkdir -p $$dir && cd $$dir && \
	$(FETCH) $(LUAROCKS_URL) | $(UNTGZ) && \
	./configure --prefix=$(DEPS_PREFIX) --force-config \
		--with-lua=$(DEPS_PREFIX) && make bootstrap

$(LUA):
	dir="$(DEPS_DIR)/src/lua"; \
	mkdir -p $$dir && cd $$dir && \
	$(FETCH) $(LUA_URL) | $(UNTGZ) && \
	sed -i -e '/^CFLAGS/s/-O2/-g3/' src/Makefile && \
	make $(LUA_TARGET) install INSTALL_TOP=$(DEPS_PREFIX)

.PHONY: all depsclean test gdb valgrind
