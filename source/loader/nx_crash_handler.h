/* nx_crash_handler.h -- hooks of the crash reporter (nx_crash_handler.c).
 *
 * libnx calls __libnx_exception_handler on a CPU exception; the reporter dumps registers and a
 * backtrace to the log. Frames inside the game's .so files are named through
 * crash_resolve_module, which the loader overrides (main.c) because those images are not real
 * modules the system knows. MIT (see LICENSE). */
#ifndef NX_CRASH_HANDLER_H
#define NX_CRASH_HANDLER_H

#include <stddef.h>
#include <stdint.h>

/* If `addr` lies inside a loaded module, writes its name to name_out (name_cap bytes), its base
 * to *base_out and returns 1; else returns 0. Weak default in nx_crash_handler.c returns 0. */
int crash_resolve_module(uintptr_t addr, char *name_out, size_t name_cap, uintptr_t *base_out);

#endif
