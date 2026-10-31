/* rd_gldebug_gl.h -- config.txt debug_mem: mesa's debug output (KHR_debug) on, so every GL
 * error is logged by the call that raised it, with mesa's message and the engine's return
 * addresses found on the stack ("[mem] GL debug ..."). The decisions are rd_gldebug.h (tested);
 * this is the GL glue. MIT (see LICENSE). */
#ifndef RD_GLDEBUG_GL_H
#define RD_GLDEBUG_GL_H

/* GL thread, context current, once (later calls do nothing): enables synchronous debug output
 * and installs the callback. Logs whether it worked. */
void rd_gldebug_install(void);

#endif
