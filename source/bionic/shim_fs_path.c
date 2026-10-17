/* shim_fs_path.c -- path handling of the file-system shims: the APK asset root and Unity's
 * case-sensitivity probe redirected into the game folder, the inherited basename read
 * fallback, mkdir -p that never touches the SD root, and /proc + /sys served through open().
 *
 * Split out of libc_shim.c (inherited from the so-loader lineage, MIT). MIT (see LICENSE). */
#define _GNU_SOURCE

#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include "rd_platform.h"
#include "rd_device.h"
#include "util.h"
#include "libc_shim.h"
#include "shim_fs.h"
#include "rd_modimage.h"


// Inherited read fallback: if a path with a subdirectory is missing, use the file of the same
// name in the cwd (the game folder) if there is one. Reads only, never a write. Logged, so a
// console run shows whether Real Drift ever needs it (it was written for MVGL asset packs).
int basename_fallback(const char *path, char *out, size_t outsz) {
  const char *slash = strrchr(path, '/');
  if (!slash || !slash[1]) return 0;   // no subdir component to strip
  struct stat st;
  snprintf(out, outsz, "%s", slash + 1); // basename, resolved against the cwd
  if (stat(out, &st) != 0) return 0;
  debugPrintf("[io] basename fallback: %s -> %s\n", path, out);
  return 1;
}

// Create one directory, skipping paths newlib's mkdir() can't handle safely.
// A bare "device:" path (e.g. "sdmc:") makes newlib resolve to a device root
// with an empty in-device path and dereference a NULL devoptab -- a Data Abort
// reading devoptab->mkdir_r at +0x68. Refuse those (and null/empty).
static int safe_mkdir(const char *p) {
  if (!p || !*p) { errno = EINVAL; return -1; }
  const char *colon = strchr(p, ':');
  if (colon) {                       // has a "device:" prefix
    const char *in = colon + 1;      // the path inside the device
    while (*in == '/') in++;
    if (!*in) { errno = EEXIST; return 0; }  // "sdmc:" / "sdmc:/" -> root, skip
    // A single top-level component ("sdmc:/switch") also null-derefs newlib's
    // devoptab. Such dirs (the homebrew mount point) always pre-exist already.
    if (!strchr(in, '/')) { errno = EEXIST; return 0; }
  }
  return mkdir(p, 0777);
}

// mkdir -p: create `dir` and every missing parent. Save data lives in subdirs
// the engine only mkdir()s one level at a time, so a deeper missing parent left
// the whole chain (and the save write) failing.
//
// We must NOT try to create the game root or any ancestor of it ("sdmc:",
// "sdmc:/switch", "sdmc:/switch/realdrift_nx"): they already exist, they aren't
// ours, and newlib's mkdir() of a *top-level* path (one component under the
// device, e.g. "sdmc:/switch") null-derefs its devoptab -> Data Abort at the
// mkdir_r slot (+0x68). So begin the parent walk *after* RD_GAME_HOME.
static void mkdir_p_dir(const char *dir) {
  if (!dir || !*dir) return;
  char tmp[512];
  if (snprintf(tmp, sizeof(tmp), "%s", dir) <= 0) return;
  size_t skip;
  const size_t glen = strlen(RD_GAME_HOME);
  if (strncmp(tmp, RD_GAME_HOME, glen) == 0 && (tmp[glen] == '/' || tmp[glen] == '\0')) {
    skip = glen;                                  // only create *under* the game root
  } else {
    const char *colon = strchr(tmp, ':');         // unknown base: at least skip "device:"
    skip = colon ? (size_t)(colon + 1 - tmp) : 0;
  }
  for (char *p = tmp + skip + 1; *p; p++)
    if (*p == '/') { *p = '\0'; safe_mkdir(tmp); *p = '/'; }
  if (tmp[skip]) safe_mkdir(tmp);
}
// create the parent directory chain of a file path
void mkdir_parents(const char *filepath) {
  char tmp[512];
  snprintf(tmp, sizeof(tmp), "%s", filepath);
  char *last = strrchr(tmp, '/');
  if (!last || last == tmp) return;
  *last = '\0';
  mkdir_p_dir(tmp);
}

