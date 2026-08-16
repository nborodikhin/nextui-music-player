# Host-side unit tests for pure-logic modules (no SDL/hardware).
# Run from the repo root with:  make -C test        (build + run)
#                               make -C test clean
#
# Tests compile the REAL source under test with the host cc. A module only
# qualifies for a test here if its translation unit has no platform includes -
# that constraint is what keeps this harness stub-free and sub-second.
#
# list_nav.c is the model: pure integer cursor logic, with the pad read split
# out into list_nav_pad.c so api.h never reaches the tested TU. toast_state.c
# follows it, with the drawing left in toast.c and the clock passed in.

CC      ?= cc
CFLAGS  ?= -std=gnu99 -O0 -g -Wall -Wextra
INC      = -I../src

SRC      = ../src
BIN      = bin

TESTS    = $(BIN)/test_list_nav $(BIN)/test_menu_rows $(BIN)/test_toast_state

.PHONY: all run clean
all: run

$(BIN):
	mkdir -p $(BIN)

$(BIN)/test_list_nav: test_list_nav.c $(SRC)/list_nav.c $(SRC)/list_nav.h test.h | $(BIN)
	$(CC) $(CFLAGS) $(INC) -o $@ test_list_nav.c $(SRC)/list_nav.c

$(BIN)/test_menu_rows: test_menu_rows.c $(SRC)/menu_rows.c $(SRC)/menu_rows.h test.h | $(BIN)
	$(CC) $(CFLAGS) $(INC) -o $@ test_menu_rows.c $(SRC)/menu_rows.c

$(BIN)/test_toast_state: test_toast_state.c $(SRC)/toast_state.c $(SRC)/toast_state.h test.h | $(BIN)
	$(CC) $(CFLAGS) $(INC) -o $@ test_toast_state.c $(SRC)/toast_state.c

run: $(TESTS)
	@fail=0; for t in $(TESTS); do echo "== $$t =="; ./$$t || fail=1; echo; done; \
	 if [ $$fail -ne 0 ]; then echo "TESTS FAILED"; exit 1; else echo "ALL TESTS PASSED"; fi

clean:
	rm -rf $(BIN)
