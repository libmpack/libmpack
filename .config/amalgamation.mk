include .config/release.mk
# unset LIB and define TEST_AMALGAMATION so the test harness will include the
# amalgamation instead of linking to the library.
LIB     :=
XCFLAGS += -DTEST_AMALGAMATION
