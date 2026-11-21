#ifndef __FILEDB_TEST_H__
#define __FILEDB_TEST_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>

// The reads of the card that the file scan makes, for a test that must make a read fail.

typedef int (*FileDbStatFunction)(int directory, const char* name, struct stat* status, int flags);
typedef struct dirent* (*FileDbReadDirFunction)(DIR* directory);
typedef int (*FileDbStreamErrorFunction)(FILE* file);

// Replaces the functions that read the card in place of fstatat(), readdir() and ferror().
// Pass NULL to use the function of the system again.
void FileDb_setCardFunctions(FileDbStatFunction stat_function,
                             FileDbReadDirFunction read_dir_function,
                             FileDbStreamErrorFunction stream_error_function);

#ifdef __cplusplus
}
#endif

#endif
