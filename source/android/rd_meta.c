/* rd_meta.c -- see rd_meta.h. MIT (see LICENSE). */
#include <string.h>
#include "config.h"
#include "rd_meta.h"

const char *rd_meta_string(const char *key) {
  if (key && !strcmp(key, "unity.build-id")) return RD_GAME_UNITY_BUILD_ID;
  return NULL;
}

int rd_meta_has(const char *key) {
  return rd_meta_string(key) != NULL;
}

int rd_meta_is_bundle_field(const char *name, const char *sig) {
  return name && sig && !strcmp(name, "metaData") && !strcmp(sig, "Landroid/os/Bundle;");
}
