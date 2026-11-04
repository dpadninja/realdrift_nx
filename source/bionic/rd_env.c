/* rd_env.c -- see rd_env.h. MIT (see LICENSE). */
#include <stddef.h>
#include <string.h>
#include "rd_env.h"

static const struct {
  const char *name;
  const char *value;
} k_fixed[] = {
  { RD_ENV_GC_UNMAP_THRESHOLD, RD_ENV_GC_NEVER_UNMAP },
};

const char *rd_env_fixed(const char *name) {
  if (!name) return NULL;
  for (size_t i = 0; i < sizeof k_fixed / sizeof k_fixed[0]; i++)
    if (!strcmp(name, k_fixed[i].name)) return k_fixed[i].value;
  return NULL;
}
