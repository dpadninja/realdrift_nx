/* rd_gldebug_gl.c -- see rd_gldebug_gl.h. MIT (see LICENSE). */
#include <stdio.h>
#include <switch.h>
#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include "rd_codeaddr.h"
#include "rd_gldebug.h"
#include "rd_gldebug_gl.h"
#include "so_util.h"
#include "util.h"

#define CALLERS     3          /* engine return addresses per line */
#define SCAN_BYTES  0x4000     /* stack scanned above the callback's frame */

static Mutex g_lock;           /* the counts: the callback runs on whichever thread calls GL */
static rd_gldebug_seen g_seen;

/* A return address into a loaded game module (libunity, libil2cpp, libmain), or NULL. */
static so_module *game_code(uint64_t v) {
  so_module *m = so_find_module_by_addr((const void *)(uintptr_t)v);
  if (!m || !rd_is_code_return_addr(v, (uint64_t)(uintptr_t)m->load_virtbase, m->phdr, m->phnum))
    return NULL;
  return m;
}

/* Fills out[0..CALLERS) with "libunity.so+0x..." for the first return addresses into game code
 * on this thread's stack, above `sp`, within its memory block; returns the first address. */
static uint64_t find_callers(const uint64_t *sp, char *out, size_t cap) {
  MemoryInfo mi;
  u32 pi;
  out[0] = 0;
  if (R_FAILED(svcQueryMemory(&mi, &pi, (u64)(uintptr_t)sp))) return 0;
  const uint64_t *end = (const uint64_t *)(uintptr_t)(mi.addr + mi.size);
  if ((uintptr_t)end > (uintptr_t)sp + SCAN_BYTES) end = sp + SCAN_BYTES / sizeof *sp;
  uint64_t first = 0;
  size_t len = 0;
  int found = 0;
  for (const uint64_t *p = sp; p < end && found < CALLERS; p++) {
    so_module *m = game_code(*p);
    if (!m) continue;
    const char *base = m->name;
    for (const char *c = m->name; *c; c++) if (*c == '/') base = c + 1;
    const int n = snprintf(out + len, cap - len, " %s+0x%llx", base,
                           (unsigned long long)(*p - (uint64_t)(uintptr_t)m->load_virtbase));
    if (n < 0 || (size_t)n >= cap - len) break;
    len += (size_t)n;
    if (!found++) first = *p;
  }
  return first;
}

static void GL_APIENTRY on_message(GLenum source, GLenum type, GLuint id, GLenum severity,
                                   GLsizei length, const GLchar *message, const void *user) {
  (void)source; (void)severity; (void)length; (void)user;
  if (!rd_gldebug_wanted(type)) return;
  char callers[160];
  const uint64_t first =
      find_callers((const uint64_t *)__builtin_frame_address(0), callers, sizeof callers);
  mutexLock(&g_lock);
  const uint32_t n = rd_gldebug_count(&g_seen, id, first);
  mutexUnlock(&g_lock);
  if (rd_gldebug_logged(n))
    debugPrintf("[mem] GL debug %s id %u (x%u): %.200s |%s\n",
                type == RD_GL_DEBUG_TYPE_ERROR ? "error" : "undefined behaviour", (unsigned)id,
                (unsigned)n, message ? message : "", callers[0] ? callers : " no caller found");
}

void rd_gldebug_install(void) {
  static int done;
  if (done) return;
  done = 1;
  PFNGLDEBUGMESSAGECALLBACKPROC set =
      (PFNGLDEBUGMESSAGECALLBACKPROC)eglGetProcAddress("glDebugMessageCallback");
  if (!set) set = (PFNGLDEBUGMESSAGECALLBACKPROC)eglGetProcAddress("glDebugMessageCallbackKHR");
  if (!set) {
    debugPrintf("[mem] GL debug output: no glDebugMessageCallback\n");
    return;
  }
  glEnable(GL_DEBUG_OUTPUT);
  glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
  set(on_message, NULL);
  debugPrintf("[mem] GL debug output on (synchronous): errors logged by the call that raised "
              "them, err 0x%x\n", (unsigned)glGetError());
}
