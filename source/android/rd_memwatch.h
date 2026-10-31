/* rd_memwatch.h -- config.txt debug_mem: "[mem]" lines with where the memory is (every ~10 s
 * and after each load, rd_memstat.h) and a check of glGetError after every GL call that
 * allocates GPU memory (textures, buffers, renderbuffers), so a texture that could not be
 * allocated -- it samples black -- shows up in debug.log with its size and the memory at that
 * moment. Only with debug_mem = 1: the wrappers are handed out by egl_wrap_gl.
 * glGetError reports everything since the last check, so an error may come from an earlier
 * call, and the wrappers consume it (the engine does not read GL errors in a release build).
 * rd_gldebug_gl.h names the call that raised it.
 * Each "[mem]" line is followed by "[mem] GL objects: ..." (rd_memstat_format_globj): texture
 * and buffer names generated / deleted and the nvmap memory alive, to tell a leak of GL objects
 * from mesa not returning memory.
 * No host test: mallinfo / svc / GL glue; the report is rd_memstat (tested).
 * MIT (see LICENSE). */
#ifndef RD_MEMWATCH_H
#define RD_MEMWATCH_H

#include <stdint.h>
#include "rd_memstat.h"

/* Fills the managed-heap part of a snapshot (gc_heap, gc_used, gc_state). */
typedef void (*rd_memwatch_gc_fn)(rd_memstat *s);

/* Main thread, once the il2cpp API is resolved (main.c; the engine layer is above this one):
 * the function that adds the managed heap to every "[mem]" line. */
void rd_memwatch_set_gc(rd_memwatch_gc_fn fn);

/* Logs one "[mem] (<why>) ..." line. Any thread. */
void rd_memwatch_log(const char *why);

/* Main loop, after each frame (frame_ns = how long it took): logs when rd_memstat_due says so. */
void rd_memwatch_frame(uint32_t frame, uint64_t frame_ns);

/* GL thread, once, at the first eglSwapBuffers: logs what one 1024x1024 ETC2 texture costs on
 * the heap ("[mem] probe ..."), i.e. whether mesa keeps ETC2 compressed (the Switch's GPU has
 * no ETC2: mesa may decompress it to RGBA8, 4-8x the size). Restores the bindings it uses. */
void rd_memwatch_probe_once(void);

/* Failures of memalign, nvMapCreate and nvAddressSpaceMap (wrapped at link time) are logged as
 * "[mem] <what> FAILED" with the memory; GL turns any of them into GL_OUT_OF_MEMORY. */

/* For egl_wrap_gl: the checking wrapper of GL function `name` (remembering `real`), or NULL
 * when the function does not allocate. */
void *rd_memwatch_wrap(const char *name, void *real);

#endif
