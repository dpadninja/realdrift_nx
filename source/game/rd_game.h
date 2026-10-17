/* rd_game.h -- THE place for what identifies the supported game build and where its files
 * live. Everything else (C and tools/game.py) takes these values from here.
 *
 * Target: Real Drift (full version) 5.0.8, versionCode 76, Unity 2018.4.14f1 / IL2CPP,
 * arm64-v8a. Values from the APK's AndroidManifest.xml; tools/stage_sd.py refuses any other
 * APK (e.g. Real Drift Lite, com.realdriftportnew.sipon). History: analysis/research/10-patch-sites.md.
 * MIT (see LICENSE). */
#ifndef RD_GAME_H
#define RD_GAME_H

#define RD_STR_(x) #x
#define RD_STR(x)  RD_STR_(x)
/* RD_STR of a numeric macro, as an expression. Unlike RD_STR, an undefined (e.g. not
 * included) macro is a compile error instead of silently becoming its own name. */
#define RD_NUM_STR(x) ((void)(x), RD_STR(x))

/* ---- game build ---------------------------------------------------------------------- */
#define RD_GAME_PACKAGE        "com.realdrift.sipon"
#define RD_GAME_VERSION_NAME   "5.0.8"
#define RD_GAME_VERSION_CODE   76
/* AndroidManifest meta-data "unity.build-id". libunity mounts the OBB only if it contains the
 * marker entry assets/<build-id> (rd_meta.c serves this value to Unity). */
#define RD_GAME_UNITY_BUILD_ID "a716b65c-2cb0-47ca-ab2b-f42b256c4e69"
#define RD_UNITY_VERSION       "2018.4.14f1"

/* ---- files on the SD card ------------------------------------------------------------- */
/* The game folder == the .nro's folder. Unity sees it without the "sdmc:" device prefix
 * (managed_path in libc_shim.c), so keep it absolute. */
#define RD_GAME_HOME_DIR       "/switch/realdrift_nx"
#define RD_GAME_HOME           "sdmc:" RD_GAME_HOME_DIR

/* Copied from the user's game files by tools/stage_sd.py: */
#define RD_LIB_MAIN            "libmain.so"
#define RD_LIB_UNITY           "libunity.so"
#define RD_LIB_IL2CPP          "libil2cpp.so"
#define RD_BASE_APK_NAME       "base.apk"     /* the APK itself (Unity + AssetManager read it as ZIP) */
/* getObbDir()/main.<versionCode>.<package>.obb, getObbDir() == RD_GAME_HOME */
#define RD_GAME_OBB_NAME       "main." RD_STR(RD_GAME_VERSION_CODE) "." RD_GAME_PACKAGE ".obb"

/* Written by the loader / the game at run time: */
#define RD_CONFIG_FILE         "config.txt"   /* settings (rd_settings.c) */
#define RD_PREFS_FILE          "prefs.kv"     /* PlayerPrefs store (unity_jni.c) */
#define RD_LOG_FILE            "debug.log"
#define RD_LOG_PATH            RD_GAME_HOME "/" RD_LOG_FILE

#endif
