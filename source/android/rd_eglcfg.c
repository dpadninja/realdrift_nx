/* rd_eglcfg.c -- see rd_eglcfg.h. MIT (see LICENSE). */
#include "rd_eglcfg.h"

int32_t rd_egl_attrib(const int32_t *list, int32_t name, int32_t dflt) {
  for (; list && list[0] != RD_EGL_NONE; list += 2)
    if (list[0] == name) return list[1];
  return dflt;
}

size_t rd_egl_attribs_samples(const int32_t *in, int32_t *out, size_t cap, int32_t samples) {
  size_t n = 0;
  for (; in && in[0] != RD_EGL_NONE; in += 2) {
    if (in[0] == RD_EGL_SAMPLES || in[0] == RD_EGL_SAMPLE_BUFFERS) continue;
    if (n + 2 > cap) return 0;
    out[n++] = in[0];
    out[n++] = in[1];
  }
  if (n + 5 > cap) return 0;
  out[n++] = RD_EGL_SAMPLE_BUFFERS;
  out[n++] = 1;
  out[n++] = RD_EGL_SAMPLES;
  out[n++] = samples;
  out[n++] = RD_EGL_NONE;
  return n;
}
