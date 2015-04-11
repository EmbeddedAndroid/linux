.PHONY: all all_32 all_64 check_build32 clean run_tests

TARGETS_C_BOTHBITS := sigreturn

BINARIES_32 := $(TARGETS_C_BOTHBITS:%=%_32)
BINARIES_64 := $(TARGETS_C_BOTHBITS:%=%_64)

CFLAGS := -O2 -g -std=gnu99 -pthread -Wall

# Taken from perf makefile
uname_M := $(shell uname -m 2>/dev/null || echo not)
ARCH ?= $(shell echo $(uname_M) | sed -e s/i.86/i386/)
ifeq ($(ARCH),i386)
ARCH := x86
all: all_32
endif
ifeq ($(ARCH),x86_64)
ARCH := x86
# If we're on a 64-bit host, build 64-bit tests as well
all: all_32 all_64
endif

all_32: check_build32 $(BINARIES_32)

all_64: $(BINARIES_64)

include ../lib.mk

TEST_PROGS := $(BINARIES_32) $(BINARIES_64) run_x86_tests.sh

$(TARGETS_C_BOTHBITS:%=%_32): %_32: %.c
ifeq ($(ARCH),x86)
	$(CC) -m32 -o $@ $(CFLAGS) $(EXTRA_CFLAGS) $^ -lrt -ldl
else
	echo "Not an x86 target, can't build x86 tests"
endif

$(TARGETS_C_BOTHBITS:%=%_64): %_64: %.c
ifeq ($(ARCH),x86)
	$(CC) -m64 -o $@ $(CFLAGS) $(EXTRA_CFLAGS) $^ -lrt -ldl
else
	echo "Not an x86 target, can't build x86 tests"
endif

check_build32:
	@if ! $(CC) -m32 -o /dev/null trivial_32bit_program.c; then	\
	  echo "Warning: you seem to have a broken 32-bit build" 2>&1; 	\
	  echo "environment.  If you are using a Debian-like";		\
	  echo " distribution, try:"; 					\
	  echo "";							\
	  echo "  apt-get install gcc-multilib libc6-i386 libc6-dev-i386"; \
	  echo "";							\
	  echo "If you are using a Fedora-like distribution, try:";	\
	  echo "";							\
	  echo "  yum install glibc-devel.*i686";			\
	  exit 1;							\
	fi

install:
ifneq ($(ARCH),x86)
echo "Not an x86 target, can't install x86 tests"
override EMIT_TESTS :=  echo "echo \"selftests: run_x86_tests.sh [SKIP]\""
endif

run_tests:
	./run_x86_tests.sh

clean:
	$(RM) $(BINARIES_32) $(BINARIES_64)
