/* rd_device.h -- the Android device the port pretends to be. Reported through Build.* fields
 * (jni_fake.c), system properties (__system_property_get, libc_shim.c), sysconf and the
 * synthetic /proc and /sys files (libc_shim.c). Keep them consistent: Unity cross-reads them.
 * Nothing here is checked by Real Drift; the values only steer engine decisions.
 * MIT (see LICENSE). */
#ifndef RD_DEVICE_H
#define RD_DEVICE_H

#include "rd_game.h"   /* RD_STR */

#define RD_DEV_SDK_INT        33              /* Android 13: above any minSdk gate */
#define RD_DEV_RELEASE        "13"
#define RD_DEV_INCREMENTAL    "1"
#define RD_DEV_SECURITY_PATCH "2023-01-01"
#define RD_DEV_ABI            "arm64-v8a"
#define RD_DEV_FINGERPRINT    "Nintendo/Switch/Switch:" RD_DEV_RELEASE "/REL/" RD_DEV_INCREMENTAL \
                              ":user/release-keys"

/* Locale (java.util.Locale). Real Drift is not localized, so it is fixed: en_US also keeps
 * culture-dependent number formatting free of decimal commas. */
#define RD_DEV_LANGUAGE       "en"
#define RD_DEV_COUNTRY        "US"
#define RD_DEV_ISO3_LANGUAGE  "eng"
#define RD_DEV_ISO3_COUNTRY   "USA"
#define RD_DEV_LOCALE         RD_DEV_LANGUAGE "_" RD_DEV_COUNTRY

/* Homebrew gets CPU cores 0..2. */
#define RD_DEV_CPU_CORES      3
#define RD_DEV_CPU_MAX_KHZ    "1785000"       /* the Switch's normal CPU clock */
#define RD_DEV_CPU_MIN_KHZ    "1020000"

/* RAM reported to Unity (/proc/meminfo, sysconf(_SC_PHYS_PAGES)). Deliberately small: Unity
 * sizes its up-front 256 MB heap-region reservations from it; the real backing is the mmap
 * arena (libc_shim.c), which has more headroom than this. */
#define RD_DEV_MEM_KB         524288          /* 512 MB */
#define RD_DEV_MEM_FREE_KB    393216          /* 384 MB */
#define RD_DEV_PAGE_SIZE      0x1000

#endif
