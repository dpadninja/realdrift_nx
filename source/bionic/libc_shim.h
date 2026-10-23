/* libc_shim.h -- the bionic C library functions we implement for the game's Android libraries.
 *
 * libmain/libunity/libil2cpp are linked against bionic. Where the bionic and newlib ABIs differ
 * (struct layouts, flag values, missing functions) we provide converting wrappers, declared
 * here and referenced from the import tables (imports.c, unity_imports.c, rd_imports.c);
 * everything that matches is passed straight through. Online/IPC functionality (sockets,
 * fork/exec, dlopen of system libs) is dead on the Switch and stubbed to fail cleanly so the
 * engine falls back to offline behaviour. The implementations:
 *   shim_libc.c     fortify, system properties, futex/syscall, sysconf, locale, stubs
 *   shim_fs.c       open/read/write/close/stat/rename/readdir (+ shim_fs_path.c, shim_fs_cache.c)
 *   shim_stdio.c    fopen and the fake stdin/stdout/stderr streams
 *   shim_mmap.c     mmap/munmap/mprotect: heap arena, overcommit window, file-map cache
 *   shim_dl.c       dlopen/dlsym over the loaded modules
 *   shim_pthread.c  rwlocks, semaphores, timed waits
 * Forked from badpiggies_nx by ChanseyIsTheBest (https://github.com/ChanseyIsTheBest/badpiggies_nx,
 * commit 7726be3), from the so-loader lineage; split and documented for realdrift_nx.
 * MIT (see LICENSE). */

#ifndef __LIBC_SHIM_H__
#define __LIBC_SHIM_H__

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stddef.h>
#include <time.h>
#include <pthread.h>

/* ---- shim_libc.c ------------------------------------------------------------------------ */

/* `p` without its leading "device:" (e.g. "sdmc:"), i.e. a Unix-rooted path. Paths handed to
 * managed code must be device-less: Mono/IL2CPP see "sdmc:/..." as relative and Path.Combine
 * builds "assets/bin/Data/sdmc:/switch/realdrift_nx", which newlib then mis-parses. newlib
 * resolves device-less absolute paths through the default device, so file I/O still works. */
const char *managed_path(const char *p);

/* Fortify (_chk) variants: the object-size arguments are ignored, the plain function runs. */
void *__memcpy_chk_fake(void *dst, const void *src, size_t n, size_t dstlen);
void *__memmove_chk_fake(void *dst, const void *src, size_t n, size_t dstlen);
void *__memset_chk_fake(void *dst, int c, size_t n, size_t dstlen);
char *__strcat_chk_fake(char *dst, const char *src, size_t dstlen);
char *__strchr_chk_fake(const char *s, int c, size_t slen);
char *__strcpy_chk_fake(char *dst, const char *src, size_t dstlen);
size_t __strlen_chk_fake(const char *s, size_t slen);
char *__strncat_chk_fake(char *dst, const char *src, size_t n, size_t dstlen);
char *__strncpy_chk_fake(char *dst, const char *src, size_t n, size_t dstlen);
char *__strncpy_chk2_fake(char *dst, const char *src, size_t n, size_t dstlen, size_t srclen);
char *__strrchr_chk_fake(const char *s, int c, size_t slen);
int __vsnprintf_chk_fake(char *s, size_t maxlen, int flag, size_t slen, const char *fmt, va_list va);
int __vsprintf_chk_fake(char *s, int flag, size_t slen, const char *fmt, va_list va);
int __snprintf_chk_fake(char *s, size_t maxlen, int flag, size_t slen, const char *fmt, ...);
int __sprintf_chk_fake(char *s, int flag, size_t slen, const char *fmt, ...);
int  __open_2_fake(const char *path, int flags);
long __read_chk_fake(int fd, void *buf, size_t count, size_t buflen);
long __pread_chk_fake(int fd, void *buf, size_t count, long off, size_t buflen);
void __FD_SET_chk_fake(int fd, void *set, size_t setlen);
int  __FD_ISSET_chk_fake(int fd, const void *set, size_t setlen);

/* bionic-only functions: system properties answer from rd_device.h (the fake device), futex
 * and syscall() are emulated over libnx, sysconf reports the fake device's cores and pages. */
