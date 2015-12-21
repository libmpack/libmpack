unexport CC
LINARO_URL := https://releases.linaro.org/15.06/components/toolchain/binaries/4.8/armeb-linux-gnueabihf/gcc-linaro-4.8-2015.06-x86_64_armeb-linux-gnueabihf.tar.xz
QEMU_URL   := https://github.com/qemu/qemu/archive/stable-2.4.tar.gz
DEPS       := $(shell pwd)/.deps
DPREFIX    := $(DEPS)/usr
RUNNER     := $(DPREFIX)/bin/qemu-armeb
TPREFIX    := $(DPREFIX)/bin/armeb-linux-gnueabihf-
COMPILER   := $(TPREFIX)gcc
CC         := $(COMPILER)
AR         := $(TPREFIX)ar
RANLIB     := $(TPREFIX)ranlib
LINK       := $(CC)
XLDFLAGS   += -all-static
FETCH      ?= curl -L -o -

$(RUNNER):
	@echo installing qemu...
	@rm -rf $(DEPS)/src/qemu
	@mkdir -p $(DEPS)/src/qemu
	@$(FETCH) $(QEMU_URL) | tar xfz - --strip-components=1 -C $(DEPS)/src/qemu
	@cd $(DEPS)/src/qemu && \
		./configure --prefix=$(DPREFIX) --target-list=armeb-linux-user && \
		make install
	@rm -rf $(DEPS)/src

$(COMPILER):
	mkdir -p $(DPREFIX)
	@echo installing linaro toolchain...
	@$(FETCH) $(LINARO_URL) | tar xJf - --strip-components=1 -C $(DPREFIX)
