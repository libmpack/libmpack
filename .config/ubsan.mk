XCFLAGS  += -fno-omit-frame-pointer -fno-optimize-sibling-calls \
					-fno-sanitize-recover -fsanitize=undefined
XLDFLAGS += -fsanitize=undefined
export UBSAN_OPTIONS := log_path=ubsan
export UBSAN_SYMBOLIZER_PATH := $(SYMBOLIZER)
