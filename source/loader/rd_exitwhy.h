/* rd_exitwhy.h -- naming the ways the process ends without the crash handler, host-testable
 * (tests/c/test_rd_exitwhy.c). Used by rd_exitlog.c.
 *
 * On Android abort() raises SIGABRT and the system reports a crash. On the Switch newlib turns
 * abort(), exit() and _exit() into a plain process exit: the console goes back to HOME without
 * an error, and the last second of the buffered debug.log is lost with the reason (a run that
 * left the race for the menu ended so). The loader wraps these calls to log who made them.
 * Signal numbers are Linux/bionic ones: the engine passes them to raise().
 * MIT (see LICENSE). */
#ifndef RD_EXITWHY_H
#define RD_EXITWHY_H

#include <stddef.h>

enum { RD_EXIT_ABORT, RD_EXIT_EXIT, RD_EXIT_QUICK, RD_EXIT_RAISE };   /* abort exit _exit raise */

/* Writes the call as it was made into buf (cap bytes): "abort()", "exit(1)", "_exit(0)",
 * "raise(SIGABRT)" (or "raise(42)" for a signal without a name). */
void rd_exit_describe(char *buf, size_t cap, int kind, int code);

/* 1 for the first caller to pass `flag` (atomically set), 0 for every later one: abort()
 * ends in _exit(), and only the first of them is the reason worth logging. */
int rd_exit_claim(int *flag);

#endif
