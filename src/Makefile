###########################################################
# Supported platforms
#   tg5040 / tg5050 / my355 : ARM64 device builds (cross-compiled in the Docker
#                     toolchain)
#   desktop         : native Linux/macOS build for local dev/testing (no device,
#                     no cross-compiler). Uses SDL2 + desktop OpenGL instead of
#                     GLES, and has no AAC/M4A support (see fdk_aac_desktop_stub.c).
SUPPORTED_PLATFORMS = tg5040 tg5050 my355 desktop
DEFAULT_PLATFORM = tg5040

ifeq (,$(PLATFORM))
PLATFORM=$(UNION_PLATFORM)
endif
ifeq (,$(PLATFORM))
PLATFORM=$(DEFAULT_PLATFORM)
endif
ifeq (,$(filter $(PLATFORM),$(SUPPORTED_PLATFORMS)))
$(error Invalid PLATFORM '$(PLATFORM)'. Supported: $(SUPPORTED_PLATFORMS))
endif

# The desktop target compiles natively, so CROSS_COMPILE is expected to be empty
# (CC falls back to the host gcc). Device targets must have a cross toolchain.
ifneq ($(PLATFORM), desktop)
ifeq (,$(CROSS_COMPILE))
$(error Missing CROSS_COMPILE for this toolchain)
endif
endif

###########################################################

TARGET   = musicplayer
PRODUCT  = ../bin/$(PLATFORM)/$(TARGET).elf

BUILD_DIR        = build/$(PLATFORM)
OBJ_DIR          = $(BUILD_DIR)/obj
OPUS_OBJ_DIR     = $(BUILD_DIR)/opus_obj
MBEDTLS_OBJ_DIR  = $(BUILD_DIR)/mbedtls_obj
LIBOPUS          = $(BUILD_DIR)/libopus.a
LIBMBEDTLS       = $(BUILD_DIR)/libmbedtls.a

CC = $(CROSS_COMPILE)gcc

# macOS's plain `ar` is the Apple/LLVM one, which can't be relied on to match
# the GCC toolchain we build with there; use gcc-ar instead. Device/Linux
# builds use the cross/system `ar` as before.
ifeq ($(PLATFORM), desktop)
ifeq ($(shell uname -s), Darwin)
AR = gcc-ar
else
AR = $(CROSS_COMPILE)ar
endif
else
AR = $(CROSS_COMPILE)ar
endif

###########################################################
# NextUI source tree. `./dev deps fetch` checks it out into <project>/NextUI; the
# container build mounts it at the same relative path. Override to build
# against a tree that lives somewhere else.

NEXTUI_ROOT ?= ../NextUI/workspace

# `clean` is exempt so a tree can always be scrubbed without its dependencies.
ifeq (,$(filter clean,$(MAKECMDGOALS)))
ifeq (,$(wildcard $(NEXTUI_ROOT)/all/common/api.h))
$(error No NextUI tree at $(NEXTUI_ROOT) - run ./dev deps fetch, or set NEXTUI_ROOT)
endif
endif

###########################################################
# Include paths

INCDIR  = -I. -I./audio
INCDIR += -I$(NEXTUI_ROOT)/all/common/ -I$(NEXTUI_ROOT)/$(PLATFORM)/platform/
INCDIR += -I$(NEXTUI_ROOT)/all/minarch/libretro-common/include
INCDIR += -I./include -I./include/mbedtls_lib -I./include/yxml
INCDIR += -I./include/libogg/include -I./include/libopus/include
INCDIR += -I./include/opusfile/include
INCDIR += -I$(PREFIX)/include -I$(PREFIX_LOCAL)/include
INCDIR += -I$(NEXTUI_ROOT)/$(PLATFORM)/libmsettings

###########################################################
# Opus / OGG / Opusfile — built as libopus.a

LIBOGG_SRC = \
    include/libogg/src/bitwise.c \
    include/libogg/src/framing.c

