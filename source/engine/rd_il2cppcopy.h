/* rd_il2cppcopy.h -- config.txt fast_boot: copy Unity's il2cpp data from the APK only once.
 *
 * On every launch libunity deletes <game folder>/il2cpp and copies the APK's il2cpp data
 * (global-metadata.dat, mscorlib resources, mono etc/) there again: 7.3 MB written and 3.3 MB
 * inflated, ~2.8 s of the first frame (analysis/research/26). rd_patches.c sends that call
 * to its own function, which skips the copy when the folder holds a stamp written after the
 * last complete copy that still matches: this loader's stamp version, the game's
 * unity.build-id and base.apk's size and modification time, with global-metadata.dat present.
 * Otherwise it lets Unity copy as before and writes the stamp afterwards.
 * Pure logic, host-tested (tests/c/test_rd_il2cppcopy.c). MIT (see LICENSE). */
#ifndef RD_IL2CPPCOPY_H
#define RD_IL2CPPCOPY_H

#include <stddef.h>
#include <stdint.h>

#define RD_IL2CPPCOPY_STAMP_MAX 160     /* bytes of a stamp, NUL included */

/* 1 if `path` (at most RD_PATH_MAX bytes read) is a folder named RD_IL2CPP_DATA_DIR: the
 * destination of the per-launch copy. */
int rd_il2cppcopy_is_data_dir(const char *path);

/* The stamp text for base.apk of apk_size bytes modified at apk_mtime into buf (n bytes).
 * Returns its length, or -1 if it does not fit. */
int rd_il2cppcopy_stamp(char *buf, size_t n, int64_t apk_size, int64_t apk_mtime);

/* 1 if the copy can be skipped: the stamp file's `len` bytes equal `stamp` and the copied
 * global-metadata.dat has metadata_size > 0 bytes. */
int rd_il2cppcopy_current(const char *file, size_t len, const char *stamp, int64_t metadata_size);

#endif
