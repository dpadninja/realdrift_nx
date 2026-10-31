/* rd_gldebug.c -- see rd_gldebug.h. MIT (see LICENSE). */
#include "rd_gldebug.h"

int rd_gldebug_wanted(uint32_t type) {
  return type == RD_GL_DEBUG_TYPE_ERROR || type == RD_GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR;
}

uint32_t rd_gldebug_count(rd_gldebug_seen *s, uint32_t id, uint64_t caller) {
  const uint64_t key = (caller ^ ((uint64_t)id << 48)) | 1;   /* never 0 = an empty slot */
  uint32_t i = (uint32_t)((key * 0x9E3779B97F4A7C15ull) >> 57) % RD_GLDEBUG_SLOTS;
  for (int probe = 0; probe < RD_GLDEBUG_SLOTS; probe++, i = (i + 1) % RD_GLDEBUG_SLOTS) {
    if (s->key[i] == key) return ++s->count[i];
    if (!s->key[i]) {
      s->key[i] = key;
      return s->count[i] = 1;
    }
  }
  return ++s->overflow;
}

int rd_gldebug_logged(uint32_t n) {
  return n == 1 || (n && n % RD_GLDEBUG_EVERY == 0);
}
