# Host-side unit tests for pure-logic modules (no SDL/hardware).
# Run from the repo root with:  make -C test        (build + run)
#                               make -C test clean
#
# Tests compile the REAL source under test with the host cc. A module only
# qualifies for a test here if its translation unit has no platform includes -
# that constraint is what keeps this harness stub-free and sub-second.
#
# ui_theme_utils.c is the one exception: it includes SDL for the SDL_Color type
# only, and calls no SDL function, thus the test needs the SDL headers but no
# SDL library. A color type of its own would need no header, but two color
# structs in one codebase cost a reader more than one include costs the build.
#
# list_nav.c is the model: pure integer cursor logic, with the pad read split
# out into list_nav_pad.c so api.h never reaches the tested TU. Its declaration
# remains in list_nav.h because ListNavInput is part of that public API. toast_state.c
# follows it, with the drawing left in toast.c and the clock passed in.
#
# keyboard_map.c qualifies the same way: the key tables and the pair lookups are
# plain C, with the font probe passed in as a callback so SDL_ttf stays in
# ui_keyboard.c.
#
# spectrum_ceiling.c is the same shape as list_nav.c: the rise of the ceiling is
# float arithmetic, with the FFT and the paint left in spectrum.c. It declares
# itself in spectrum.h, which holds no platform include.
#
# screen_title.c is the same shape as toast_state.c: the text of a title and
# where its marquee is, with the clock and the measured widths passed in and the
# paint left in ui_utils.c.
#
# podcast_episode_layout.c is integer geometry, as list_nav.c is: the summary
# height, the viewport and the rows come in, and the scroll comes out. Its test
# links list_nav.c for the wrap of the cursor.
#
# frame_state.c is the record of what a frame changed: two flags in, and the
# presentation that the frame needs out. The calls of the platform stay in
# module_common.c.
#
# lyric_window.c is the timing and the layout of the lyric window: the lines and
# the position come in, and the current index and the rows come out. It reads
# the line type from lyrics.h, which holds no platform include.
#
# file_utils.c qualifies for a different reason: it talks to the filesystem, but
# only through libc, so it needs no stubs either. Its test does create files -
# under its own mk_tempdir(), and removes them again.

CC      ?= cc
CFLAGS  ?= -std=gnu99 -O0 -g -Wall -Wextra
INC      = -I../src $(shell pkg-config --cflags sdl2)

SRC      = ../src
BIN      = bin

TESTS    = $(BIN)/test_list_nav $(BIN)/test_menu_rows $(BIN)/test_toast_state \
           $(BIN)/test_file_utils $(BIN)/test_keyboard_map $(BIN)/test_ui_theme \
           $(BIN)/test_utf8 $(BIN)/test_spectrum_ceiling $(BIN)/test_screen_title \
           $(BIN)/test_podcast_episode_layout $(BIN)/test_frame_state \
           $(BIN)/test_lyric_window $(BIN)/test_db $(BIN)/test_settings

# ui_utils.c draws, thus it includes the platform and this target cannot follow
# the rule above. The test compiles ui_utils.c itself, thus it always runs the
# source of the tree, and it takes the rest of the symbols from the objects that
# the desktop build already made. Run ./dev build desktop first.
# The desktop build makes one kind at a time and keeps each kind apart. The tests
# link the kind that ./dev build makes by default. Pass KIND=release to link the
# other one.
KIND ?= debug
DESKTOP_BUILD = ../src/build/$(KIND)/desktop
DESKTOP_OBJ = $(DESKTOP_BUILD)/obj
TESTS   += $(BIN)/test_ui_utils

# ui_fonts.c reads the theme of the platform, thus its test links the objects of
# the desktop build as the test above does. It links ui_fonts.o in place of a
# copy of its own, because `desktop-objs` rebuilds that object from the source of
# the tree.
TESTS   += $(BIN)/test_ui_fonts

# ui_music_playing.c draws with SDL_ttf, thus its test links the objects of the
# desktop build as the two above do.
TESTS   += $(BIN)/test_ui_music_playing

.PHONY: all build check run clean
all: run

# Compile every test binary and run none. An IDE indexes the tests through it.
build: $(TESTS)

check:
	./check_color_calls.py
	./check_present_calls.py

$(BIN):
	mkdir -p $(BIN)

$(BIN)/test_list_nav: test_list_nav.c $(SRC)/list_nav.c $(SRC)/list_nav.h test.h | $(BIN)
	$(CC) $(CFLAGS) $(INC) -o $@ test_list_nav.c $(SRC)/list_nav.c

$(BIN)/test_menu_rows: test_menu_rows.c $(SRC)/menu_rows.c $(SRC)/menu_rows.h test.h | $(BIN)
	$(CC) $(CFLAGS) $(INC) -o $@ test_menu_rows.c $(SRC)/menu_rows.c

