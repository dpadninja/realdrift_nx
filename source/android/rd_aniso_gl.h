/* rd_aniso_gl.h -- config.txt anisotropic_filtering: GL glue (decisions in rd_aniso.h).
 *
 * Unity sets sampling through sampler objects (glSamplerParameteri) and texture parameters
 * (glTexParameteri / glTexParameterf). When one of them sets a mipmapped minification filter,
 * the wrapper also sets GL_TEXTURE_MAX_ANISOTROPY_EXT to the configured level; when the engine
 * sets the anisotropy itself, it is raised to at least that level. Capped at the GPU's
 * GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, read once. GL thread.
 * No host test: GL glue; the decisions are rd_aniso (tested). MIT (see LICENSE). */
#ifndef RD_ANISO_GL_H
#define RD_ANISO_GL_H

/* For egl_wrap_gl: the anisotropy wrapper of GL function `name` (remembering `real`, which may
 * itself be a wrapper), or NULL when the function is not one it wraps. */
void *rd_aniso_wrap(const char *name, void *real);

#endif