// mkdir wrapper: create the full chain and treat "already exists" as success
int mkdir_fake(const char *path, unsigned mode) {
  (void)mode;
  if (!path || !*path) { errno = EINVAL; return -1; }
  mkdir_p_dir(path);
  int r = safe_mkdir(path);
  if (r != 0 && errno == EEXIST) r = 0;
  return r;
}

int g_watch_fd = -1;   /* data.unity3d fd: trace its reads/seeks to debug header load */

// Synthetic /proc and /sys files. Unity reads /proc/meminfo (MemTotal) to size
// its allocator reservations and /proc/cpuinfo + /sys cpu range to count cores
// for the job system. Values: rd_device.h (RAM deliberately reported small).
const char *synthetic_proc(const char *path) {
  if (!path) return NULL;
  if (!strcmp(path, "/proc/meminfo"))
    return "MemTotal:        " RD_STR(RD_DEV_MEM_KB) " kB\n"
           "MemFree:         " RD_STR(RD_DEV_MEM_FREE_KB) " kB\n"
           "MemAvailable:    " RD_STR(RD_DEV_MEM_FREE_KB) " kB\n"
           "Buffers:              0 kB\n"
           "Cached:               0 kB\n"
           "SwapTotal:            0 kB\n"
           "SwapFree:             0 kB\n";
  _Static_assert(RD_DEV_CPU_CORES == 3, "update the cpuinfo / cpu range strings below");
  _Static_assert(RD_DEV_MEM_KB > 0 && RD_DEV_MEM_FREE_KB > 0, "rd_device.h not included?");
  if (!strcmp(path, "/proc/cpuinfo"))
    return "processor\t: 0\nprocessor\t: 1\nprocessor\t: 2\n"
           "Features\t: fp asimd aes pmull sha1 sha2 crc32\n"
           "CPU implementer\t: 0x41\nCPU architecture: 8\nCPU variant\t: 0x1\n"
           "CPU part\t: 0xd07\nCPU revision\t: 1\n";
  if (strstr(path, "cpu_capacity")) return "1024\n";
  if (strstr(path, "cpuinfo_max_freq") || strstr(path, "scaling_max_freq")) return RD_DEV_CPU_MAX_KHZ "\n";
  if (strstr(path, "cpuinfo_min_freq") || strstr(path, "scaling_min_freq")) return RD_DEV_CPU_MIN_KHZ "\n";
  if (strstr(path, "/cpu/possible") || strstr(path, "/cpu/present") || strstr(path, "/cpu/online"))
    return "0-2\n";
  if (!strncmp(path, "/proc/", 6) || !strncmp(path, "/sys/", 5)) return ""; // empty for the rest
  return NULL;
}

