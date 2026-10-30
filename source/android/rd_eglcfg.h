/* rd_eglcfg.h -- EGL attribute lists for config.txt antialiasing: the engine's eglChooseConfig
 * request rewritten to ask for a multisampled window (egl.c). Pure, host-testable
 * (tests/c/test_rd_eglcfg.c). MIT (see LICENSE). */
#ifndef RD_EGLCFG_H
#define RD_EGLCFG_H

#include <stddef.h>
#include <stdint.h>

#define RD_EGL_NONE            0x3038
#define RD_EGL_SAMPLES         0x3031
#define RD_EGL_SAMPLE_BUFFERS  0x3032
#define RD_EGL_ATTRIBS_MAX     64     /* words in a rewritten list, EGL_NONE included */

/* Value of attribute `name` in the EGL_NONE-terminated list (pairs), `dflt` if absent or the
 * list is NULL. */
int32_t rd_egl_attrib(const int32_t *list, int32_t name, int32_t dflt);

/* Copies the list `in` (NULL = empty) to out[0..cap) with EGL_SAMPLE_BUFFERS = 1 and
 * EGL_SAMPLES = `samples` (replaced where present, appended where not), terminated by
 * EGL_NONE. Returns the words written, EGL_NONE included, or 0 if they do not fit. */
size_t rd_egl_attribs_samples(const int32_t *in, int32_t *out, size_t cap, int32_t samples);

#endif
