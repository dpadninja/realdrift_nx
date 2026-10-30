/* rd_aniso_gl.c -- see rd_aniso_gl.h. MIT (see LICENSE). */
#include <string.h>
#include <GLES3/gl31.h>
#include "rd_aniso.h"
#include "rd_aniso_gl.h"
#include "config.h"
#include "util.h"

static void (*real_tex_i)(GLenum, GLenum, GLint);
static void (*real_tex_f)(GLenum, GLenum, GLfloat);
static void (*real_sampler_i)(GLuint, GLenum, GLint);

/* The GPU's limit, read on first use (GL thread, context current); logged once. */
static float gpu_max(void) {
  static float max = -1.0f;
  if (max < 0.0f) {
    max = 0.0f;
    glGetFloatv(RD_GL_MAX_TEXTURE_MAX_ANISOTROPY, &max);
    debugPrintf("[aniso] anisotropic filtering %dx (GPU max %.0fx)\n",
                config.anisotropic_filtering, (double)max);
  }
  return max;
}

static float amount(float asked) {
  return rd_aniso_amount(asked, config.anisotropic_filtering, gpu_max());
}

static void w_tex_i(GLenum target, GLenum pname, GLint value) {
  if (pname == RD_GL_TEXTURE_MAX_ANISOTROPY) {
    glTexParameterf(target, pname, amount((float)value));
    return;
  }
  real_tex_i(target, pname, value);
  if (rd_aniso_after(pname, value))
    glTexParameterf(target, RD_GL_TEXTURE_MAX_ANISOTROPY, amount(1.0f));
}

static void w_tex_f(GLenum target, GLenum pname, GLfloat value) {
  if (pname == RD_GL_TEXTURE_MAX_ANISOTROPY) value = amount(value);
  real_tex_f(target, pname, value);
  if (rd_aniso_after(pname, (int32_t)value))
    glTexParameterf(target, RD_GL_TEXTURE_MAX_ANISOTROPY, amount(1.0f));
}

static void w_sampler_i(GLuint sampler, GLenum pname, GLint value) {
  if (pname == RD_GL_TEXTURE_MAX_ANISOTROPY) {
    glSamplerParameterf(sampler, pname, amount((float)value));
    return;
  }
  real_sampler_i(sampler, pname, value);
  if (rd_aniso_after(pname, value))
    glSamplerParameterf(sampler, RD_GL_TEXTURE_MAX_ANISOTROPY, amount(1.0f));
}

void *rd_aniso_wrap(const char *name, void *real) {
#define WRAP(gl, var, w) if (!strcmp(name, gl)) { var = real; return (void *)&w; }
  WRAP("glTexParameteri", real_tex_i, w_tex_i)
  WRAP("glTexParameterf", real_tex_f, w_tex_f)
  WRAP("glSamplerParameteri", real_sampler_i, w_sampler_i)
#undef WRAP
  return NULL;
}
