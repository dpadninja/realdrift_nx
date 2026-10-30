/* rd_sleek.c -- see rd_sleek.h. MIT (see LICENSE). */
#include "rd_sleek.h"

int rd_sleek_set_flag(uint8_t *flag, int want) {
  const uint8_t v = want ? 1 : 0;
  if (*flag == v) return 0;
  *flag = v;
  return 1;
}
