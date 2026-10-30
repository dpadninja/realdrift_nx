/* rd_aniso.c -- see rd_aniso.h. MIT (see LICENSE). */
#include "rd_aniso.h"

int rd_aniso_valid(int level) {
  return level == RD_ANISO_OFF || level == 2 || level == 4 || level == 8 || level == 16;
}

int rd_aniso_after(uint32_t pname, int32_t value) {
  return pname == RD_GL_TEXTURE_MIN_FILTER && value >= RD_GL_NEAREST_MIPMAP_NEAREST &&
         value <= RD_GL_LINEAR_MIPMAP_LINEAR;
}

float rd_aniso_amount(float asked, int level, float max) {
  float a = asked > (float)level ? asked : (float)level;
  return max > 1.0f && a > max ? max : a;
}
