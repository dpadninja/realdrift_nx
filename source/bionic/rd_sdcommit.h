/* rd_sdcommit.h -- which written files get the SD card committed when they are closed.
 *
 * libnx buffers sdmc: writes until fsdevCommitDevice; shim_fs_cache.c commits when a written
 * file is closed so a save survives a HOME kill. Unity also rewrites its il2cpp data folder
 * from the APK on every launch (21 files, each closed and committed in the first frame); those
 * are recreated anyway, so they wait for the periodic nx_sd_flush instead.
 * Pure logic, host-tested (tests/c/test_rd_sdcommit.c). MIT (see LICENSE). */
#ifndef RD_SDCOMMIT_H
#define RD_SDCOMMIT_H

/* 1 if closing a file written at `path` should commit the card now; 0 for files under the
 * game folder's RD_IL2CPP_DATA_DIR (with or without the "sdmc:" prefix). */
int rd_sdcommit_on_close(const char *path);

#endif
