/* imports.c -- the shim table that resolves the imports of libmain/libunity/libil2cpp.
 *
 * Copyright (C) 2021 fgsfds, Andy Nguyen (FF4 base)
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 *
 * Satisfied here: a large libc subset (shimmed where bionic != newlib), GLES2 + EGL (mesa),
 * OpenSL ES (opensles.c), the libandroid ALooper/ANativeWindow/ASensor API
 * (android_native_unity.c) and a few liblog/cxxabi helpers. unity_imports.c and
 * rd_imports.c add further tables; nx_resolve_imports() resolves against all of them.
 * tests/test_import_coverage.py checks that every import of the game's libraries is covered.
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <malloc.h>
extern int z_strncasecmp(const char *, const char *, unsigned long);  /* NULL-safe, unity_imports.c */
#include <unistd.h>
#include <ctype.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <wchar.h>
#include <errno.h>
#include <locale.h>
#include <setjmp.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include "nx_pointer.h"
#include "rd_fps.h"
#include "rd_platform.h"
#include <switch.h>
#include "diag.h"

#include "config.h"
#include "so_util.h"
#include "util.h"
#include "libc_shim.h"
#include "opensles.h"
#include "imports.h"
#include "android_native_unity.h"
#include "unity_imports.h"

extern int *__errno(void);              // newlib

// ---------------------------------------------------------------------------
// liblog
// ---------------------------------------------------------------------------

int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
  (void)prio;
#if DEBUG_LOG
  va_list list; static char string[0x1000];
  va_start(list, fmt); vsnprintf(string, sizeof(string), fmt, list); va_end(list);
  debugPrintf("%s: %s\n", tag, string);
#else
  (void)tag; (void)fmt;
#endif
  return 0;
}
int __android_log_write(int prio, const char *tag, const char *text) {
  (void)prio; debugPrintf("%s: %s\n", tag, text); return 0;
}
int __android_log_vprint(int prio, const char *tag, const char *fmt, va_list va) {
  (void)prio;
#if DEBUG_LOG
  static char string[0x1000]; vsnprintf(string, sizeof(string), fmt, va);
  debugPrintf("%s: %s\n", tag, string);
#else
  (void)tag; (void)fmt; (void)va;
#endif
  return 0;
}
void __assert2(const char *file, int line, const char *func, const char *expr) {
  debugPrintf("assert: %s:%d (%s): %s\n", file, line, func, expr); abort();
}

// ---------------------------------------------------------------------------
// stack protector / cxxabi
// ---------------------------------------------------------------------------

uint64_t __stack_chk_guard_fake = 0x0ull; /* match install_bionic_tls's zeroed tpidr+0x28 slot */
void __stack_chk_fail_fake(void) { debugPrintf("__stack_chk_fail\n"); abort(); }

int  __cxa_atexit_fake(void (*fn)(void *), void *arg, void *dso) { (void)fn; (void)arg; (void)dso; return 0; }
void __cxa_finalize_fake(void *dso) { (void)dso; }

// stdin/stdout/stderr point into the fake __sF block (see libc_shim.c)
FILE *stderr_fake = (FILE *)&fake_sF[2];

// ---------------------------------------------------------------------------
// pthread: bionic allocates the opaque types inline and zero-inits them, so we
// lazily back them with heap-allocated newlib objects stashed through the
// caller's pointer slot.
// ---------------------------------------------------------------------------

int pthread_mutex_init_fake(pthread_mutex_t **uid, const int *attr) {
  pthread_mutex_t *m = calloc(1, sizeof(pthread_mutex_t));
  if (!m) return -1;
  const int recursive = (attr && *attr == 1); // bionic PTHREAD_MUTEX_RECURSIVE == 1
  int ret;
  if (recursive) {
    pthread_mutexattr_t a; pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    ret = pthread_mutex_init(m, &a); pthread_mutexattr_destroy(&a);
  } else {
    ret = pthread_mutex_init(m, NULL);
  }
  if (ret != 0) { free(m); return -1; }
  *uid = m; return 0;
}
int pthread_mutex_destroy_fake(pthread_mutex_t **uid) {
  if (uid && *uid && (uintptr_t)*uid > 0x8000) { pthread_mutex_destroy(*uid); free(*uid); *uid = NULL; }
  return 0;
}
static int ensure_mutex(pthread_mutex_t **uid) {
  // A real (already-initialized) handle is a heap pointer; anything small is a
  // bionic static initializer left in place: 0 = normal, 0x4000 = recursive,
  // 0x8000 = errorcheck, etc. Back ALL of those with a real newlib mutex.
  // (Was only handling 0 and 0x4000 -> a 0x8000 errorcheck static was treated
  // as a valid pointer and locked at address 0x8000: wild deref / hang.)
  if ((uintptr_t)*uid < 0x10000) {
    int recursive = ((uintptr_t)*uid == 0x4000);
    int a = 1;
    return pthread_mutex_init_fake(uid, recursive ? &a : NULL);
  }
  return 0;
}
int pthread_mutex_lock_fake(pthread_mutex_t **uid) {
  if (ensure_mutex(uid) < 0) return -1;
  if (pthread_mutex_trylock(*uid) == 0) return 0;        // uncontended: no beacon
  diag_wait_enter(DIAG_W_MUTEX, *uid);
  int r = pthread_mutex_lock(*uid);
  diag_wait_exit();
  return r;
}
int pthread_mutex_trylock_fake(pthread_mutex_t **uid) { if (ensure_mutex(uid) < 0) return -1; return pthread_mutex_trylock(*uid); }
int pthread_mutex_unlock_fake(pthread_mutex_t **uid) { if (ensure_mutex(uid) < 0) return -1; return pthread_mutex_unlock(*uid); }
int pthread_mutex_timedlock_fake(pthread_mutex_t **uid, const struct timespec *abs) {
  (void)abs;
  if (ensure_mutex(uid) < 0) return -1;
  for (int i = 0; i < 1000; i++) {
    if (pthread_mutex_trylock(*uid) == 0) return 0;
    svcSleepThread(1000000ull);
  }
  return ETIMEDOUT;
}