int __system_property_get_fake(const char *name, char *value);
unsigned long getauxval_fake(unsigned long type);
int gettid_fake(void);
long syscall_fake(long number, ...);
void sincosf_fake(float x, float *s, float *c);
int sched_get_priority_max_fake(int policy);
int sched_get_priority_min_fake(int policy);
void android_set_abort_message_fake(const char *msg);
size_t __ctype_get_mb_cur_max_fake(void);
int __register_atfork_fake(void);
int __cxa_thread_atexit_impl_fake(void (*fn)(void *), void *arg, void *dso);
long sysconf_fake(int name);
long pathconf_fake(const char *path, int name);

/* *_l locale variants: the locale argument is ignored, the C locale is used. */
void *newlocale_fake(int mask, const char *locale, void *base);
void freelocale_fake(void *loc);
void *uselocale_fake(void *loc);
int iswalpha_l_fake(int wc, void *loc); int iswblank_l_fake(int wc, void *loc);
int iswcntrl_l_fake(int wc, void *loc); int iswdigit_l_fake(int wc, void *loc);
int iswlower_l_fake(int wc, void *loc); int iswprint_l_fake(int wc, void *loc);
int iswpunct_l_fake(int wc, void *loc); int iswspace_l_fake(int wc, void *loc);
int iswupper_l_fake(int wc, void *loc); int iswxdigit_l_fake(int wc, void *loc);
int towlower_l_fake(int wc, void *loc); int towupper_l_fake(int wc, void *loc);
int strcoll_l_fake(const char *a, const char *b, void *loc);
size_t strxfrm_l_fake(char *dst, const char *src, size_t n, void *loc);
size_t strftime_l_fake(char *s, size_t max, const char *fmt, const void *tm, void *loc);
long double strtold_l_fake(const char *s, char **end, void *loc);
long long strtoll_l_fake(const char *s, char **end, int base, void *loc);
unsigned long long strtoull_l_fake(const char *s, char **end, int base, void *loc);
int wcscoll_l_fake(const wchar_t *a, const wchar_t *b, void *loc);
size_t wcsxfrm_l_fake(wchar_t *dst, const wchar_t *src, size_t n, void *loc);
size_t mbsnrtowcs_fake(wchar_t *dst, const char **src, size_t nms, size_t len, void *ps);
size_t wcsnrtombs_fake(char *dst, const wchar_t **src, size_t nwc, size_t len, void *ps);

/* Networking: online play is dead, so every call fails cleanly (sockets cannot be created,
 * names do not resolve) and the engine stays offline. */
int socket_fake(int d, int t, int p);
int connect_fake(int s, const void *a, unsigned l);
int bind_fake(int s, const void *a, unsigned l);
int listen_fake(int s, int b);
int accept_fake(int s, void *a, void *l);
long send_fake(int s, const void *b, size_t l, int f);
long recv_fake(int s, void *b, size_t l, int f);
long sendto_fake(int s, const void *b, size_t l, int f, const void *a, unsigned al);
long recvfrom_fake(int s, void *b, size_t l, int f, void *a, void *al);
int shutdown_fake(int s, int how);
int setsockopt_fake(int s, int lv, int n, const void *v, unsigned l);
int socketpair_fake(int d, int t, int p, int sv[2]);
int getsockopt_fake(int s, int lv, int n, void *v, void *l);
int getsockname_fake(int s, void *a, void *l);
int getpeername_fake(int s, void *a, void *l);
int getaddrinfo_fake(const char *node, const char *svc, const void *hints, void **res);
void freeaddrinfo_fake(void *res);
int getnameinfo_fake(const void *a, unsigned al, char *h, unsigned hl, char *s, unsigned sl, int f);
int gethostname_fake(char *name, size_t len);
void *getservbyname_fake(const char *n, const char *p);
unsigned if_nametoindex_fake(const char *n);
char *if_indextoname_fake(unsigned i, char *buf);
int *__get_h_errno_fake(void);

/* Process control: fork/exec are unavailable (fail with ENOSYS); the process is pid 1, has a
 * home directory in the game folder, and getcwd drops the "sdmc:" device. */
extern char **fake_environ;
int fork_fake(void);
int execvp_fake(const char *f, char *const argv[]);
int waitpid_fake(int pid, int *status, int opts);
int kill_fake(int pid, int sig);
int getpid_fake(void);
int sched_yield_fake(void);
void *getpwuid_fake(int uid);
char *getenv_fake(const char *name);
char *getcwd_fake(char *buf, size_t size);
int getrusage_fake(int who, void *usage);

/* ---- shim_fs.c (with shim_fs_path.c, shim_fs_cache.c) ------------------------------------ */

