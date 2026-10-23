/* rd_sdstat.c -- what reaches the SD card, for debug_load (rd_loadprof.h): newlib's stdio and
 * read() end in libnx's fsdev, which opens and reads files with fsFsOpenFile / fsFileRead.
 * The Makefile links them through -Wl,--wrap; each call is counted with its bytes and time
 * (always: an atomic add and two clock reads against an IPC round trip).
 * No host test: libnx FS wraps that only count.
 * MIT (see LICENSE). */
#include <switch.h>
#include "rd_platform.h"
#include "rd_perfstat.h"

Result __real_fsFsOpenFile(FsFileSystem *fs, const char *path, u32 mode, FsFile *out);
Result __wrap_fsFsOpenFile(FsFileSystem *fs, const char *path, u32 mode, FsFile *out);
Result __real_fsFileRead(FsFile *f, s64 off, void *buf, u64 read_size, u32 option, u64 *bytes_read);
Result __wrap_fsFileRead(FsFile *f, s64 off, void *buf, u64 read_size, u32 option, u64 *bytes_read);

Result __wrap_fsFsOpenFile(FsFileSystem *fs, const char *path, u32 mode, FsFile *out) {
  const uint64_t t0 = rd_now_ns();
  const Result rc = __real_fsFsOpenFile(fs, path, mode, out);
  __atomic_add_fetch(&nx_stat_sd_open_ns, rd_now_ns() - t0, __ATOMIC_RELAXED);
  __atomic_add_fetch(&nx_stat_sd_open, 1, __ATOMIC_RELAXED);
  return rc;
}

Result __wrap_fsFileRead(FsFile *f, s64 off, void *buf, u64 read_size, u32 option, u64 *bytes_read) {
  const uint64_t t0 = rd_now_ns();
  const Result rc = __real_fsFileRead(f, off, buf, read_size, option, bytes_read);
  __atomic_add_fetch(&nx_stat_sd_read_ns, rd_now_ns() - t0, __ATOMIC_RELAXED);
  __atomic_add_fetch(&nx_stat_sd_read, 1, __ATOMIC_RELAXED);
  if (R_SUCCEEDED(rc) && bytes_read)
    __atomic_add_fetch(&nx_stat_sd_read_bytes, *bytes_read, __ATOMIC_RELAXED);
  return rc;
}