int pthread_cond_init_fake(pthread_cond_t **cnd, const int *attr) {
  (void)attr;
  pthread_cond_t *c = calloc(1, sizeof(pthread_cond_t));
  if (!c) return -1;
  if (pthread_cond_init(c, NULL) < 0) { free(c); return -1; }
  *cnd = c; return 0;
}
static int ensure_cond(pthread_cond_t **cnd) {
  // like ensure_mutex: any small value is a bionic static cond initializer that
  // needs a real newlib object (was only treating 0 as needing init, so a
  // non-zero sentinel was waited/signalled on as a garbage pointer).
  if ((uintptr_t)*cnd < 0x10000) return pthread_cond_init_fake(cnd, NULL);
  return 0;
}
int pthread_cond_broadcast_fake(pthread_cond_t **cnd) { if (ensure_cond(cnd) < 0) return -1; return pthread_cond_broadcast(*cnd); }
int pthread_cond_signal_fake(pthread_cond_t **cnd) { if (ensure_cond(cnd) < 0) return -1; return pthread_cond_signal(*cnd); }
int pthread_cond_destroy_fake(pthread_cond_t **cnd) { if (cnd && (uintptr_t)*cnd >= 0x10000) { pthread_cond_destroy(*cnd); free(*cnd); *cnd = NULL; } return 0; }
#define COND_WAIT_CAP_MS 16
int pthread_cond_wait_fake(pthread_cond_t **cnd, pthread_mutex_t **mtx) {
  if (ensure_cond(cnd) < 0 || ensure_mutex(mtx) < 0) return -1;
  diag_wait_enter(DIAG_W_COND, *cnd);
  // Cap the UNTIMED wait too (not just the timed one below). Unity's engine
  // (job system, GfxDevice sync, PreloadManager) blocks the main thread in a
  // plain pthread_cond_wait; a raced/lost pthread_cond_signal -- or a
  // bionic-static-cond object mismatch across signal/wait -- would then hang the
  // whole engine forever (every worker idle, UnityMain parked in
  // svcWaitProcessWideKeyAtomic == the observed frame-2 deadlock). Waking every
  // ~16ms and returning as a spurious wakeup lets the caller re-check its
  // predicate (POSIX-legal: correct waiters always loop on the predicate), which
  // breaks a lost-wakeup stall without changing correct behaviour.
  struct timespec cap;
  clock_gettime(CLOCK_MONOTONIC, &cap);
  long add = COND_WAIT_CAP_MS * 1000000L;
  cap.tv_sec  += (cap.tv_nsec + add) / 1000000000L;
  cap.tv_nsec  = (cap.tv_nsec + add) % 1000000000L;
  int r = pthread_cond_timedwait(*cnd, *mtx, &cap);
  diag_wait_exit();
  return (r == ETIMEDOUT) ? 0 : r;   // timeout -> report as spurious wakeup
}
// Bound every timed cond-wait to at most COND_WAIT_CAP_MS. The .so libs (libc++
// std::condition_variable, and Swappy's frame pacer) compute an ABSOLUTE deadline
// against CLOCK_MONOTONIC, but newlib/libnx's pthread_cond_timedwait may measure
// "now" against a different clock -- a mismatch turns a ~16 ms vsync wait into an
// effectively infinite one (the Swappy hang: engine wedged in condvarWaitTimeout,
// frame counter frozen, black screen). Re-deriving the deadline as
// now(MONOTONIC)+min(requested, CAP) guarantees the wait can't exceed the cap
// regardless of which clock newlib uses, so the pacer hits its timeout fallback
// and keeps pacing. Spurious/early wakeups are POSIX-legal (every correct waiter
// re-checks its predicate), so this is safe in general.
int pthread_cond_timedwait_fake(pthread_cond_t **cnd, pthread_mutex_t **mtx, const struct timespec *t) {
  if (ensure_cond(cnd) < 0 || ensure_mutex(mtx) < 0) return -1;
  struct timespec now, cap;
  clock_gettime(CLOCK_MONOTONIC, &now);
  long add = COND_WAIT_CAP_MS * 1000000L;
  cap.tv_sec  = now.tv_sec + (now.tv_nsec + add) / 1000000000L;
  cap.tv_nsec = (now.tv_nsec + add) % 1000000000L;
  // honor the caller's deadline if it's sooner than our cap; else clamp to cap
  const struct timespec *use = &cap;
  if (t && (t->tv_sec < cap.tv_sec ||
            (t->tv_sec == cap.tv_sec && t->tv_nsec <= cap.tv_nsec)))
    use = t;
  diag_wait_enter(DIAG_W_COND, *cnd);
  int r = pthread_cond_timedwait(*cnd, *mtx, use);
  diag_wait_exit();
  return r;
}

int pthread_once_fake(volatile int *once, void (*init)(void)) {
  if (!once || !init) return -1;
  if (__sync_lock_test_and_set(once, 1) == 0) (*init)();
  return 0;
}

int pthread_mutexattr_init_fake(int *a) { if (a) *a = 0; return 0; }
int pthread_mutexattr_settype_fake(int *a, int t) { if (a) *a = t; return 0; }

// bionic pthread_attr_t is opaque storage we own; stash size/detach there
#define ATTR_MAGIC 0x41545452 /* 'ATTR' */
typedef struct { uint32_t magic; uint32_t detach; size_t stacksize; } OurAttr;

int pthread_attr_init_fake(void *a) { if (a) { OurAttr *o = a; o->magic = ATTR_MAGIC; o->detach = 0; o->stacksize = 0; } return 0; }
int pthread_attr_destroy_fake(void *a) { (void)a; return 0; }
int pthread_attr_setdetachstate_fake(void *a, int s) { if (a) { OurAttr *o = a; if (o->magic == ATTR_MAGIC) o->detach = (uint32_t)s; } return 0; }
int pthread_attr_setstacksize_fake(void *a, size_t s) { if (a) { OurAttr *o = a; if (o->magic == ATTR_MAGIC) o->stacksize = s; } return 0; }

typedef struct { void *(*entry)(void *); void *arg; int is_main; uint8_t tls[BIONIC_TLS_SIZE]; } ThreadStart;
static volatile int g_first_engine_thread_taken = 0;
static void *thread_trampoline(void *p) {
  ThreadStart *ts = (ThreadStart *)p;   /* leaked on purpose: tpidr points into ts->tls */
  install_bionic_tls(ts->tls);          // this thread's OWN stack-guard block (tpidr_el0+0x28)
  diag_thread_register(ts->entry, ts->is_main);
  void *r = ts->entry(ts->arg);
  diag_thread_unregister();
  return r;
}
int pthread_create_fake(pthread_t *thread, const void *bionic_attr, void *entry, void *arg) {
  ThreadStart *ts = malloc(sizeof(*ts));
  if (!ts) return -1;
  ts->entry = (void *(*)(void *))entry;
  ts->arg = arg;
  ts->is_main = (__sync_lock_test_and_set(&g_first_engine_thread_taken, 1) == 0);
  size_t stack = 0;
  if (bionic_attr) {
    const OurAttr *o = bionic_attr;
    if (o->magic == ATTR_MAGIC) stack = o->stacksize;
  }
  if (stack < (2u << 20)) stack = 2u << 20; // 2 MB floor for the heavy engine threads
  pthread_attr_t attr; pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, stack);
  const int r = pthread_create(thread, &attr, thread_trampoline, ts);
  pthread_attr_destroy(&attr);
  if (r != 0) { free(ts); return r; }
  return 0;
}
int pthread_join_fake(pthread_t thread, void **retval) {
  diag_wait_enter(DIAG_W_JOIN, (const void *)(uintptr_t)thread);
  int r = pthread_join(thread, retval);
  diag_wait_exit();
  return r;
}
int pthread_setschedparam_fake(pthread_t t, int policy, const void *p) { (void)t; (void)policy; (void)p; return 0; }
int pthread_sigmask_fake(int how, const void *set, void *old) { (void)how; (void)set; (void)old; return 0; }

// ---------------------------------------------------------------------------
// pthread TLS keys, multiplexed over a single real newlib key.
// devkitA64 backs pthread keys with a tiny pool (~16 libnx TLS slots), but
// Unity's runtime creates dozens during init (46 call sites). The ~17th
// pthread_key_create returns EAGAIN, and libunity treats that as fatal
// (asserts the key was created, else BRK). bionic allows 128 keys; emulate
// that: one real key holds a per-thread value array for up to 128 fake keys.
// ---------------------------------------------------------------------------
#define FAKE_KEYS_MAX 128
static pthread_mutex_t g_key_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct { int used; void (*dtor)(void *); } g_key_table[FAKE_KEYS_MAX];
static pthread_key_t g_master_key;
static int g_master_key_ready;
typedef struct { void *values[FAKE_KEYS_MAX]; } KeyValues;

static void master_key_dtor(void *p) {
  KeyValues *kv = p;
  for (int iter = 0; iter < 4; iter++) {     // POSIX: rerun while dtors set new values
    int again = 0;
    for (int i = 0; i < FAKE_KEYS_MAX; i++) {
      void *v = kv->values[i];
      if (g_key_table[i].used && g_key_table[i].dtor && v) {
        kv->values[i] = NULL;
        g_key_table[i].dtor(v);
        again = 1;
      }
    }
    if (!again) break;
  }
  free(kv);
}

int pthread_key_create_fake(unsigned *key, void (*dtor)(void *)) {
  pthread_mutex_lock(&g_key_mutex);
  if (!g_master_key_ready) {
    if (pthread_key_create(&g_master_key, master_key_dtor) != 0) {
      pthread_mutex_unlock(&g_key_mutex);
      debugPrintf("pthread_key_create_fake: master key alloc failed\n");
      return EAGAIN;
    }
    g_master_key_ready = 1;
  }
  for (unsigned i = 0; i < FAKE_KEYS_MAX; i++) {
    if (!g_key_table[i].used) {
      g_key_table[i].used = 1;
      g_key_table[i].dtor = dtor;
      *key = i + 1;                 // 1-based: a zeroed key is invalid
      pthread_mutex_unlock(&g_key_mutex);
      return 0;
    }
  }
  pthread_mutex_unlock(&g_key_mutex);
  debugPrintf("pthread_key_create_fake: out of keys\n");
  return EAGAIN;
}

int pthread_key_delete_fake(unsigned key) {
  if (key == 0 || key > FAKE_KEYS_MAX) return EINVAL;
  pthread_mutex_lock(&g_key_mutex);
  g_key_table[key - 1].used = 0;
  g_key_table[key - 1].dtor = NULL;
  pthread_mutex_unlock(&g_key_mutex);
  return 0;
}