/* Files: open translates bionic flags, applies the path redirects and caches (shim_fs.h);
 * stat/fstat/lstat/readdir convert to the bionic aarch64 layouts (struct bionic_stat). */
struct bionic_stat;
int open_fake(const char *path, int flags, ...);
int openat_fake(int dirfd, const char *path, int flags, ...);
int mkdir_fake(const char *path, unsigned mode);
int unlinkat_fake(int dirfd, const char *path, int flags);
void *opendir_fake(const char *path);
void *readdir_fake(void *dirp);
int stat_fake(const char *path, struct bionic_stat *st);
int fstat_fake(int fd, struct bionic_stat *st);
int lstat_fake(const char *path, struct bionic_stat *st);
char *realpath_fake(const char *path, char *resolved);
int strerror_r_fake(int err, char *buf, size_t len);
int statvfs_fake(const char *path, void *buf);
int statfs_fake(const char *path, void *buf);

/* POSIX rename(): replaces an existing destination (Horizon's does not), closing our own open
 * handles on it first. */
int rename_fake(const char *oldp, const char *newp);

/* Descriptor I/O: fake-pipe fds go to fakefd.c, read-only archives through the read-ahead
 * window, everything else to newlib. close() commits the SD card after a written file. */
long read_fake(int fd, void *buf, size_t count);
long write_fake(int fd, const void *buf, size_t count);
int  close_fake(int fd);
int  pipe_fake(int fds[2]);
int  poll_fake(void *fds, unsigned long nfds, int timeout);
int  select_fake(int n, void *r, void *w, void *e, void *t);
/* lseek (off_t is 64-bit on arm64, so this also serves lseek64), aware of read-ahead. */
long z_lseek(int fd, long off, int whence);

/* Debug trace of reads on one fd (g_watch_fd, -1 = off): logs offsets and the first bytes. */
extern int g_watch_fd;
void watch_dump(const char *tag, int fd, long a, long b, const void *buf, long got);

/* Commits pending SD card writes (saves); called every RD_SD_FLUSH_FRAMES and on exit. */
void nx_sd_flush(void);

/* ---- shim_stdio.c ------------------------------------------------------------------------ */

/* fopen with the path redirects, synthetic /proc files and a large buffer for the APK/OBB. */
FILE *fopen_fake(const char *path, const char *mode);

/* stdio over the fake bionic __sF block: the engine's stdin/stdout/stderr are &fake_sF[0..2]
 * (output goes to debug.log); any other FILE * is a newlib stream. */
extern uint8_t fake_sF[3][0x100];
size_t fwrite_fake(const void *ptr, size_t size, size_t n, FILE *f);
size_t fread_fake(void *ptr, size_t size, size_t n, FILE *f);
int fputc_fake(int c, FILE *f);
int fputs_fake(const char *s, FILE *f);
int fflush_fake(FILE *f);
int fclose_fake(FILE *f);
int ferror_fake(FILE *f);
int feof_fake(FILE *f);
int fileno_fake(FILE *f);
int fseek_fake(FILE *f, long off, int whence);
long ftell_fake(FILE *f);
int getc_fake(FILE *f);
int fgetc_fake(FILE *f);
char *fgets_fake(char *s, int n, FILE *f);
int ungetc_fake(int c, FILE *f);
int fprintf_fake(FILE *f, const char *fmt, ...);
int vfprintf_fake(FILE *f, const char *fmt, va_list va);

/* ---- shim_mmap.c ------------------------------------------------------------------------- */

/* mmap/munmap/mprotect/madvise over the page arena (and the overcommit window for large
 * PROT_NONE reservations); file maps are read in, read-only ones deduplicated. */
int posix_memalign_fake(void **out, size_t align, size_t size);
void *mmap_fake(void *addr, size_t length, int prot, int flags, int fd, long offset);
int munmap_fake(void *addr, size_t length);
int mprotect_fake(void *addr, size_t len, int prot);
int madvise_fake(void *addr, size_t len, int advice);

/* Arms the stack-region overcommit arena: window = a reserved stack-region range, pool = a
 * page-aligned heap buffer that backs committed pages. Called once from main() after the
 * newlib heap exists. Returns 1 if armed, 0 if mmap stays heap-backed. */
int  oc_arena_init(void *window, size_t window_bytes, void *pool, size_t pool_bytes);