LIBOPUS_SRC = $(filter-out \
    %/opus_custom_demo.c %/opus_demo.c %/opus_compare.c \
    %/qext_compare.c %/repacketizer_demo.c %/mini_kfft.c, \
    $(wildcard include/libopus/src/*.c) \
    $(wildcard include/libopus/celt/*.c) \
    $(wildcard include/libopus/silk/*.c) \
    $(wildcard include/libopus/silk/float/*.c))

OPUSFILE_SRC = \
    include/opusfile/src/info.c \
    include/opusfile/src/internal.c \
    include/opusfile/src/opusfile.c \
    include/opusfile/src/stream.c

OPUS_ALL_SRC = $(LIBOGG_SRC) $(LIBOPUS_SRC) $(OPUSFILE_SRC)

OPUS_INCDIR = \
    -I./include/libogg/include \
    -I./include/libopus/include \
    -I./include/opusfile/include \
    -I./include/libopus \
    -I./include/libopus/src \
    -I./include/libopus/celt \
    -I./include/libopus/silk \
    -I./include/libopus/silk/float

OPUS_CFLAGS = \
    -O2 -fomit-frame-pointer \
    -DOPUS_BUILD -DVAR_ARRAYS -DHAVE_LRINTF \
    -DOP_DISABLE_HTTP -DOP_DISABLE_FLOAT_API \
    -std=gnu99 -MMD -MP

OPUS_OBJS = $(patsubst %.c,$(OPUS_OBJ_DIR)/%.o,$(OPUS_ALL_SRC))

###########################################################
# mbedTLS — built as libmbedtls.a

MBEDTLS_SRC = \
    include/mbedtls_entropy_alt.c \
    $(wildcard include/mbedtls_lib/*.c)

MBEDTLS_CFLAGS = \
    -O2 -fomit-frame-pointer \
    -DMBEDTLS_CONFIG_FILE='<mbedtls_config.h>' \
    -std=gnu99 -MMD -MP \
    $(INCDIR)

MBEDTLS_OBJS = $(patsubst %.c,$(MBEDTLS_OBJ_DIR)/%.o,$(MBEDTLS_SRC))

###########################################################
# Main application sources

LOCAL_SRC = \
    musicplayer.c player.c playlist.c playlist_m3u.c \
    radio.c radio_net.c album_art.c lyrics.c radio_hls.c radio_curated.c \
    downloader.c selfupdate.c display_helper.c \
    podcast.c podcast_rss.c podcast_search.c \
    http_download.c wget_fetch.c file_utils.c wifi.c keyboard.c settings.c resume.c \
    add_to_playlist.c background.c \
    module_common.c module_menu.c module_library.c module_player.c \
    module_playlist.c module_radio.c module_podcast.c module_downloader.c \
    module_settings.c \
    list_nav.c list_nav_pad.c menu_rows.c toast.c toast_state.c \
    ui_fonts.c ui_icons.c ui_utils.c browser.c ui_album_art.c \
    ui_main.c ui_music.c ui_radio.c ui_downloader.c ui_podcast.c \
    ui_playlist.c ui_system.c ui_settings.c ui_keyboard.c \
    keyboard_map.c utf8.c \
    spectrum.c \
    audio/kiss_fft.c audio/kiss_fftr.c \
    include/yxml/yxml.c \
    include/parson/parson.c

# Desktop has no libfdk-aac; link a no-op stub so player.c (which calls the FDK
# API unconditionally) links, with AAC/M4A decode failing gracefully at runtime.
ifeq ($(PLATFORM), desktop)
LOCAL_SRC += fdk_aac_desktop_stub.c
endif

# Sources that live outside src/ (shared NextUI code + per-platform code).
# These need their own rule below: a plain $(OBJ_DIR)/%.o: %.c pattern would
# reuse the same object/dep file across platforms, since the leading ../ in the
# source path collapses right back out of $(OBJ_DIR) (e.g. both
# build/tg5040/obj/../NextUI/workspace/all/common/api.o and its tg5050 twin
# resolve to the same path). That caused stale objects compiled for one
# platform's headers to get linked into the other platform's binary.
EXT_SRC = \
    $(NEXTUI_ROOT)/all/common/utils.c $(NEXTUI_ROOT)/all/common/api.c \
    $(NEXTUI_ROOT)/all/common/config.c $(NEXTUI_ROOT)/all/common/scaler.c \
    $(NEXTUI_ROOT)/$(PLATFORM)/platform/platform.c

APP_SRC = $(LOCAL_SRC) $(EXT_SRC)

LOCAL_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(LOCAL_SRC))
EXT_OBJS   = $(patsubst $(NEXTUI_ROOT)/%.c,$(OBJ_DIR)/ext/%.o,$(EXT_SRC))
APP_OBJS = $(LOCAL_OBJS) $(EXT_OBJS)

###########################################################
# Compiler / linker flags

MY_CFLAGS  = -O2 -fomit-frame-pointer
MY_CFLAGS += -DMBEDTLS_CONFIG_FILE='<mbedtls_config.h>'
MY_CFLAGS += -DOPUS_BUILD -DHAVE_LRINTF -DOP_DISABLE_HTTP -DOP_DISABLE_FLOAT_API
MY_CFLAGS += $(INCDIR) -DPLATFORM=\"$(PLATFORM)\" -std=gnu99
MY_CFLAGS += -DUSE_SDL2 -DGL_GLEXT_PROTOTYPES
MY_CFLAGS += -MMD -MP
MY_CFLAGS += -Werror=switch

# Desktop uses full OpenGL; devices use GLES. Neither macro is read by this
# repo's sources today (the GL context profile is picked at runtime via
# PLAT_getModel()), but keep them matching the platform's makefile.env.
ifeq ($(PLATFORM), desktop)
MY_CFLAGS += -DUSE_GL
else
MY_CFLAGS += -DUSE_GLES
endif

MY_LDFLAGS  = -L$(BUILD_DIR)
MY_LDFLAGS += -L$(PREFIX)/lib -L$(PREFIX_LOCAL)/lib
MY_LDFLAGS += -L$(PREFIX)/lib/$(CROSS_TRIPLE)
MY_LDFLAGS += -L$(NEXTUI_ROOT)/$(PLATFORM)/libmsettings
MY_LDFLAGS += -L$(NEXTUI_ROOT)/all/minarch/build/$(PLATFORM)

LDLIBS  = -lopus -lmbedtls
LDLIBS += -lSDL2_image -lSDL2_ttf
LDLIBS += -lmsettings -lsamplerate -lzip -lm -lpthread -ldl -lz

ifeq ($(PLATFORM), desktop)
# Native desktop build: full OpenGL, and SDL2 provides audio output so there is
# no ALSA link. libfdk-aac is not available on desktop (not in Debian), so AAC/
# M4A decoding is stubbed out — see fdk_aac_desktop_stub.c, linked via LOCAL_SRC.
# GL links differently per host: macOS OpenGL is a framework, Linux is -lGL (no
# gl.pc, so pkg-config falls back). Mirrors desktop/platform/makefile.env.
ifeq ($(shell uname -s), Darwin)
LDLIBS += -lSDL2 -framework OpenGL
else
LDLIBS += $(shell pkg-config --libs sdl2 gl 2>/dev/null || echo "-lSDL2 -lGL")
endif
else
LDLIBS += $(shell pkg-config --libs sdl2 glesv2 2>/dev/null || echo "-lSDL2 -lGLESv2")
LDLIBS += -lfdk-aac
# The audio libraries a device pulls in are whatever its toolchain sysroot
# carries: the TrimUI targets have ALSA (tg5050 additionally needs tinyalsa),
# while my355 has tinyalsa only.
ifeq ($(PLATFORM), my355)
LDLIBS += -ltinyalsa
else
LDLIBS += -lasound
ifeq ($(PLATFORM), tg5050)
LDLIBS += -ltinyalsa
endif
endif
endif

###########################################################

.PHONY: all clean

all: $(PRODUCT)

$(PRODUCT): $(APP_OBJS) $(LIBOPUS) $(LIBMBEDTLS)
	@mkdir -p $(@D)
	$(CC) $(APP_OBJS) $(MY_LDFLAGS) -o $@ $(LDLIBS)

$(LIBOPUS): $(OPUS_OBJS)
	$(AR) rcs $@ $^

$(LIBMBEDTLS): $(MBEDTLS_OBJS)
	$(AR) rcs $@ $^

# Compile main app sources
$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) -c $(MY_CFLAGS) -o $@ $<

# Compile sources that live outside src/ (see EXT_SRC above) into their own
# per-platform subdirectory instead of $(OBJ_DIR)/%.o, so they can't collapse
# to a path shared with another platform's build.
$(OBJ_DIR)/ext/%.o: $(NEXTUI_ROOT)/%.c
	@mkdir -p $(@D)
	$(CC) -c $(MY_CFLAGS) -o $@ $<

# NextUI's config.c rewrites the global theme file as a side effect of reading
# it (CFG_init -> setters -> CFG_sync). This pak must never write global config,
# so force-include a shim that makes config.c a read-only consumer by failing
# write-mode fopen() inside that one translation unit. See config_readonly_shim.h.
$(OBJ_DIR)/ext/all/common/config.o: MY_CFLAGS += -include $(CURDIR)/config_readonly_shim.h

# Compile Opus / OGG / Opusfile sources
$(OPUS_OBJ_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) -c $(OPUS_CFLAGS) $(OPUS_INCDIR) -o $@ $<

# Compile mbedTLS sources
$(MBEDTLS_OBJ_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) -c $(MBEDTLS_CFLAGS) -o $@ $<

clean:
	rm -f $(PRODUCT)
	rm -rf $(BUILD_DIR)
	rm -rf opus_obj

# Auto-generated header dependency files
-include $(APP_OBJS:.o=.d)
-include $(OPUS_OBJS:.o=.d)
-include $(MBEDTLS_OBJS:.o=.d)
