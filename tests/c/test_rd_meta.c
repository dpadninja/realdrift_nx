/* sources: source/android/rd_meta.c */
/* Host unit test of rd_meta. MIT (see LICENSE). */
#include <string.h>
#include "check.h"
#include "config.h"
#include "rd_meta.h"

static void test_build_id_is_served(void) {
  CHECK(rd_meta_has("unity.build-id"));
  CHECK(strcmp(rd_meta_string("unity.build-id"), RD_GAME_UNITY_BUILD_ID) == 0);
  CHECK(strlen(RD_GAME_UNITY_BUILD_ID) == 36);   /* canonical GUID text */
}

static void test_unknown_keys(void) {
  CHECK(!rd_meta_has("unity.splash-mode"));
  CHECK(rd_meta_string("unity.build-idX") == NULL);
  CHECK(rd_meta_string("") == NULL);
  CHECK(rd_meta_string(NULL) == NULL);
  CHECK(!rd_meta_has(NULL));
}

static void test_metadata_field_is_bundle(void) {
  CHECK(rd_meta_is_bundle_field("metaData", "Landroid/os/Bundle;"));
  CHECK(!rd_meta_is_bundle_field("metaData", "Ljava/lang/String;"));
  CHECK(!rd_meta_is_bundle_field("extras", "Landroid/os/Bundle;"));
  CHECK(!rd_meta_is_bundle_field(NULL, "Landroid/os/Bundle;"));
  CHECK(!rd_meta_is_bundle_field("metaData", NULL));
}

int main(void) {
  RUN(test_metadata_field_is_bundle);
  RUN(test_build_id_is_served);
  RUN(test_unknown_keys);
  return g_failures != 0;
}
