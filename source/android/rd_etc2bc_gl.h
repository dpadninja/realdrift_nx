/* rd_etc2bc_gl.h -- config.txt texture_transcode: the engine's ETC2 textures are stored as
 * S3TC (rd_etc2bc.h) so mesa does not decompress them to RGBA8.
 *
 * glTexStorage2D with an ETC format allocates the S3TC format instead; glCompressedTexImage2D
 * and glCompressedTexSubImage2D with ETC data re-encode it (on rd_jobs.h workers for big
 * images) and upload S3TC, the latter only when the texture's storage is S3TC (read back with
 * glGetTexLevelParameteriv), so textures made before or by others are left alone. Data in a
 * bound GL_PIXEL_UNPACK_BUFFER is read through glMapBufferRange. GL thread.
 * MIT (see LICENSE). */
#ifndef RD_ETC2BC_GL_H
#define RD_ETC2BC_GL_H

#include <stdint.h>

/* For egl_wrap_gl: the re-encoding wrapper of GL function `name` (remembering `real`, which
 * may itself be a wrapper), or NULL when the function is not one of the three. */
void *rd_etc2bc_wrap(const char *name, void *real);

/* Totals so far: uploads re-encoded, 4x4 blocks and the time it took (ns). Any thread. */
void rd_etc2bc_stats(uint32_t *uploads, uint64_t *blocks, uint64_t *ns);

#endif
