/* rd_aniso.h -- config.txt anisotropic_filtering: when to add anisotropy to the engine's texture
 * sampling (rd_aniso_gl.c). The game runs on quality level "Fastest", which has anisotropic
 * textures disabled, so a road seen at a grazing angle samples a small mip level and turns to
 * mush in the distance (analysis/research/34). Pure, host-testable (tests/c/test_rd_aniso.c).
 * MIT (see LICENSE). */
#ifndef RD_ANISO_H
#define RD_ANISO_H

#include <stdint.h>

#define RD_GL_TEXTURE_MIN_FILTER         0x2801
#define RD_GL_NEAREST_MIPMAP_NEAREST     0x2700   /* the four mipmapped min filters: 0x2700..3 */
#define RD_GL_LINEAR_MIPMAP_LINEAR       0x2703
#define RD_GL_TEXTURE_MAX_ANISOTROPY     0x84FE   /* EXT_texture_filter_anisotropic */
#define RD_GL_MAX_TEXTURE_MAX_ANISOTROPY 0x84FF

#define RD_ANISO_OFF 0   /* config value: the engine's own sampling */

/* 1 if `level` is a config value: RD_ANISO_OFF, 2, 4, 8 or 16. */
int rd_aniso_valid(int level);

/* 1 if a texture/sampler parameter call (pname, value) sets a mipmapped minification filter:
 * only then does anisotropy apply, so the wrapper adds it right after. */
int rd_aniso_after(uint32_t pname, int32_t value);

/* The anisotropy to set: the larger of what the engine asked and `level`, capped at `max` (the
 * GPU's limit; <= 1 = unknown, no cap). */
float rd_aniso_amount(float asked, int level, float max);

#endif