// Serve /proc and /sys reads that arrive through raw open() (e.g.
// /proc/self/maps, which the engine opens to enumerate memory mappings).
// newlib's open() can't be memory-backed, so materialize the synthetic content
// into a small file under the game dir and hand back a real fd. Returns an fd,
// or -1 if `path` isn't a node we synthesize (caller proceeds normally).
int synth_proc_open(const char *path) {
  if (!path) return -1;
  if (strncmp(path, "/proc/", 6) && strncmp(path, "/sys/", 5)) return -1;
  static char buf[16384];
  int len;
  if (!strcmp(path, "/proc/self/maps") || !strcmp(path, "/proc/self/smaps")) {
    len = so_dump_maps(buf, sizeof buf);
  } else {
    const char *s = synthetic_proc(path);
    if (!s) return -1;                                   // not /proc or /sys
    len = (int)strlen(s);
    if (len > (int)sizeof buf) len = (int)sizeof buf;
    memcpy(buf, s, (size_t)len);
  }
  char safe[160]; size_t j = 0;
  for (const char *p = path; *p && j < sizeof safe - 1; p++) safe[j++] = (*p == '/') ? '_' : *p;
  safe[j] = '\0';
  char tf[256];
  snprintf(tf, sizeof tf, "%s/.synth%s", RD_GAME_HOME, safe);

  /* Only rewrite the backing file when the content actually changed.
   *
   * This used to create + write + close + reopen on EVERY call -- four SD operations. Unity
   * re-reads /proc/self/maps each time it symbolicates a native stack, which this game does on
   * every tap, so that was ~33 ms of SD traffic per tap even after the .so map and open caches
   * removed everything else. The module list does not change after boot, so the content is
   * identical every time: hash it, and skip the write when it matches. Only the final read-only
   * open remains, and that is cheap. */
  {
    static uint64_t last_hash[8];
    static int      last_slot = 0, nslots = 0;
    uint64_t h = 1469598103934665603ULL;             /* FNV-1a over path + content */
    for (const char *p = tf; *p; p++) { h ^= (unsigned char)*p; h *= 1099511628211ULL; }
    for (int i = 0; i < len; i++)     { h ^= (unsigned char)buf[i]; h *= 1099511628211ULL; }
    int fresh = 0;
    for (int i = 0; i < nslots; i++) if (last_hash[i] == h) { fresh = 1; break; }
    if (!fresh) {
      int wfd = open(tf, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (wfd >= 0) { if (write(wfd, buf, (size_t)len) < 0) { /* best effort */ } close(wfd); }
      if (nslots < 8) last_hash[nslots++] = h;
      else { last_hash[last_slot] = h; last_slot = (last_slot + 1) % 8; }
      debugPrintf("[io] synth %s regenerated (%d bytes)\n", path, len);
    }
  }
  return open(tf, O_RDONLY);
}

// ---------------------------------------------------------------------------


/* Unity probes filesystem case-sensitivity on every boot: it creates
 * CASESENSITIVETEST<guid> (O_CREAT|O_EXCL) in the data root, re-opens it under a
 * different case, and never cleans up -- a fresh GUID name each launch, so junk
 * accumulates on the SD card. Redirect every such name (any case, any guid) onto ONE
 * fixed hidden scratch file. Different-case probes then hit the same file, which is
 * exactly the case-INSENSITIVE answer FAT/exFAT gives anyway, and main.c sweeps the
 * scratch (plus any strays from older builds) at boot. */
const char *casetest_redirect(const char *path) {
  if (!path) return path;
  const char *b = strrchr(path, '/');
  b = b ? b + 1 : path;
  if (strncasecmp(b, RD_CASETEST_PREFIX, sizeof RD_CASETEST_PREFIX - 1) == 0)
    return RD_GAME_HOME "/" RD_CASETEST_SCRATCH;
  return path;
}

/* Android APK asset root -> our asset dir on the SD card.
 *
 * The game builds ABSOLUTE "/assets/..." paths (on Android that's the APK's
 * asset root). On Switch a leading "/" makes newlib resolve against the DEFAULT
 * DEVICE ROOT, i.e. sdmc:/assets/... -- the root of the SD card -- which does not
 * exist. Our assets live under RD_GAME_HOME/assets/. Result: every absolute asset
 * open returned -1 (the .unity3d bundles and the Data json files) even though they
 * were present, because they were being looked for in the wrong place.
 *
 * Relative "assets/..." paths already work (they resolve against the cwd, which
 * main.c chdir()s to RD_GAME_HOME), so ONLY the absolute form needs rewriting.
 * basename_fallback() can't cover this: it drops the subdirectory, so
 * "/assets/AssetBundles/Episode_1_Levels.unity3d" would degrade to looking for
 * "Episode_1_Levels.unity3d" in the game root. Keep the full subpath instead. */
const char *asset_redirect(const char *path, char *buf, size_t bufsz) {
  if (!path) return path;
  if (!strncmp(path, "/assets/", 8)) {
    snprintf(buf, bufsz, RD_GAME_HOME "/assets/%s", path + 8);
    return buf;
  }
  if (!strcmp(path, "/assets")) {
    snprintf(buf, bufsz, RD_GAME_HOME "/assets");
    return buf;
  }
  return path;
}
