/* rd_exitlog.c -- see rd_exitlog.h. The __wrap_* functions replace every reference to the
 * wrapped call in the link (Makefile LDFLAGS), the import tables' entries included.
 * MIT (see LICENSE). */
#include <switch.h>
#include "diag.h"
#include "rd_exitlog.h"
#include "rd_exitwhy.h"
#include "rd_log.h"

static int g_reported;

void rd_exitlog_report(int kind, int code) {
  /* raise() may be harmless (the default action is not always to exit), so it only logs and
   * leaves the claim to a real exit that may follow. */
  if (kind == RD_EXIT_RAISE ? __atomic_load_n(&g_reported, __ATOMIC_SEQ_CST)
                            : !rd_exit_claim(&g_reported))
    return;
  char what[32];
  rd_exit_describe(what, sizeof what, kind, code);
  diag_report_self(RD_LOG_TAG_CRASH, what);
}

void NX_NORETURN __real_abort(void);
void NX_NORETURN __real_exit(int code);
void NX_NORETURN __real__exit(int code);
int __real_raise(int sig);

/* abort(): __assert2, __stack_chk_fail, std::terminate, il2cpp and Boehm fatal errors. */
void NX_NORETURN __wrap_abort(void);
void NX_NORETURN __wrap_abort(void) {
  rd_exitlog_report(RD_EXIT_ABORT, 0);
  __real_abort();
}

/* exit(): the engine's import, and fatal_error() after its message. */
void NX_NORETURN __wrap_exit(int code);
void NX_NORETURN __wrap_exit(int code) {
  rd_exitlog_report(RD_EXIT_EXIT, code);
  __real_exit(code);
}

/* _exit(): where newlib's abort() and exit() end. */
void NX_NORETURN __wrap__exit(int code);
void NX_NORETURN __wrap__exit(int code) {
  rd_exitlog_report(RD_EXIT_QUICK, code);
  __real__exit(code);
}

/* raise(): the engine's import; logged unless an exit was already reported. */
int __wrap_raise(int sig);
int __wrap_raise(int sig) {
  rd_exitlog_report(RD_EXIT_RAISE, sig);
  return __real_raise(sig);
}
