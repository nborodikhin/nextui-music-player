# Host-side unit tests for pure-logic modules (no SDL/hardware).
# Run from the repo root with:  make -C test        (build + run)
#                               make -C test clean
#
# Tests compile the REAL source under test against tiny stub headers in
# test/stubs/ (so heavy headers like api.h -> sdl.h don't get pulled in on the
# host). -Istubs supplies defines.h/api.h, which don't exist under src/; the
# genuine ring_log.h / watchdog.h / log_trace.h come from -I../src (a quoted
# include resolves to the including file's own dir first). watchdog.c's lone
# external symbol, LogTrace_trace, is stubbed inside test_watchdog.c.

CC      ?= cc
CFLAGS  ?= -std=gnu99 -O0 -g -Wall -Wextra
INC      = -Istubs -I../src
LDLIBS   = -lpthread

SRC      = ../src
BIN      = bin

TESTS    = $(BIN)/test_ring_log $(BIN)/test_watchdog $(BIN)/test_utc_time

.PHONY: all run clean
all: run

$(BIN):
	mkdir -p $(BIN)

# ring_log.c is compiled with -Dwrite=rl_test_write so the test can intercept
# write() and force short writes deterministically (see test_ring_log.c). The
# override applies only to the ring_log.c compile, not the test driver.
$(BIN)/test_ring_log: test_ring_log.c $(SRC)/ring_log.c test.h | $(BIN)
	$(CC) $(CFLAGS) $(INC) -Dwrite=rl_test_write -o $@ test_ring_log.c $(SRC)/ring_log.c $(LDLIBS)

$(BIN)/test_watchdog: test_watchdog.c $(SRC)/watchdog.c $(SRC)/watchdog.h test.h | $(BIN)
	$(CC) $(CFLAGS) $(INC) -o $@ test_watchdog.c $(SRC)/watchdog.c $(LDLIBS)

$(BIN)/test_utc_time: test_utc_time.c $(SRC)/utc_time.c $(SRC)/utc_time.h test.h | $(BIN)
	$(CC) $(CFLAGS) $(INC) -o $@ test_utc_time.c $(SRC)/utc_time.c $(LDLIBS)

run: $(TESTS)
	@fail=0; for t in $(TESTS); do echo "== $$t =="; ./$$t || fail=1; echo; done; \
	 if [ $$fail -ne 0 ]; then echo "TESTS FAILED"; exit 1; else echo "ALL TESTS PASSED"; fi

clean:
	rm -rf $(BIN)
