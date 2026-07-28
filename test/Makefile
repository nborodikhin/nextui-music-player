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

TESTS    = $(BIN)/test_ring_log $(BIN)/test_watchdog $(BIN)/test_utc_time $(BIN)/test_log_trace \
           $(BIN)/test_crash_meta $(BIN)/test_crash_handler

# crash_handler.c is the one TU under test that genuinely needs SDL headers (for
# the BMP->PNG conversion, which is off the crash path). Everything else it
# pulls in — player/background/fb_capture/settings/watchdog — is satisfied by
# link-time stubs inside test_crash_handler.c.
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 SDL2_image 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 SDL2_image 2>/dev/null)

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

# log_trace.c compiles against the stub api.h (LOG_note is a no-op there) plus
# the real ring_log.c. SDL is no longer on the log path, so no SDL stub needed.
$(BIN)/test_log_trace: test_log_trace.c $(SRC)/log_trace.c $(SRC)/ring_log.c $(SRC)/log_trace.h test.h | $(BIN)
	$(CC) $(CFLAGS) $(INC) -o $@ test_log_trace.c $(SRC)/log_trace.c $(SRC)/ring_log.c $(LDLIBS)

# crash_meta.c is dependency-free by design (stdio + signal.h only), so it needs
# no stubs — that is exactly why format_meta() was split in two.
$(BIN)/test_crash_meta: test_crash_meta.c $(SRC)/crash_meta.c $(SRC)/crash_meta.h test.h | $(BIN)
	$(CC) $(CFLAGS) $(INC) -o $@ test_crash_meta.c $(SRC)/crash_meta.c $(LDLIBS)

# End-to-end bundle writer test. Forks a child that raises a real SIGSEGV, so it
# needs the genuine ring_log/log_trace/utc_time/crash_meta alongside crash_handler.
$(BIN)/test_crash_handler: test_crash_handler.c $(SRC)/crash_handler.c $(SRC)/ring_log.c \
                           $(SRC)/log_trace.c $(SRC)/utc_time.c $(SRC)/crash_meta.c \
                           $(SRC)/crash_handler.h test.h | $(BIN)
	$(CC) $(CFLAGS) $(INC) $(SDL_CFLAGS) -o $@ test_crash_handler.c \
	    $(SRC)/crash_handler.c $(SRC)/ring_log.c $(SRC)/log_trace.c \
	    $(SRC)/utc_time.c $(SRC)/crash_meta.c $(LDLIBS) $(SDL_LIBS)

run: $(TESTS)
	@fail=0; for t in $(TESTS); do echo "== $$t =="; ./$$t || fail=1; echo; done; \
	 if [ $$fail -ne 0 ]; then echo "TESTS FAILED"; exit 1; else echo "ALL TESTS PASSED"; fi

clean:
	rm -rf $(BIN)
