unexport CC
LINARO_V := 6.2.1-2016.11
LINARO_V_NOPATCH := 6.2-2016.11
LINARO_URL := https://releases.linaro.org/components/toolchain/binaries/$(LINARO_V_NOPATCH)/armeb-linux-gnueabihf/gcc-linaro-$(LINARO_V)-x86_64_armeb-linux-gnueabihf.tar.xz
QEMU_URL   := https://github.com/qemu/qemu/archive/stable-2.4.tar.gz
DEPS       := $(shell pwd)/.deps
DPREFIX    := $(DEPS)/usr
RUNNER     := $(DPREFIX)/bin/qemu-armeb
TPREFIX    := $(DPREFIX)/bin/armeb-linux-gnueabihf-
C_COMPILER := $(TPREFIX)gcc
CC         := $(C_COMPILER)
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
		CFLAGS= LDFLAGS= \
		./configure --prefix=$(DPREFIX) \
		--target-list=armeb-linux-user && \
		make install
	@rm -rf $(DEPS)/src

$(C_COMPILER):
	mkdir -p $(DPREFIX)
	@echo installing linaro toolchain...
	@echo fetch $(LINARO_URL)
	@$(FETCH) $(LINARO_URL) | tar xJf - --strip-components=1 -C $(DPREFIX)