$(BIN)/test_toast_state: test_toast_state.c $(SRC)/toast_state.c $(SRC)/toast_state.h $(SRC)/toast_types.h test.h | $(BIN)
	$(CC) $(CFLAGS) $(INC) -o $@ test_toast_state.c $(SRC)/toast_state.c

$(BIN)/test_screen_title: test_screen_title.c $(SRC)/screen_title.c $(SRC)/screen_title.h test.h | $(BIN)
	$(CC) $(CFLAGS) $(INC) -o $@ test_screen_title.c $(SRC)/screen_title.c

$(BIN)/test_podcast_episode_layout: test_podcast_episode_layout.c $(SRC)/podcast_episode_layout.c \
                                    $(SRC)/podcast_episode_layout.h $(SRC)/list_nav.c test.h | $(BIN)
	$(CC) $(CFLAGS) $(INC) -o $@ test_podcast_episode_layout.c $(SRC)/podcast_episode_layout.c $(SRC)/list_nav.c

$(BIN)/test_frame_state: test_frame_state.c $(SRC)/frame_state.c $(SRC)/frame_state.h test.h | $(BIN)
	$(CC) $(CFLAGS) $(INC) -o $@ test_frame_state.c $(SRC)/frame_state.c

$(BIN)/test_lyric_window: test_lyric_window.c $(SRC)/lyric_window.c $(SRC)/lyric_window.h \
                          $(SRC)/lyrics.h test.h | $(BIN)
	$(CC) $(CFLAGS) $(INC) -o $@ test_lyric_window.c $(SRC)/lyric_window.c

$(BIN)/test_keyboard_map: test_keyboard_map.c $(SRC)/keyboard_map.c $(SRC)/keyboard_map.h $(SRC)/utf8.c test.h | $(BIN)
	$(CC) $(CFLAGS) $(INC) -o $@ test_keyboard_map.c $(SRC)/keyboard_map.c $(SRC)/utf8.c

$(BIN)/test_ui_theme: test_ui_theme.c $(SRC)/ui_theme_utils.c $(SRC)/ui_theme.h test.h | $(BIN)
	$(CC) $(CFLAGS) $(INC) -o $@ test_ui_theme.c $(SRC)/ui_theme_utils.c -lm

$(BIN)/test_utf8: test_utf8.c $(SRC)/utf8.c $(SRC)/utf8.h test.h | $(BIN)
	$(CC) $(CFLAGS) $(INC) -o $@ test_utf8.c $(SRC)/utf8.c

$(BIN)/test_spectrum_ceiling: test_spectrum_ceiling.c $(SRC)/spectrum_ceiling.c \
                              $(SRC)/spectrum.h test.h | $(BIN)
	$(CC) $(CFLAGS) $(INC) -o $@ test_spectrum_ceiling.c $(SRC)/spectrum_ceiling.c -lm

DB_INC = $(INC) -I../NextUI/workspace/all/common -I../NextUI/workspace/desktop/platform \
         -I../NextUI/workspace/desktop/libmsettings -DPLATFORM='"desktop"' -DUSE_SDL2
TEST_TEMP_DIR ?= $(abspath ../.test_tmp/manual)

$(BIN)/test_db: test_db.c $(SRC)/db.c $(SRC)/db.h $(SRC)/db_schema.c $(SRC)/db_schema.h \
                $(SRC)/db_statement.c $(SRC)/db_statement.h \
                $(SRC)/file_utils.c $(SRC)/file_utils.h test.h | $(BIN)
	mkdir -p "$(TEST_TEMP_DIR)"
	$(CC) $(CFLAGS) $(DB_INC) \
	  -DFILE_UTILS_USERDATA_DIR='"$(TEST_TEMP_DIR)/db-userdata"' \
	  -DFILE_UTILS_TEMP_DIR='"$(TEST_TEMP_DIR)"' \
	  -o $@ test_db.c $(SRC)/db.c $(SRC)/db_schema.c $(SRC)/db_statement.c $(SRC)/file_utils.c \
	  -lsqlite3 -lpthread -lzip

$(BIN)/test_settings: test_settings.c $(SRC)/settings.c $(SRC)/settings.h \
                      $(SRC)/db.c $(SRC)/db.h $(SRC)/db_schema.c $(SRC)/db_schema.h \
                      $(SRC)/db_statement.c $(SRC)/db_statement.h \
                      $(SRC)/file_utils.c $(SRC)/file_utils.h test.h | $(BIN)
	mkdir -p "$(TEST_TEMP_DIR)"
	$(CC) $(CFLAGS) $(DB_INC) \
	  -DFILE_UTILS_USERDATA_DIR='"$(TEST_TEMP_DIR)/settings-userdata"' \
	  -DFILE_UTILS_TEMP_DIR='"$(TEST_TEMP_DIR)"' \
	  -o $@ test_settings.c $(SRC)/settings.c $(SRC)/db.c $(SRC)/db_schema.c \
	  $(SRC)/db_statement.c $(SRC)/file_utils.c -lsqlite3 -lpthread -lzip