void *pthread_getspecific_fake(unsigned key) {
  if (key == 0 || key > FAKE_KEYS_MAX || !g_master_key_ready) return NULL;
  KeyValues *kv = pthread_getspecific(g_master_key);
  return kv ? kv->values[key - 1] : NULL;
}

int pthread_setspecific_fake(unsigned key, const void *value) {
  if (key == 0 || key > FAKE_KEYS_MAX || !g_master_key_ready) return EINVAL;
  KeyValues *kv = pthread_getspecific(g_master_key);
  if (!kv) {
    kv = calloc(1, sizeof(*kv));
    if (!kv) return ENOMEM;
    pthread_setspecific(g_master_key, kv);
  }
  kv->values[key - 1] = (void *)value;
  return 0;
}

// ---------------------------------------------------------------------------
// misc small shims
// ---------------------------------------------------------------------------

static int ret0_i(void) { return 0; }
static int retm1_i(void) { return -1; }
static unsigned ret0_u(void) { return 0; }
static int signal_stub(int s, void *h) { (void)s; (void)h; return 0; }
static int sigaction_stub(int s, const void *a, void *o) { (void)s; (void)a; (void)o; return 0; }
static int ioctl_stub(int fd, unsigned long req, ...) { (void)fd; (void)req; return -1; }
static int fcntl_stub(int fd, int cmd, ...) { (void)fd; (void)cmd; return 0; }
static int tcgetattr_stub(int fd, void *t) { (void)fd; if (t) memset(t, 0, 60); return 0; }
static int tcsetattr_stub(int fd, int opt, const void *t) { (void)fd; (void)opt; (void)t; return 0; }

// POSIX file ops that devkitA64 newlib/libnx may not provide -- implement or
// stub them locally so the link never depends on their presence.
static int access_impl(const char *path, int mode) {
  (void)mode; struct stat st; return stat(path, &st) == 0 ? 0 : -1;
}
static int chmod_stub(const char *path, int mode) { (void)path; (void)mode; return 0; }
static int truncate_stub(const char *path, long len) { (void)path; (void)len; return 0; }
static int ftruncate_stub(int fd, long len) { return ftruncate(fd, (off_t)len); }
static int fsync_stub(int fd) { int r = fsync(fd); fsdevCommitDevice("sdmc"); return r; }
static int dup2_stub(int a, int b) { (void)a; return b; }
static long pread_impl(int fd, void *buf, size_t n, long off) {
  long cur = lseek(fd, 0, SEEK_CUR);
  if (cur < 0) return -1;
  if (lseek(fd, off, SEEK_SET) < 0) return -1;
  size_t total = 0;
  while (total < n) {                       /* loop: fsdev can short-read */
    long r = read(fd, (char *)buf + total, n - total);
    if (r <= 0) break;
    total += (size_t)r;
  }
  lseek(fd, cur, SEEK_SET);
  watch_dump("pread", fd, (long)off, (long)n, buf, (long)total);
  return (long)total;
}
static long pwrite_impl(int fd, const void *buf, size_t n, long off) {
  long cur = lseek(fd, 0, SEEK_CUR);
  if (cur < 0) return -1;
  if (lseek(fd, off, SEEK_SET) < 0) return -1;
  long r = write(fd, buf, n);
  lseek(fd, cur, SEEK_SET);
  return r;
}
static int uname_fake(void *buf) { if (buf) memset(buf, 0, 390); return 0; }
static long sysconf_pass(int n) { return sysconf_fake(n); }
static char *g_tzname_fake[2] = { (char *)"UTC", (char *)"UTC" };
static int readlink_stub(const char *p, char *b, size_t n) { (void)p; (void)b; (void)n; errno = EINVAL; return -1; }
static int link_stub(const char *a, const char *b) { (void)a; (void)b; return -1; }
static int symlink_stub(const char *a, const char *b) { (void)a; (void)b; return -1; }
static int fchmod_stub(int fd, int m) { (void)fd; (void)m; return 0; }
static int fchmodat_stub(int d, const char *p, int m, int f) { (void)d; (void)p; (void)m; (void)f; return 0; }
static int utimensat_stub(int d, const char *p, const void *t, int f) { (void)d; (void)p; (void)t; (void)f; return 0; }
static long sendfile_stub(int o, int i, long *off, size_t c) { (void)o; (void)i; (void)off; (void)c; return -1; }
static void *fdopendir_stub(int fd) { (void)fd; return NULL; }
static char *inet_ntoa_stub(uint32_t in) { (void)in; static char s[] = "0.0.0.0"; return s; }
static void _exit_fake(int code) { (void)code; extern void NX_NORETURN __libnx_exit(int rc); __libnx_exit(0); }

/* NULL-safe string ops. Unity's SystemInfo device-detection feeds the device
 * model/manufacturer (NULL in our fake JNI) straight into compare/search libc
 * calls; raw newlib then deref-NULL-crashes (this is what killed strcasecmp).
 * Treat NULL as an empty string: comparisons are unequal, searches find nothing,
 * length is zero -- so the device checks all fall through harmlessly. */
static int z_strcmp(const char *a, const char *b) {
  if (a == b) return 0;
  if (!a) return -1;
  if (!b) return 1;
  return strcmp(a, b);
}
static int z_strncmp(const char *a, const char *b, size_t n) {
  if (a == b || n == 0) return 0;
  if (!a) return -1;
  if (!b) return 1;
  return strncmp(a, b, n);
}
static char *z_strstr(const char *h, const char *n) {
  if (!h || !n) return NULL;
  return strstr(h, n);
}
static char *z_strchr(const char *s, int c) { return s ? strchr(s, c) : NULL; }
static char *z_strrchr(const char *s, int c) { return s ? strrchr(s, c) : NULL; }
static size_t z_strlen(const char *s) { return s ? strlen(s) : 0; }

// engine-facing NDK functions implemented elsewhere ------------------------
// libandroid: ALooper / ANativeWindow in android_native_unity.h; the sensor queue here.
extern int   ASensorEventQueue_enableSensor(void *, const void *);
extern int   ASensorEventQueue_disableSensor(void *, const void *);
extern int   ASensorEventQueue_setEventRate(void *, const void *, int32_t);
extern int   ASensorEventQueue_getEvents(void *, void *, size_t);

// ---------------------------------------------------------------------------
// EGL surface-size fixup
// ---------------------------------------------------------------------------

// switch-mesa returns 0 for EGL_WIDTH/EGL_HEIGHT on the window surface even
// though it renders full-screen; the engine trusts that and sets a 0x0 viewport
// (nothing draws). Hand back the real panel size instead.
static EGLBoolean egl_QuerySurface_log(EGLDisplay d, EGLSurface s, EGLint attr, EGLint *val) {
  EGLBoolean r = eglQuerySurface(d, s, attr, val);
  if (val) {
    if (attr == 0x3057 /*EGL_WIDTH*/  && *val <= 0) { *val = screen_width;  r = EGL_TRUE; }
    if (attr == 0x3056 /*EGL_HEIGHT*/ && *val <= 0) { *val = screen_height; r = EGL_TRUE; }
  }
  return r;
}

// SINGLETON window surface. libnx's one NWindow hosts exactly one swapchain, but Unity
// creates the window surface then re-creates it on gfx-state recreation; a 2nd
// eglCreateWindowSurface on the same NWindow corrupts the buffer queue and the present
// faults the vi compositor. So create it once and hand the same EGLSurface back; no-op destroy.
static EGLSurface g_win_surface = NULL;
static void      *g_win_native  = NULL;
extern EGLSurface eglCreateWindowSurface(EGLDisplay, EGLConfig, void *, const EGLint *);
static EGLSurface egl_CreateWindowSurface_log(EGLDisplay d, EGLConfig cfg, void *win, const EGLint *al) {
  if (g_win_surface && g_win_native == win) return g_win_surface;
  EGLSurface sfc = eglCreateWindowSurface(d, cfg, win, al);
  if (sfc && sfc != (EGLSurface)0) { g_win_surface = sfc; g_win_native = win; }
  nx_window_reassert();   /* mesa may reset the nwindow crop/transform when it builds its swapchain */
  return sfc;
}
extern EGLBoolean eglDestroySurface(EGLDisplay, EGLSurface);
static EGLBoolean egl_DestroySurface_keep(EGLDisplay d, EGLSurface s) {
  if (s && s == g_win_surface) return EGL_TRUE;   /* keep the one swapchain alive */
  return eglDestroySurface(d, s);
}


