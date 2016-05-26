# makefile to setup environment for travis and development

# distributors probably want to set this to 'yes' for both make and make install
USE_SYSTEM_LUA ?= no

# Lua-related configuration
LUA_VERSION_MAJ_MIN ?= 5.1
LUA_VERSION ?= $(LUA_VERSION_MAJ_MIN).5
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
ifeq ($(USE_SYSTEM_LUA),no)
MPACK ?= $(DEPS_PREFIX)/lib/lua/$(LUA_VERSION_NOPATCH)/mpack.so
else
MPACK ?= mpack.so
endif

# Compilation
CC ?= gcc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -ansi -O0 -g3 -Wall -Wextra -Werror -Wconversion \
	-Wstrict-prototypes -Wno-unused-parameter -pedantic
CFLAGS += -fPIC -DMPACK_DEBUG_REGISTRY_LEAK

LUA_INCLUDE := $(shell $(PKG_CONFIG) --cflags lua-$(LUA_VERSION_MAJ_MIN) 2>/dev/null || echo "-I/usr/include/lua$(LUA_VERSION_MAJ_MIN)")
LUA_LIB := $(shell $(PKG_CONFIG) --libs lua-$(LUA_VERSION_MAJ_MIN) 2>/dev/null || echo "-llua$(LUA_VERSION_MAJ_MIN)")
INCLUDES = $(LUA_INCLUDE)
LIBS = $(LUA_LIB)

LUA_CMOD_INSTALLDIR := $(shell $(PKG_CONFIG) --variable=INSTALL_CMOD lua-$(LUA_VERSION_MAJ_MIN) 2>/dev/null || echo "/usr/lib/lua/$(LUA_VERSION_MAJ_MIN)")


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
		$(DEPS_PREFIX)/lib/luarocks/rocks/busted/2.0.rc12-1/bin/busted test.lua

gdb: $(BUSTED) $(MPACK)
	eval $$($(LUAROCKS) path); \
	gdb -x .gdb --args $(LUA) \
		$(DEPS_PREFIX)/lib/luarocks/rocks/busted/2.0.rc12-1/bin/busted test.lua

ifeq ($(USE_SYSTEM_LUA),no)
$(MPACK): $(LUAROCKS) lmpack.c
	$(LUAROCKS) make CFLAGS='$(CFLAGS)'
else
$(MPACK): lmpack.c
	$(CC) -shared $(CFLAGS) $(INCLUDES) $(LDFLAGS) $^ -o $@ $(LIBS)
endif

$(BUSTED): $(LUAROCKS)
	$(LUAROCKS) install penlight 1.3.2-2
	$(LUAROCKS) install lua-term 0.7-1
	$(LUAROCKS) install dkjson 2.5-2
	$(LUAROCKS) install lua_cliargs 3.0-1
	$(LUAROCKS) install say 1.3-1
	$(LUAROCKS) install luafilesystem 1.6.3-2
	$(LUAROCKS) install luassert 1.7.10-0
	$(LUAROCKS) install mediator_lua 1.1.2-0
	$(LUAROCKS) install luasystem 0.2.0-0
	$(LUAROCKS) install busted 2.0.rc12-1
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

install: $(MPACK)
ifeq ($(USE_SYSTEM_LUA),no)
	@:
else
	mkdir -p "$(DESTDIR)$(LUA_CMOD_INSTALLDIR)"
	install -Dm755 $< "$(DESTDIR)$(LUA_CMOD_INSTALLDIR)/$<"
endif

.PHONY: all depsclean install test gdb valgrind
