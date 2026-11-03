/* rd_exitlog.h -- logs the calls that end the process without the crash handler: abort(),
 * exit(), _exit() and raise(), from the engine (import tables), mesa, libnx and our own code
 * (-Wl,--wrap in the Makefile). Each logs "[crash] ===== abort() on thread X =====" with a
 * backtrace (diag_report_self); "[crash]" lines flush debug.log at once, so the buffered lines
 * before it reach the SD card too. The call then goes on as before (rd_exitwhy.h has why).
 * Any thread. MIT (see LICENSE). */
#ifndef RD_EXITLOG_H
#define RD_EXITLOG_H

/* Logs the first exit call of the process (kind RD_EXIT_*, code = exit code or signal); later
 * ones, such as the _exit() that abort() ends in, are not logged. A raise() is logged unless
 * an exit was, and does not stop the exit after it from being logged. */
void rd_exitlog_report(int kind, int code);

#endif