/* config.txt debug_fps_counter: frame rate of the presented frames, top-left. The text is
 * laid out again only when rd_fps publishes a new value (twice a second). */
static void fps_overlay_draw(void) {
  static rd_fps fps = RD_FPS_INIT;
  static rd_rect glyphs[RD_OVERLAY_MAX_RECTS], panel;
  static int nglyphs = -1;
  if (rd_fps_frame(&fps, rd_now_ns(), RD_FPS_WINDOW_NS) || nglyphs < 0) {
    char text[32];
    rd_fps_format(&fps, text, sizeof text);
    const float px = 3.0f * (float)screen_height / 720.0f;   /* 3 px at 720p, 4.5 at 1080p */
    const float pad = px, margin = 2.0f * px;
    panel = (rd_rect){ margin, margin, rd_fps_text_width(text, px) + 2.0f * pad,
                       RD_FPS_GLYPH_H * px + 2.0f * pad };
    nglyphs = rd_fps_layout(text, margin + pad, margin + pad, px, glyphs,
                            (int)(sizeof glyphs / sizeof glyphs[0]));
  }
  if (!rd_overlay_begin(screen_width, screen_height)) return;
  rd_overlay_rects(&panel, 1, 0.0f, 0.0f, 0.0f, 0.6f);
  rd_overlay_rects(glyphs, nglyphs, 1.0f, 1.0f, 0.2f, 1.0f);
  rd_overlay_end();
}

static EGLBoolean egl_SwapBuffers_log(EGLDisplay d, EGLSurface s) {
  /* Re-assert the nwindow transform for the first few frames while mesa settles its swapchain. */
  static unsigned frame = 0;
  if (frame < 5) { frame++; nx_window_reassert(); }
  if (config.debug_fps_counter) fps_overlay_draw();
  nxp_draw();   /* on-screen cursor overlay; saves/restores all GL state */
  return eglSwapBuffers(d, s);
}

// --- NDK/libc imports newer Unity versions need (kept: harmless, tested by coverage) ---
// Frame pacing: newer Unity uses the NDK AChoreographer (Android Frame Pacing / Swappy)
// for vsync callbacks. We drive frames from our own loop, so report the choreographer
// as UNAVAILABLE -- AChoreographer_getInstance returns NULL, which Unity's
// ChoreographerNDK::Enable null-checks (ldr [this+0x180]; cbz -> ret) and falls back
// to loop-cadence rendering (same intent as the jni_fake Java-Choreographer null).
static void *AChoreographer_getInstance(void){ return NULL; }

/* ALooper_pollAll: deprecated NDK API the game still imports; historically it loops
 * ALooper_pollOnce until there are no more immediate events. The wrapper implements the
 * real pollOnce (android_native_unity.c), so route to it -- one poll per call is sufficient
 * for Unity's pump and avoids spinning. Was unresolved -> would fault the first time the
 * engine pumped its looper. */
static int ALooper_pollAll_fake(int timeoutMillis, int *outFd, int *outEvents, void **outData) {
  return ALooper_pollOnce(timeoutMillis, outFd, outEvents, outData);
}
static void  AChoreographer_postFrameCallback(void *c, void *cb, void *d){ (void)c;(void)cb;(void)d; }
static void  AChoreographer_postFrameCallback64(void *c, void *cb, void *d){ (void)c;(void)cb;(void)d; }
// process_vm_readv: Unity's crash handler / memory tools use it; unsupported here.
static long  z_process_vm_readv(int pid, const void *lv, unsigned long lc,
                                const void *rv, unsigned long rc, unsigned long f){
  (void)pid;(void)lv;(void)lc;(void)rv;(void)rc;(void)f; errno = ENOSYS; return -1; }
// __gnu_strerror_r: GNU-variant (returns char*) of strerror_r.
static char  *z_gnu_strerror_r(int e, char *buf, size_t n){
  if (buf && n) { strncpy(buf, strerror(e), n); buf[n-1] = 0; return buf; }
  return strerror(e); }
extern int ALooper_removeFd(ALooper *l, int fd);   // android_native_unity.c

// ---------------------------------------------------------------------------
// import table
// ---------------------------------------------------------------------------

