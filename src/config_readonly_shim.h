/*
 * config_readonly_shim.h — force-included ONLY when compiling NextUI's
 * ../../all/common/config.c (see the target-specific rule in Makefile).
 *
 * WHY THIS EXISTS
 * ---------------
 * NextUI has two issues, see https://github.com/LoveRetro/NextUI/issues/789:
 * - config.c's CFG_init() calls CFG_sync()
 * - CFG_sync() rewrites the settings file minuisettings.txt on every call.
 * Both are non issues for NextUI's own programs, but pose compatibility
 * issues when config.c is compiled into a side program such as Music Player,
 * because options unknown to config.c are stripped during the write.
 * Effectively, Music Player reverts all settings unknown to it to their
 * default values.
 *
 * A music player has no business writing global config. This shim makes the
 * fopen-based writes in config.c fail, leaving minuisettings.txt intact.
 *
 * HOW IT WORKS
 * ------------
 * When compiling config.c, this header is force-included via
 * "-include $(CURDIR)/config_readonly_shim.h"
 * and redirects fopen() to mp_config_readonly_fopen(), which prevents writing
 * by returning failure (NULL) when mode "w" or "a" is requested.
 * It is safe because the only fopen(..., "w") in the file call is in CFG_sync(),
 * and CFG_sync() gracefully handles a NULL value ignoring errno. the only
 * visible side effect is a log "[CFG] Unable to open settings file, cant write".
 */
#ifndef MP_CONFIG_READONLY_SHIM_H
#define MP_CONFIG_READONLY_SHIM_H

#include <stdio.h> /* pull in the real fopen prototype before we shadow it */

static inline FILE *mp_config_readonly_fopen(const char *path, const char *mode)
{
	/* Block write ("w"/"w+"/"wb"...) and append ("a"...) opens; allow reads
	 * (and any unexpected mode) through. config.c only ever opens "r" and "w". */
	if (mode && (mode[0] == 'w' || mode[0] == 'a'))
		return NULL;
	return fopen(path, mode); /* macro not yet defined here -> the real fopen */
}

/* From here on, every fopen() in config.c routes through the guard above. */
#define fopen(path, mode) mp_config_readonly_fopen((path), (mode))

#endif /* MP_CONFIG_READONLY_SHIM_H */
