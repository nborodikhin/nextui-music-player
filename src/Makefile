###########################################################
# Supported platforms
SUPPORTED_PLATFORMS = tg5040 tg5050
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
ifeq (,$(CROSS_COMPILE))
$(error Missing CROSS_COMPILE for this toolchain)
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
AR = $(CROSS_COMPILE)ar

###########################################################
# Include paths

INCDIR  = -I. -I./audio
INCDIR += -I../../all/common/ -I../../$(PLATFORM)/platform/
INCDIR += -I../../all/minarch/libretro-common/include
INCDIR += -I./include -I./include/mbedtls_lib -I./include/yxml
INCDIR += -I./include/libogg/include -I./include/libopus/include
INCDIR += -I./include/opusfile/include
INCDIR += -I$(PREFIX)/include -I$(PREFIX_LOCAL)/include
INCDIR += -I../../$(PLATFORM)/libmsettings

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

APP_SRC = \
    musicplayer.c player.c playlist.c playlist_m3u.c \
    radio.c radio_net.c album_art.c lyrics.c radio_hls.c radio_curated.c \
    downloader.c selfupdate.c display_helper.c \
    podcast.c podcast_rss.c podcast_search.c \
    http_download.c wget_fetch.c wifi.c keyboard.c settings.c resume.c \
    add_to_playlist.c background.c ring_log.c log_trace.c watchdog.c crash_handler.c fb_capture.c \
    module_common.c module_menu.c module_library.c module_player.c \
    module_playlist.c module_radio.c module_podcast.c module_downloader.c \
    module_system.c module_settings.c \
    ui_fonts.c ui_icons.c ui_utils.c browser.c ui_album_art.c \
    ui_main.c ui_music.c ui_radio.c ui_downloader.c ui_podcast.c \
    ui_playlist.c ui_system.c ui_settings.c \
    spectrum.c \
    audio/kiss_fft.c audio/kiss_fftr.c \
    include/yxml/yxml.c \
    include/parson/parson.c \
    ../../all/common/utils.c ../../all/common/api.c \
    ../../all/common/config.c ../../all/common/scaler.c \
    ../../$(PLATFORM)/platform/platform.c

APP_OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(APP_SRC))

###########################################################
# Compiler / linker flags

MY_CFLAGS  = -O2 -fomit-frame-pointer
MY_CFLAGS += -DMBEDTLS_CONFIG_FILE='<mbedtls_config.h>'
MY_CFLAGS += -DOPUS_BUILD -DHAVE_LRINTF -DOP_DISABLE_HTTP -DOP_DISABLE_FLOAT_API
MY_CFLAGS += $(INCDIR) -DPLATFORM=\"$(PLATFORM)\" -std=gnu99
MY_CFLAGS += -DUSE_SDL2 -DUSE_GLES -DGL_GLEXT_PROTOTYPES
MY_CFLAGS += -MMD -MP

MY_LDFLAGS  = -L$(BUILD_DIR)
MY_LDFLAGS += -L$(PREFIX)/lib -L$(PREFIX_LOCAL)/lib
MY_LDFLAGS += -L$(PREFIX)/lib/$(CROSS_TRIPLE)
MY_LDFLAGS += -L../../$(PLATFORM)/libmsettings
MY_LDFLAGS += -L../../all/minarch/build/$(PLATFORM)

LDLIBS  = -lopus -lmbedtls
LDLIBS += $(shell pkg-config --libs sdl2 glesv2 2>/dev/null || echo "-lSDL2 -lGLESv2")
LDLIBS += -lSDL2_image -lSDL2_ttf
LDLIBS += -lmsettings -lsamplerate -lzip -lm -lpthread -ldl -lz
LDLIBS += -lasound -lfdk-aac

ifeq ($(PLATFORM), tg5050)
LDLIBS += -ltinyalsa
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
