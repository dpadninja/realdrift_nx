/* android_log.h -- liblog and __assert2 for the engine (android_log.c).
 *
 * Referenced from the import table (imports.c). Log lines go to debug.log as "<tag>: <text>";
 * the formatted variants print only with DEBUG_LOG (config.h). MIT (see LICENSE). */
#ifndef ANDROID_LOG_H
#define ANDROID_LOG_H

#include <stdarg.h>

/* liblog: prio is ignored; return 0 like Android does on success. */
int  __android_log_print(int prio, const char *tag, const char *fmt, ...);
int  __android_log_write(int prio, const char *tag, const char *text);
int  __android_log_vprint(int prio, const char *tag, const char *fmt, va_list va);
/* bionic's assert(): logs the failed expression with its location, then abort(). */
void __assert2(const char *file, int line, const char *func, const char *expr);

#endif