DynLibFunction dynlib_functions[] = {
  // --- liblog / cxxabi / fortify markers ---
  { "__android_log_print", (uintptr_t)&__android_log_print },
  { "__android_log_write", (uintptr_t)&__android_log_write },
  { "__android_log_vprint", (uintptr_t)&__android_log_vprint },
  { "android_set_abort_message", (uintptr_t)&android_set_abort_message_fake },
  { "__assert2", (uintptr_t)&__assert2 },
  { "__cxa_atexit", (uintptr_t)&__cxa_atexit_fake },
  { "__cxa_finalize", (uintptr_t)&__cxa_finalize_fake },
  { "__cxa_thread_atexit_impl", (uintptr_t)&__cxa_thread_atexit_impl_fake },
  { "__stack_chk_fail", (uintptr_t)&__stack_chk_fail_fake },
  { "__stack_chk_guard", (uintptr_t)&__stack_chk_guard_fake },
  { "__errno", (uintptr_t)&__errno },
  { "__get_h_errno", (uintptr_t)&__get_h_errno_fake },

  // --- fortify wrappers ---
  { "__memcpy_chk", (uintptr_t)&__memcpy_chk_fake },
  { "__memmove_chk", (uintptr_t)&__memmove_chk_fake },
  { "__memset_chk", (uintptr_t)&__memset_chk_fake },
  { "__strcat_chk", (uintptr_t)&__strcat_chk_fake },
  { "__strchr_chk", (uintptr_t)&__strchr_chk_fake },
  { "__strcpy_chk", (uintptr_t)&__strcpy_chk_fake },
  { "__strlen_chk", (uintptr_t)&__strlen_chk_fake },
  { "__strncat_chk", (uintptr_t)&__strncat_chk_fake },
  { "__strncpy_chk", (uintptr_t)&__strncpy_chk_fake },
  { "__strncpy_chk2", (uintptr_t)&__strncpy_chk2_fake },
  { "__strrchr_chk", (uintptr_t)&__strrchr_chk_fake },
  { "__vsnprintf_chk", (uintptr_t)&__vsnprintf_chk_fake },
  { "__vsprintf_chk", (uintptr_t)&__vsprintf_chk_fake },
  { "__snprintf_chk", (uintptr_t)&__snprintf_chk_fake },
  { "__sprintf_chk", (uintptr_t)&__sprintf_chk_fake },
  { "__open_2", (uintptr_t)&__open_2_fake },
  { "__read_chk", (uintptr_t)&__read_chk_fake },
  { "__pread_chk", (uintptr_t)&__pread_chk_fake },
  { "__FD_SET_chk", (uintptr_t)&__FD_SET_chk_fake },
  { "__FD_ISSET_chk", (uintptr_t)&__FD_ISSET_chk_fake },

  // --- bionic misc ---
  { "__system_property_get", (uintptr_t)&__system_property_get_fake },
  { "getauxval", (uintptr_t)&getauxval_fake },
  { "syscall", (uintptr_t)&syscall_fake },
  { "dl_iterate_phdr", (uintptr_t)&so_dl_iterate_phdr },
  { "__register_atfork", (uintptr_t)&__register_atfork_fake },
  { "__ctype_get_mb_cur_max", (uintptr_t)&__ctype_get_mb_cur_max_fake },
  { "sysconf", (uintptr_t)&sysconf_pass },
  { "pathconf", (uintptr_t)&pathconf_fake },
  { "uname", (uintptr_t)&uname_fake },
  { "openlog", (uintptr_t)&ret0_i },
  { "closelog", (uintptr_t)&ret0_i },
  { "syslog", (uintptr_t)&ret0_i },
  { "abort", (uintptr_t)&abort },
  { "_exit", (uintptr_t)&_exit_fake },

  // --- memory ---
  { "malloc", (uintptr_t)&malloc },
  { "calloc", (uintptr_t)&calloc },
  { "realloc", (uintptr_t)&realloc },
  { "free", (uintptr_t)&free },
  { "memalign", (uintptr_t)&memalign },
  { "posix_memalign", (uintptr_t)&posix_memalign_fake },
  { "mmap", (uintptr_t)&mmap_fake },
  { "munmap", (uintptr_t)&munmap_fake },
  { "mprotect", (uintptr_t)&mprotect_fake },
  { "madvise", (uintptr_t)&madvise_fake },

  // --- mem/str ---
  { "memchr", (uintptr_t)&memchr }, { "memcmp", (uintptr_t)&memcmp },
  { "memcpy", (uintptr_t)&memcpy }, { "memmove", (uintptr_t)&memmove },
  { "memset", (uintptr_t)&memset },
  { "strcat", (uintptr_t)&strcat }, { "strchr", (uintptr_t)&z_strchr },
  { "strcmp", (uintptr_t)&z_strcmp }, { "strcpy", (uintptr_t)&strcpy },
  { "strlen", (uintptr_t)&z_strlen }, { "strncasecmp", (uintptr_t)&z_strncasecmp },
  { "strncmp", (uintptr_t)&z_strncmp }, { "strncpy", (uintptr_t)&strncpy },
  { "strrchr", (uintptr_t)&z_strrchr }, { "strstr", (uintptr_t)&z_strstr },
  { "strtod", (uintptr_t)&strtod }, { "strtof", (uintptr_t)&strtof },
  { "strtol", (uintptr_t)&strtol }, { "strtold", (uintptr_t)&strtold },
  { "strtoll", (uintptr_t)&strtoll }, { "strtoul", (uintptr_t)&strtoul },
  { "strtoull", (uintptr_t)&strtoull }, { "atoi", (uintptr_t)&atoi },
  { "qsort", (uintptr_t)&qsort }, { "rand", (uintptr_t)&rand }, { "srand", (uintptr_t)&srand },
  { "isalnum", (uintptr_t)&isalnum }, { "isspace", (uintptr_t)&isspace },
  { "isupper", (uintptr_t)&isupper }, { "isxdigit", (uintptr_t)&isxdigit },
  { "tolower", (uintptr_t)&tolower },

  // --- wide / multibyte / locale ---
  { "wcslen", (uintptr_t)&wcslen }, { "wmemchr", (uintptr_t)&wmemchr },
  { "wmemcmp", (uintptr_t)&wmemcmp }, { "wcstod", (uintptr_t)&wcstod },
  { "wcstof", (uintptr_t)&wcstof }, { "wcstol", (uintptr_t)&wcstol },
  { "wcstold", (uintptr_t)&wcstold }, { "wcstoll", (uintptr_t)&wcstoll },
  { "wcstoul", (uintptr_t)&wcstoul }, { "wcstoull", (uintptr_t)&wcstoull },
  { "btowc", (uintptr_t)&btowc }, { "wctob", (uintptr_t)&wctob },
  { "mbrlen", (uintptr_t)&mbrlen }, { "mbrtowc", (uintptr_t)&mbrtowc },
  { "mbtowc", (uintptr_t)&mbtowc }, { "mbsrtowcs", (uintptr_t)&mbsrtowcs },
  { "wcrtomb", (uintptr_t)&wcrtomb }, { "mbsnrtowcs", (uintptr_t)&mbsnrtowcs_fake },
  { "wcsnrtombs", (uintptr_t)&wcsnrtombs_fake },
  { "setlocale", (uintptr_t)&setlocale }, { "localeconv", (uintptr_t)&localeconv },
  { "newlocale", (uintptr_t)&newlocale_fake }, { "freelocale", (uintptr_t)&freelocale_fake },
  { "uselocale", (uintptr_t)&uselocale_fake },
  { "iswalpha_l", (uintptr_t)&iswalpha_l_fake }, { "iswblank_l", (uintptr_t)&iswblank_l_fake },
  { "iswcntrl_l", (uintptr_t)&iswcntrl_l_fake }, { "iswdigit_l", (uintptr_t)&iswdigit_l_fake },
  { "iswlower_l", (uintptr_t)&iswlower_l_fake }, { "iswprint_l", (uintptr_t)&iswprint_l_fake },
  { "iswpunct_l", (uintptr_t)&iswpunct_l_fake }, { "iswspace_l", (uintptr_t)&iswspace_l_fake },
  { "iswupper_l", (uintptr_t)&iswupper_l_fake }, { "iswxdigit_l", (uintptr_t)&iswxdigit_l_fake },
  { "towlower_l", (uintptr_t)&towlower_l_fake }, { "towupper_l", (uintptr_t)&towupper_l_fake },
  { "strcoll_l", (uintptr_t)&strcoll_l_fake }, { "strxfrm_l", (uintptr_t)&strxfrm_l_fake },
  { "strftime_l", (uintptr_t)&strftime_l_fake }, { "strtold_l", (uintptr_t)&strtold_l_fake },
  { "strtoll_l", (uintptr_t)&strtoll_l_fake }, { "strtoull_l", (uintptr_t)&strtoull_l_fake },
  { "wcscoll_l", (uintptr_t)&wcscoll_l_fake }, { "wcsxfrm_l", (uintptr_t)&wcsxfrm_l_fake },

  // --- printf family ---
  { "printf", (uintptr_t)&debugPrintf }, { "puts", (uintptr_t)&puts },
  { "snprintf", (uintptr_t)&snprintf }, { "sprintf", (uintptr_t)&sprintf },
  { "swprintf", (uintptr_t)&swprintf }, { "vsnprintf", (uintptr_t)&vsnprintf },
  { "vsprintf", (uintptr_t)&vsprintf }, { "vasprintf", (uintptr_t)&vasprintf },
  { "sscanf", (uintptr_t)&sscanf }, { "vsscanf", (uintptr_t)&vsscanf },

  // --- math ---
  { "acosf", (uintptr_t)&acosf }, { "asinf", (uintptr_t)&asinf },
  { "atan2f", (uintptr_t)&atan2f }, { "cosf", (uintptr_t)&cosf },
  { "sinf", (uintptr_t)&sinf }, { "tanf", (uintptr_t)&tanf },
  { "expf", (uintptr_t)&expf }, { "logf", (uintptr_t)&logf },
  { "powf", (uintptr_t)&powf }, { "pow", (uintptr_t)&pow },
  { "fmodf", (uintptr_t)&fmodf }, { "sincosf", (uintptr_t)&sincosf_fake },
  { "nexttowardf", (uintptr_t)&nexttowardf },  /* was unresolved -> would fault if called */
  { "sqrt", (uintptr_t)&sqrt },                /* was unresolved -> game/engine calls it heavily */
  { "remainderf", (uintptr_t)&remainderf },    /* was unresolved -> would fault if called */

  // --- time ---
  { "clock_gettime", (uintptr_t)&clock_gettime }, { "gettimeofday", (uintptr_t)&gettimeofday },
  { "gmtime", (uintptr_t)&gmtime }, { "gmtime_r", (uintptr_t)&gmtime_r },
  { "localtime", (uintptr_t)&localtime }, { "localtime_r", (uintptr_t)&localtime_r },
  { "mktime", (uintptr_t)&mktime }, { "time", (uintptr_t)&time },
  { "nanosleep", (uintptr_t)&nanosleep }, { "usleep", (uintptr_t)&usleep },
  { "tzset", (uintptr_t)&tzset }, { "tzname", (uintptr_t)&g_tzname_fake },
  { "getenv", (uintptr_t)&getenv_fake }, { "putenv", (uintptr_t)&putenv },

  // --- stdio (fake __sF aware) ---
  { "__sF", (uintptr_t)&fake_sF },
  { "stdin", (uintptr_t)&fake_sF[0] }, { "stdout", (uintptr_t)&fake_sF[1] }, { "stderr", (uintptr_t)&fake_sF[2] },
  { "fopen", (uintptr_t)&fopen_fake }, { "fclose", (uintptr_t)&fclose_fake },
  { "fread", (uintptr_t)&fread_fake }, { "fwrite", (uintptr_t)&fwrite_fake },
  { "fseek", (uintptr_t)&fseek_fake }, { "fseeko", (uintptr_t)&fseeko },
  { "ftell", (uintptr_t)&ftell_fake }, { "ftello", (uintptr_t)&ftello },
  { "fflush", (uintptr_t)&fflush_fake }, { "fprintf", (uintptr_t)&fprintf_fake },
  { "vfprintf", (uintptr_t)&vfprintf_fake }, { "fputc", (uintptr_t)&fputc_fake },
  { "fputs", (uintptr_t)&fputs_fake }, { "fgetc", (uintptr_t)&fgetc_fake },
  { "fgets", (uintptr_t)&fgets_fake }, { "getc", (uintptr_t)&getc_fake },
  { "getwc", (uintptr_t)&getc_fake }, { "fputwc", (uintptr_t)&fputc_fake },
  { "ungetc", (uintptr_t)&ungetc_fake }, { "ungetwc", (uintptr_t)&ungetc_fake },
  { "feof", (uintptr_t)&feof_fake }, { "ferror", (uintptr_t)&ferror_fake },
  { "fileno", (uintptr_t)&fileno_fake }, { "remove", (uintptr_t)&remove },
  { "rename", (uintptr_t)&rename_fake },   /* POSIX replace-dest semantics; see libc_shim.c */

  // --- filesystem ---
  { "open", (uintptr_t)&open_fake }, { "openat", (uintptr_t)&openat_fake },
  { "close", (uintptr_t)&close_fake }, { "read", (uintptr_t)&read_fake },
  { "write", (uintptr_t)&write_fake }, { "pwrite", (uintptr_t)&pwrite_impl },
  { "pread", (uintptr_t)&pread_impl },
  { "lseek", (uintptr_t)&z_lseek }, { "pipe", (uintptr_t)&pipe_fake },
  { "poll", (uintptr_t)&poll_fake }, { "select", (uintptr_t)&select_fake },
  { "dup2", (uintptr_t)&dup2_stub }, { "fcntl", (uintptr_t)&fcntl_stub },
  { "ioctl", (uintptr_t)&ioctl_stub }, { "isatty", (uintptr_t)&isatty },
  { "tcgetattr", (uintptr_t)&tcgetattr_stub }, { "tcsetattr", (uintptr_t)&tcsetattr_stub },
  { "stat", (uintptr_t)&stat_fake }, { "fstat", (uintptr_t)&fstat_fake },
  { "lstat", (uintptr_t)&lstat_fake }, { "statfs", (uintptr_t)&statfs_fake },
  { "statvfs", (uintptr_t)&statvfs_fake }, { "access", (uintptr_t)&access_impl },
  { "mkdir", (uintptr_t)&mkdir_fake }, { "rmdir", (uintptr_t)&rmdir },
  { "unlink", (uintptr_t)&unlink }, { "unlinkat", (uintptr_t)&unlinkat_fake },
  { "chdir", (uintptr_t)&chdir }, { "getcwd", (uintptr_t)&getcwd_fake },
  { "chmod", (uintptr_t)&chmod_stub }, { "fchmod", (uintptr_t)&fchmod_stub },
  { "fchmodat", (uintptr_t)&fchmodat_stub }, { "truncate", (uintptr_t)&truncate_stub },
  { "ftruncate", (uintptr_t)&ftruncate_stub }, { "fsync", (uintptr_t)&fsync_stub },
  { "link", (uintptr_t)&link_stub }, { "symlink", (uintptr_t)&symlink_stub },
  { "readlink", (uintptr_t)&readlink_stub }, { "utime", (uintptr_t)&ret0_i },
  { "utimensat", (uintptr_t)&utimensat_stub }, { "sendfile", (uintptr_t)&sendfile_stub },
  { "opendir", (uintptr_t)&opendir_fake }, { "closedir", (uintptr_t)&closedir },
  { "readdir", (uintptr_t)&readdir_fake }, { "fdopendir", (uintptr_t)&fdopendir_stub },
  { "realpath", (uintptr_t)&realpath_fake },
  { "strerror", (uintptr_t)&strerror }, { "strerror_r", (uintptr_t)&strerror_r_fake },

  // --- signals / setjmp ---
  { "signal", (uintptr_t)&signal_stub }, { "sigaction", (uintptr_t)&sigaction_stub },
  { "sigaddset", (uintptr_t)&ret0_i }, { "sigemptyset", (uintptr_t)&ret0_i },
  { "setjmp", (uintptr_t)&setjmp }, { "longjmp", (uintptr_t)&longjmp },
  { "siglongjmp", (uintptr_t)&longjmp },

  // --- process / ids ---
  { "getpid", (uintptr_t)&getpid_fake }, { "getuid", (uintptr_t)&ret0_u },
  { "geteuid", (uintptr_t)&ret0_u }, { "getegid", (uintptr_t)&ret0_u },
  { "getpwuid", (uintptr_t)&getpwuid_fake }, { "getrusage", (uintptr_t)&getrusage_fake },
  { "fork", (uintptr_t)&fork_fake }, { "execvp", (uintptr_t)&execvp_fake },
  { "waitpid", (uintptr_t)&waitpid_fake }, { "kill", (uintptr_t)&kill_fake },
  { "sched_yield", (uintptr_t)&sched_yield_fake },
  { "sched_get_priority_max", (uintptr_t)&sched_get_priority_max_fake },
  { "sched_get_priority_min", (uintptr_t)&sched_get_priority_min_fake },

  // --- dynamic loader ---
  { "dlopen", (uintptr_t)&dlopen_fake }, { "dlclose", (uintptr_t)&dlclose_fake },
  { "dlerror", (uintptr_t)&dlerror_fake }, { "dlsym", (uintptr_t)&dlsym_fake },

  // --- networking (offline) ---
  { "socket", (uintptr_t)&socket_fake }, { "connect", (uintptr_t)&connect_fake },
  { "socketpair", (uintptr_t)&socketpair_fake },  /* was unresolved */
  { "environ", (uintptr_t)&fake_environ },        /* was unresolved (il2cpp env reads) */
  { "bind", (uintptr_t)&bind_fake }, { "listen", (uintptr_t)&listen_fake },
  { "accept", (uintptr_t)&accept_fake }, { "send", (uintptr_t)&send_fake },
  { "recv", (uintptr_t)&recv_fake }, { "sendto", (uintptr_t)&sendto_fake },
  { "recvfrom", (uintptr_t)&recvfrom_fake }, { "shutdown", (uintptr_t)&shutdown_fake },
  { "setsockopt", (uintptr_t)&setsockopt_fake }, { "getsockopt", (uintptr_t)&getsockopt_fake },
  { "getsockname", (uintptr_t)&getsockname_fake }, { "getpeername", (uintptr_t)&getpeername_fake },
  { "getaddrinfo", (uintptr_t)&getaddrinfo_fake }, { "freeaddrinfo", (uintptr_t)&freeaddrinfo_fake },
  { "getnameinfo", (uintptr_t)&getnameinfo_fake }, { "gethostname", (uintptr_t)&gethostname_fake },
  { "getservbyname", (uintptr_t)&getservbyname_fake },
  { "if_nametoindex", (uintptr_t)&if_nametoindex_fake }, { "if_indextoname", (uintptr_t)&if_indextoname_fake },
  // inet_aton returns 0 on FAILURE (nonzero == success); use ret0_i so callers
  // see a clean failure. inet_pton returns <=0 on error, so retm1_i is correct.
  { "inet_aton", (uintptr_t)&ret0_i }, { "inet_pton", (uintptr_t)&retm1_i },
  { "inet_ntoa", (uintptr_t)&inet_ntoa_stub },

  // --- pthread ---
  { "pthread_create", (uintptr_t)&pthread_create_fake }, { "pthread_join", (uintptr_t)&pthread_join_fake },
  { "pthread_detach", (uintptr_t)&pthread_detach }, { "pthread_exit", (uintptr_t)&pthread_exit },
  { "pthread_self", (uintptr_t)&pthread_self }, { "pthread_kill", (uintptr_t)&pthread_kill_gc },
  { "pthread_key_create", (uintptr_t)&pthread_key_create_fake }, { "pthread_key_delete", (uintptr_t)&pthread_key_delete_fake },
  { "pthread_getspecific", (uintptr_t)&pthread_getspecific_fake }, { "pthread_setspecific", (uintptr_t)&pthread_setspecific_fake },
  { "pthread_once", (uintptr_t)&pthread_once_fake },
  { "pthread_mutex_init", (uintptr_t)&pthread_mutex_init_fake },
  { "pthread_mutex_destroy", (uintptr_t)&pthread_mutex_destroy_fake },
  { "pthread_mutex_lock", (uintptr_t)&pthread_mutex_lock_fake },
  { "pthread_mutex_trylock", (uintptr_t)&pthread_mutex_trylock_fake },
  { "pthread_mutex_unlock", (uintptr_t)&pthread_mutex_unlock_fake },
  { "pthread_mutex_timedlock", (uintptr_t)&pthread_mutex_timedlock_fake },
  { "pthread_mutexattr_init", (uintptr_t)&pthread_mutexattr_init_fake },
  { "pthread_mutexattr_settype", (uintptr_t)&pthread_mutexattr_settype_fake },
  { "pthread_mutexattr_destroy", (uintptr_t)&ret0_i },
  { "pthread_cond_init", (uintptr_t)&pthread_cond_init_fake },
  { "pthread_cond_destroy", (uintptr_t)&pthread_cond_destroy_fake },
  { "pthread_cond_broadcast", (uintptr_t)&pthread_cond_broadcast_fake },
  { "pthread_cond_signal", (uintptr_t)&pthread_cond_signal_fake },
  { "pthread_cond_wait", (uintptr_t)&pthread_cond_wait_fake },
  { "pthread_cond_timedwait", (uintptr_t)&pthread_cond_timedwait_fake },
  { "pthread_rwlock_rdlock", (uintptr_t)&pthread_rwlock_rdlock_fake },
  { "pthread_rwlock_wrlock", (uintptr_t)&pthread_rwlock_wrlock_fake },
  { "pthread_rwlock_unlock", (uintptr_t)&pthread_rwlock_unlock_fake },
  { "pthread_attr_init", (uintptr_t)&pthread_attr_init_fake },
  { "pthread_attr_destroy", (uintptr_t)&pthread_attr_destroy_fake },
  { "pthread_attr_setdetachstate", (uintptr_t)&pthread_attr_setdetachstate_fake },
  { "pthread_attr_setstacksize", (uintptr_t)&pthread_attr_setstacksize_fake },
  { "pthread_setschedparam", (uintptr_t)&pthread_setschedparam_fake },
  { "pthread_sigmask", (uintptr_t)&pthread_sigmask_fake },
  { "sem_init", (uintptr_t)&sem_init_fake }, { "sem_destroy", (uintptr_t)&sem_destroy_fake },
  { "sem_post", (uintptr_t)&sem_post_fake }, { "sem_wait", (uintptr_t)&sem_wait_fake },
  { "sem_getvalue", (uintptr_t)&sem_getvalue_fake },
  { "sem_trywait", (uintptr_t)&sem_trywait_fake },
  { "sem_timedwait", (uintptr_t)&sem_timedwait_fake },

  // --- EGL (mesa; QuerySurface wrapped to fix the 0x0 surface-size report) ---
  { "eglGetDisplay", (uintptr_t)&eglGetDisplay }, { "eglInitialize", (uintptr_t)&eglInitialize },
  { "eglTerminate", (uintptr_t)&eglTerminate }, { "eglGetConfigs", (uintptr_t)&eglGetConfigs },
  { "eglGetConfigAttrib", (uintptr_t)&eglGetConfigAttrib },
  { "eglCreateWindowSurface", (uintptr_t)&egl_CreateWindowSurface_log },
  { "eglCreateContext", (uintptr_t)&eglCreateContext }, { "eglMakeCurrent", (uintptr_t)&eglMakeCurrent },
  { "eglSwapBuffers", (uintptr_t)&egl_SwapBuffers_log }, { "eglQuerySurface", (uintptr_t)&egl_QuerySurface_log },
  { "eglDestroyContext", (uintptr_t)&eglDestroyContext }, { "eglDestroySurface", (uintptr_t)&egl_DestroySurface_keep },

  // --- GLES2 (mesa) ---
  { "glActiveTexture", (uintptr_t)&glActiveTexture }, { "glAttachShader", (uintptr_t)&glAttachShader },
  { "glBindBuffer", (uintptr_t)&glBindBuffer }, { "glBindFramebuffer", (uintptr_t)&glBindFramebuffer },
  { "glBindRenderbuffer", (uintptr_t)&glBindRenderbuffer }, { "glBindTexture", (uintptr_t)&glBindTexture },
  { "glBlendEquationSeparate", (uintptr_t)&glBlendEquationSeparate }, { "glBlendFunc", (uintptr_t)&glBlendFunc },
  { "glBufferData", (uintptr_t)&glBufferData }, { "glClear", (uintptr_t)&glClear },
  { "glClearColor", (uintptr_t)&glClearColor }, { "glClearDepthf", (uintptr_t)&glClearDepthf },
  { "glClearStencil", (uintptr_t)&glClearStencil }, { "glColorMask", (uintptr_t)&glColorMask },
  { "glCompileShader", (uintptr_t)&glCompileShader }, { "glCompressedTexImage2D", (uintptr_t)&glCompressedTexImage2D },
  { "glCreateProgram", (uintptr_t)&glCreateProgram }, { "glCreateShader", (uintptr_t)&glCreateShader },
  { "glCullFace", (uintptr_t)&glCullFace }, { "glDeleteBuffers", (uintptr_t)&glDeleteBuffers },
  { "glDeleteFramebuffers", (uintptr_t)&glDeleteFramebuffers }, { "glDeleteProgram", (uintptr_t)&glDeleteProgram },
  { "glDeleteRenderbuffers", (uintptr_t)&glDeleteRenderbuffers }, { "glDeleteShader", (uintptr_t)&glDeleteShader },
  { "glDeleteTextures", (uintptr_t)&glDeleteTextures }, { "glDepthFunc", (uintptr_t)&glDepthFunc },
  { "glDepthMask", (uintptr_t)&glDepthMask }, { "glDisable", (uintptr_t)&glDisable },
  { "glDisableVertexAttribArray", (uintptr_t)&glDisableVertexAttribArray }, { "glDrawArrays", (uintptr_t)&glDrawArrays },
  { "glDrawElements", (uintptr_t)&glDrawElements }, { "glEnable", (uintptr_t)&glEnable },
  { "glEnableVertexAttribArray", (uintptr_t)&glEnableVertexAttribArray },
  { "glFramebufferRenderbuffer", (uintptr_t)&glFramebufferRenderbuffer },
  { "glFramebufferTexture2D", (uintptr_t)&glFramebufferTexture2D }, { "glGenBuffers", (uintptr_t)&glGenBuffers },
  { "glGenFramebuffers", (uintptr_t)&glGenFramebuffers }, { "glGenRenderbuffers", (uintptr_t)&glGenRenderbuffers },
  { "glGenTextures", (uintptr_t)&glGenTextures }, { "glGetAttribLocation", (uintptr_t)&glGetAttribLocation },
  { "glGetError", (uintptr_t)&glGetError }, { "glGetProgramiv", (uintptr_t)&glGetProgramiv },
  { "glGetShaderiv", (uintptr_t)&glGetShaderiv }, { "glGetUniformLocation", (uintptr_t)&glGetUniformLocation },
  { "glLinkProgram", (uintptr_t)&glLinkProgram }, { "glPixelStorei", (uintptr_t)&glPixelStorei },
  { "glPolygonOffset", (uintptr_t)&glPolygonOffset }, { "glReadPixels", (uintptr_t)&glReadPixels },
  { "glRenderbufferStorage", (uintptr_t)&glRenderbufferStorage }, { "glScissor", (uintptr_t)&glScissor },
  { "glShaderSource", (uintptr_t)&glShaderSource }, { "glStencilFunc", (uintptr_t)&glStencilFunc },
  { "glStencilMask", (uintptr_t)&glStencilMask }, { "glStencilOp", (uintptr_t)&glStencilOp },
  { "glTexImage2D", (uintptr_t)&glTexImage2D }, { "glTexParameteri", (uintptr_t)&glTexParameteri },
  { "glTexSubImage2D", (uintptr_t)&glTexSubImage2D }, { "glUniform1fv", (uintptr_t)&glUniform1fv },
  { "glUniform1i", (uintptr_t)&glUniform1i }, { "glUniform2fv", (uintptr_t)&glUniform2fv },
  { "glUniform3fv", (uintptr_t)&glUniform3fv }, { "glUniform4fv", (uintptr_t)&glUniform4fv },
  { "glUniformMatrix4fv", (uintptr_t)&glUniformMatrix4fv }, { "glUseProgram", (uintptr_t)&glUseProgram },
  { "glVertexAttribPointer", (uintptr_t)&glVertexAttribPointer }, { "glViewport", (uintptr_t)&glViewport },

  // --- libandroid: NativeActivity API (android_native.c) ---
  { "ALooper_prepare", (uintptr_t)&ALooper_prepare }, { "ALooper_addFd", (uintptr_t)&ALooper_addFd },
  { "ALooper_pollOnce", (uintptr_t)&ALooper_pollOnce }, { "ALooper_removeFd", (uintptr_t)&ALooper_removeFd },
  { "ALooper_pollAll", (uintptr_t)&ALooper_pollAll_fake },  /* was unresolved -> looper pump */
  // --- NDK AChoreographer (frame pacing) -> report unavailable, drive frames ourselves
  { "AChoreographer_getInstance", (uintptr_t)&AChoreographer_getInstance },
  { "AChoreographer_postFrameCallback", (uintptr_t)&AChoreographer_postFrameCallback },
  { "AChoreographer_postFrameCallback64", (uintptr_t)&AChoreographer_postFrameCallback64 },
  // --- more libc/libm symbols ---
  { "exp2", (uintptr_t)&exp2 }, { "tanhf", (uintptr_t)&tanhf }, { "hypotf", (uintptr_t)&hypotf },
  { "strncat", (uintptr_t)&strncat }, { "process_vm_readv", (uintptr_t)&z_process_vm_readv },
  { "__gnu_strerror_r", (uintptr_t)&z_gnu_strerror_r },
  { "ANativeWindow_setBuffersGeometry", (uintptr_t)&ANativeWindow_setBuffersGeometry },
  { "ASensorEventQueue_enableSensor", (uintptr_t)&ASensorEventQueue_enableSensor },
  { "ASensorEventQueue_disableSensor", (uintptr_t)&ASensorEventQueue_disableSensor },
  { "ASensorEventQueue_setEventRate", (uintptr_t)&ASensorEventQueue_setEventRate },
  { "ASensorEventQueue_getEvents", (uintptr_t)&ASensorEventQueue_getEvents },



  // --- OpenSL ES (opensles.c) ---
  // FMOD's OpenSL output dlsym()s a fixed set of SL_IID_* at init and treats ANY
  // NULL as a fatal "Error loading file" (19) -- it aborts before even calling
  // slCreateEngine. (Observed: it asks for ENGINE, ANDROIDSIMPLEBUFFERQUEUE,
  // ANDROIDCONFIGURATION, PLAY, then RECORD -> NULL -> abort.) opensles.c already
  // DEF_IID()-defines all 49 as unique self-pointers, so export every one; the
  // exact request order then can't matter.
  { "slCreateEngine", (uintptr_t)&slCreateEngine },
  #define SL_IID(n) { "SL_IID_" #n, (uintptr_t)&SL_IID_##n }
  SL_IID(3DCOMMIT), SL_IID(3DDOPPLER), SL_IID(3DGROUPING), SL_IID(3DLOCATION),
  SL_IID(3DMACROSCOPIC), SL_IID(3DSOURCE), SL_IID(ANDROIDCONFIGURATION),
  SL_IID(ANDROIDEFFECT), SL_IID(ANDROIDEFFECTCAPABILITIES), SL_IID(ANDROIDEFFECTSEND),
  SL_IID(ANDROIDSIMPLEBUFFERQUEUE), SL_IID(AUDIODECODERCAPABILITIES), SL_IID(AUDIOENCODER),
  SL_IID(AUDIOENCODERCAPABILITIES), SL_IID(AUDIOIODEVICECAPABILITIES), SL_IID(BASSBOOST),
  SL_IID(BUFFERQUEUE), SL_IID(DEVICEVOLUME), SL_IID(DYNAMICINTERFACEMANAGEMENT),
  SL_IID(DYNAMICSOURCE), SL_IID(EFFECTSEND), SL_IID(ENGINE), SL_IID(ENGINECAPABILITIES),
  SL_IID(ENVIRONMENTALREVERB), SL_IID(EQUALIZER), SL_IID(LED), SL_IID(METADATAEXTRACTION),
  SL_IID(METADATATRAVERSAL), SL_IID(MIDIMESSAGE), SL_IID(MIDIMUTESOLO), SL_IID(MIDITEMPO),
  SL_IID(MIDITIME), SL_IID(MUTESOLO), SL_IID(NULL), SL_IID(OBJECT), SL_IID(OUTPUTMIX),
  SL_IID(PITCH), SL_IID(PLAY), SL_IID(PLAYBACKRATE), SL_IID(PREFETCHSTATUS),
  SL_IID(PRESETREVERB), SL_IID(RATEPITCH), SL_IID(RECORD), SL_IID(SEEK), SL_IID(THREADSYNC),
  SL_IID(VIBRA), SL_IID(VIRTUALIZER), SL_IID(VISUALIZATION), SL_IID(VOLUME),
  #undef SL_IID
};