/* How full the mmap pools are, in bytes (debug_mem, rd_memwatch.h). */
typedef struct {
  size_t arena_used, arena_size;      /* page arena: reserved / total */
  size_t oc_committed, oc_pool;       /* overcommit window: committed pages / pool */
  size_t fallback;                    /* maps served by memalign after the arena filled */
} mmap_usage;
/* Fills *out. Any thread (takes the mmap locks). */
void mmap_get_usage(mmap_usage *out);

/* ---- shim_dl.c --------------------------------------------------------------------------- */

/* dlopen returns a dummy handle; dlsym looks up exports of the loaded modules, then our
 * shim tables, then GLES/EGL through eglGetProcAddress. */
void *dlopen_fake(const char *name, int flags);
int dlclose_fake(void *h);
const char *dlerror_fake(void);
void *dlsym_fake(void *handle, const char *symbol);

/* ---- shim_pthread.c ---------------------------------------------------------------------
 * bionic allocates pthread_mutex_t / pthread_cond_t inline and zero-initialises them; each is
 * backed lazily by a heap-allocated newlib object whose pointer is stored in the caller's slot
 * (the `**` arguments). Return values follow POSIX (0 or an error number). */
int pthread_mutex_init_fake(pthread_mutex_t **uid, const int *attr);
int pthread_mutex_destroy_fake(pthread_mutex_t **uid);
int pthread_mutex_lock_fake(pthread_mutex_t **uid);
int pthread_mutex_trylock_fake(pthread_mutex_t **uid);
int pthread_mutex_unlock_fake(pthread_mutex_t **uid);
int pthread_mutex_timedlock_fake(pthread_mutex_t **uid, const struct timespec *abs);
int pthread_cond_init_fake(pthread_cond_t **cnd, const int *attr);
int pthread_cond_broadcast_fake(pthread_cond_t **cnd);
int pthread_cond_signal_fake(pthread_cond_t **cnd);
int pthread_cond_destroy_fake(pthread_cond_t **cnd);
/* Waits are capped (COND_WAIT_CAP_MS) and the deadline is re-derived on CLOCK_MONOTONIC, so a
 * missed wake or a clock mismatch costs one re-poll instead of a hang. */
int pthread_cond_wait_fake(pthread_cond_t **cnd, pthread_mutex_t **mtx);
int pthread_cond_timedwait_fake(pthread_cond_t **cnd, pthread_mutex_t **mtx, const struct timespec *t);
int pthread_once_fake(volatile int *once, void (*init)(void));
/* Attributes live in the caller's opaque bionic pthread_attr_t (we keep stack size and
 * detach state; scheduling attributes are ignored). */
int pthread_mutexattr_init_fake(int *a);
int pthread_mutexattr_settype_fake(int *a, int t);
int pthread_attr_init_fake(void *a);
int pthread_attr_destroy_fake(void *a);
int pthread_attr_setdetachstate_fake(void *a, int s);
int pthread_attr_setstacksize_fake(void *a, size_t s);
/* Starts an engine thread: at least a 2 MB stack, its own bionic TLS block (stack guard), and
 * registration with the stall watchdog (diag.h). */
int pthread_create_fake(pthread_t *thread, const void *bionic_attr, void *entry, void *arg);
int pthread_join_fake(pthread_t thread, void **retval);
int pthread_setschedparam_fake(pthread_t t, int policy, const void *p);
int pthread_sigmask_fake(int how, const void *set, void *old);
/* Thread-local keys over one newlib key holding a per-thread value array (bionic key numbers
 * are small integers starting at 1; destructors run at thread exit). */
int pthread_key_create_fake(unsigned *key, void (*dtor)(void *));
int pthread_key_delete_fake(unsigned key);
void *pthread_getspecific_fake(unsigned key);
int pthread_setspecific_fake(unsigned key, const void *value);
/* rwlocks and semaphores, lazily backed by libnx RwLock / newlib semaphores like the mutexes. */
int pthread_rwlock_rdlock_fake(void **rw);
int pthread_rwlock_wrlock_fake(void **rw);
int pthread_rwlock_unlock_fake(void **rw);
int sem_init_fake(void **s, int pshared, unsigned int value);
int sem_destroy_fake(void **s);
int sem_post_fake(void **s);
int sem_wait_fake(void **s);
int sem_trywait_fake(void **s);
int sem_timedwait_fake(void **s, const struct timespec *abs);
int sem_getvalue_fake(void **s, int *val);

#endif
