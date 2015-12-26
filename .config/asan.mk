include .config/release.mk
XCFLAGS  += -g3
XCFLAGS  += -fno-omit-frame-pointer -fno-optimize-sibling-calls -fsanitize=address
XLDFLAGS += -fsanitize=address
export ASAN_OPTIONS := log_path=asan:detect_leaks=1
export ASAN_SYMBOLIZER_PATH := $(SYMBOLIZER)