DESKTOP_RPATH = -Wl,-rpath,$(abspath ../NextUI/workspace/desktop/libmsettings) \
                -Wl,-rpath,/var/tmp/nextui/lib
DESKTOP_LIBS = $(DESKTOP_RPATH) -L$(DESKTOP_BUILD) -L/var/tmp/nextui/lib \
               -L../NextUI/workspace/desktop/libmsettings \
               $(shell pkg-config --libs sdl2) -lSDL2_ttf -lSDL2_image -lsamplerate \
               -lzip -lsqlite3 -lopus -lmbedtls -lm -lpthread -ldl -lz -lGL -lmsettings
DESKTOP_INC = -I../NextUI/workspace/all/common -I../NextUI/workspace/desktop/platform \
              -I../NextUI/workspace/desktop/libmsettings -DPLATFORM='"desktop"' \
              -DUSE_SDL2 -DUSE_GL
# Every object of the desktop build except the two that this test replaces: its
# own main, and ui_utils.o, which the test compiles from the source instead.
DESKTOP_LINK_OBJ = $$(ls $(DESKTOP_OBJ)/*.o $(DESKTOP_OBJ)/audio/*.o \
                        $(DESKTOP_OBJ)/include/*/*.o $(DESKTOP_OBJ)/ext/*/*/*.o \
                      | grep -v -e musicplayer.o -e ui_utils.o)
# The objects come from the desktop build, thus they must be as new as the sources.
# src/Makefile tracks each header with -MMD and -include, thus this delegation
# rebuilds what changed and nothing else.
.PHONY: desktop-objs
desktop-objs:
	@test -d $(DESKTOP_OBJ) || \
	  { echo "test_ui_utils needs the $(KIND) desktop build."; \
	    echo "Run: ./dev build desktop --$(KIND)"; exit 1; }
	@$(MAKE) --no-print-directory -C ../src objs PLATFORM=desktop KIND=$(KIND) \
	  NEXTUI_ROOT=$(abspath ../NextUI/workspace)

$(BIN)/test_ui_utils: test_ui_utils.c $(SRC)/ui_utils.c $(SRC)/ui_utils.h test.h | $(BIN) desktop-objs
	$(CC) $(CFLAGS) $(INC) $(DESKTOP_INC) -o $@ test_ui_utils.c $(SRC)/ui_utils.c \
	  $(DESKTOP_LINK_OBJ) $(DESKTOP_LIBS)

$(BIN)/test_ui_fonts: test_ui_fonts.c $(SRC)/ui_fonts.h test.h | $(BIN) desktop-objs
	$(CC) $(CFLAGS) $(INC) $(DESKTOP_INC) -o $@ test_ui_fonts.c \
	  $$(ls $(DESKTOP_OBJ)/*.o $(DESKTOP_OBJ)/audio/*.o \
	        $(DESKTOP_OBJ)/include/*/*.o $(DESKTOP_OBJ)/ext/*/*/*.o \
	      | grep -v -e musicplayer.o) $(DESKTOP_LIBS)

$(BIN)/test_ui_music_playing: test_ui_music_playing.c $(SRC)/ui_music_playing.h test.h | $(BIN) desktop-objs
	$(CC) $(CFLAGS) $(INC) $(DESKTOP_INC) -o $@ test_ui_music_playing.c \
	  $$(ls $(DESKTOP_OBJ)/*.o $(DESKTOP_OBJ)/audio/*.o \
	        $(DESKTOP_OBJ)/include/*/*.o $(DESKTOP_OBJ)/ext/*/*/*.o \
	      | grep -v -e musicplayer.o) $(DESKTOP_LIBS)

$(BIN)/test_file_utils: test_file_utils.c $(SRC)/file_utils.c $(SRC)/file_utils.h test.h | $(BIN)
	mkdir -p "$(TEST_TEMP_DIR)"
	$(CC) $(CFLAGS) $(INC) \
	  -DFILE_UTILS_USERDATA_DIR='"$(TEST_TEMP_DIR)/file-utils-userdata"' \
	  -DFILE_UTILS_TEMP_DIR='"$(TEST_TEMP_DIR)"' \
	  -o $@ test_file_utils.c $(SRC)/file_utils.c -lzip

run: check $(TESTS)
	@fail=0; for t in $(TESTS); do echo "== $$t =="; ./$$t || fail=1; echo; done; \
	 if [ $$fail -ne 0 ]; then echo "TESTS FAILED"; exit 1; else echo "ALL TESTS PASSED"; fi

clean:
	rm -rf $(BIN)
