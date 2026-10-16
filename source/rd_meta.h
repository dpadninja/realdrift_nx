/* rd_meta.h -- AndroidManifest <meta-data> values served to the engine through
 * ApplicationInfo.metaData (android.os.Bundle). Host-testable (tests/c/test_rd_meta.c).
 *
 * Unity 2018.4 reads "unity.build-id" and only mounts an OBB that contains the marker
 * entry assets/<build-id>. Without it the engine logs "build <unknown>", ignores the OBB,
 * and everything stored there (Resources, shaders, levels 1..N) silently fails to load. */
#ifndef RD_META_H
#define RD_META_H

/* Value for `key`, or NULL if the manifest has no such meta-data. */
const char *rd_meta_string(const char *key);

/* Bundle.containsKey(key). */
int rd_meta_has(const char *key);

/* 1 if the JNI object field (name, sig) is ApplicationInfo/PackageItemInfo.metaData and
 * must be a live Bundle. Returning null there made Unity skip the build-id lookup. */
int rd_meta_is_bundle_field(const char *name, const char *sig);

#endif