size_t dynlib_numfunctions = sizeof(dynlib_functions) / sizeof(*dynlib_functions);

// ---- libc imports of the Real Drift libraries missing from the table above ---
// ctype/wctype may be macros in newlib, so wrap them to take addresses.
#include <wctype.h>
static int    lx_isalpha(int c)      { return isalpha(c); }
static wint_t lx_towupper(wint_t c)  { return towupper(c); }
static int    lx_strcoll(const char *a, const char *b)            { return strcoll(a, b); }
static size_t lx_strxfrm(char *a, const char *b, size_t n)        { return strxfrm(a, b, n); }
static int    lx_wcscoll(const wchar_t *a, const wchar_t *b)      { return wcscoll(a, b); }
static size_t lx_wcsxfrm(wchar_t *a, const wchar_t *b, size_t n)  { return wcsxfrm(a, b, n); }
static int    lx_tcflush(int fd, int q) { (void)fd; (void)q; return 0; }   // no TTY on Switch
DynLibFunction libc_extra_functions[] = {
  { "isalpha", (uintptr_t)&lx_isalpha }, { "towupper", (uintptr_t)&lx_towupper },
  { "strcoll", (uintptr_t)&lx_strcoll }, { "strxfrm",  (uintptr_t)&lx_strxfrm },
  { "wcscoll", (uintptr_t)&lx_wcscoll }, { "wcsxfrm",  (uintptr_t)&lx_wcsxfrm },
  { "tcflush", (uintptr_t)&lx_tcflush },
};
size_t libc_extra_numfunctions = sizeof(libc_extra_functions) / sizeof(*libc_extra_functions);


