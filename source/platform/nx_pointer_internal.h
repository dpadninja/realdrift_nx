/* nx_pointer_internal.h -- state nx_pointer.c shares with nx_pointer_draw.c (platform layer,
 * internal).
 *
 *   nx_pointer.c       configuration, input (pad, touch, mouse, gyro), events, settings file
 *   nx_pointer_draw.c  the cursor: cursor.png and the built-in arrow, drawn through rd_overlay
 * Written by the input code on the main thread; read by the draw code on the render thread
 * (the same thread in this port). MIT (see LICENSE). */
#ifndef NX_POINTER_INTERNAL_H
#define NX_POINTER_INTERNAL_H

#include <stdio.h>
#include "nx_pointer.h"

/* The configuration passed to nxp_init (with defaults filled in), and whether it ran. */
extern NxpConfig nxp_cfg;
extern int       nxp_ready;
/* Cursor position in render pixels (top-left origin) and visibility (-1 = not decided yet). */
extern float nxp_cx, nxp_cy;
extern int   nxp_visible;

/* printf to the configured log callback (no-op without one). */
void  nxp_logf(const char *fmt, ...);
/* fopen/fclose through the port's locked wrappers when configured (newlib's handle table is not
 * thread-safe), else plain stdio. */
FILE *nxp_fopen(const char *path, const char *mode);
int   nxp_fclose(FILE *f);
/* Reads <data_dir>/cursor.png into memory at init, so the render thread never touches files;
 * decoding and upload happen in nxp_draw. */
void  nxp_slurp_cursor_png(void);

#endif
