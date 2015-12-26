include .config/release.mk
XCFLAGS  += -g3
XCFLAGS  += -fno-omit-frame-pointer -fno-optimize-sibling-calls \
            -fsanitize=memory -fsanitize-memory-track-origins
XLDFLAGS += -fsanitize=memory
export MSAN_OPTIONS := log_path=msan
export MSAN_SYMBOLIZER_PATH := $(SYMBOLIZER)
