CFLAGS += -O2
CFLAGS += -I../../../../usr/include/
LDLIBS += -L../../../../usr/include/
LDLIBS += -lrt -lpthread -lpopt

all:
	$(CC) $(CFLAGS) mq_open_tests.c -o mq_open_tests $(LDLIBS)
	$(CC) $(CFLAGS) -o mq_perf_tests mq_perf_tests.c $(LDLIBS)

include ../lib.mk

override define RUN_TESTS
	@./mq_open_tests /test1 || echo "selftests: mq_open_tests [FAIL]"
	@./mq_perf_tests || echo "selftests: mq_perf_tests [FAIL]"
endef

TEST_PROGS := mq_open_tests mq_perf_tests

override define EMIT_TESTS
	echo "(./mq_open_tests /test1  && echo \"selftests: mq_open_tests [PASS]\") || echo \"selftests: mq_open_tests [FAIL]\""
	echo "(./mq_perf_tests  && echo \"selftests: mq_perf_tests [PASS]\") || echo \"selftests: mq_perf_tests [FAIL]\""
endef

clean:
	rm -f mq_open_tests mq_perf_tests