/* Combined shim table (base + Unity supplementary), built once and shared by
 * both import resolution and dlsym(). */
/* Real Drift extras (rd_imports.c). */
extern DynLibFunction rd_dynlib_functions[];
extern int rd_dynlib_numfunctions;

static DynLibFunction *g_combined = NULL;
static int g_combined_n = 0;
static void build_combined(void) {
  if (g_combined) return;
  g_combined_n = (int)dynlib_numfunctions + unity_dynlib_numfunctions
               + (int)libc_extra_numfunctions + rd_dynlib_numfunctions;
  g_combined = malloc((size_t)g_combined_n * sizeof(DynLibFunction));
  size_t off = 0;
  memcpy(g_combined + off, dynlib_functions, dynlib_numfunctions * sizeof(DynLibFunction));
  off += dynlib_numfunctions;
  memcpy(g_combined + off, unity_dynlib_functions,
         (size_t)unity_dynlib_numfunctions * sizeof(DynLibFunction));
  off += unity_dynlib_numfunctions;
  memcpy(g_combined + off, libc_extra_functions,
         libc_extra_numfunctions * sizeof(DynLibFunction));
  off += libc_extra_numfunctions;
  memcpy(g_combined + off, rd_dynlib_functions,
         (size_t)rd_dynlib_numfunctions * sizeof(DynLibFunction));
}

/* Search the shim table by name -- used by dlsym() for the GLES/EGL/libc
 * entry points the engine resolves dynamically (not module exports). */
uintptr_t dynlib_find_export(const char *name) {
  if (!name) return 0;
  build_combined();
  for (int i = 0; i < g_combined_n; i++)
    if (strcmp(name, g_combined[i].symbol) == 0)
      return g_combined[i].func;
  return 0;
}

void nx_resolve_imports(so_module *mod) {
  so_relocate(mod);
  /* Resolve against the base table plus the Unity and Real Drift supplementary tables.
   * so_resolve poisons anything missing, so we must search both in a single
   * pass -- build a combined table once. */
  build_combined();
  so_resolve(mod, g_combined, g_combined_n, 1);
}
