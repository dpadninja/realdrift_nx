/* rd_apkpath.h -- what Context.getPackageCodePath() returns. Host-testable
 * (tests/c/test_rd_apkpath.c).
 *
 * Without a mounted OBB, Unity reads assets/bin/Data/... as plain files relative to the
 * game folder, so the inherited loader returned the folder itself. Once the OBB is
 * mounted (unity.build-id matches) Unity switches to archive mode and opens the package
 * path as a ZIP: globalgamemanagers / level0 / sharedassets0 live only in the APK, so a
 * folder there fails with "No GlobalGameManagers file was found" (run 05). We therefore
 * ship the original APK as <game folder>/base.apk and report that path when present. */
#ifndef RD_APKPATH_H
#define RD_APKPATH_H

#include <stddef.h>
#include "rd_game.h"   /* RD_BASE_APK_NAME */


/* Writes "<root>/base.apk" to buf if exists() says it is there, else `root`.
 * Returns buf, or NULL if buf is too small. */
const char *rd_package_code_path(const char *root, int (*exists)(const char *path),
                                 char *buf, size_t n);

#endif
