/* jni_framework.c -- what the fake Android framework answers: return values of Java methods
 * the engine calls on framework classes (Activity, Context, Display, Build, ...) and of the
 * Java fields it reads. Classes with real behaviour live in unity_jni.c.
 * Split out of jni_fake.c, forked from badpiggies_nx by ChanseyIsTheBest
 * (https://github.com/ChanseyIsTheBest/badpiggies_nx, commit 7726be3); changed for realdrift_nx.
 * MIT (see LICENSE). */

#include <stdlib.h>

#include "rd_log.h"
#include "rd_device.h"
#include "opensles.h"       /* RD_AUDIO_* */
#include "util.h"
#include "jni_fake.h"
#include "android_native_unity.h"
#include "libc_shim.h"     /* managed_path */
#include "rd_meta.h"
#include "jni_internal.h"

// --- MyNativeActivity / general activity ------------------------------------


// Walk a JNI arg list per the signature and return the first non-empty String
// argument's text (used to seed the keyboard from ShowEditBox's initial text).
static const char *first_string_arg(const char *sig, va_list va) {
  const char *p = sig ? strchr(sig, '(') : NULL;
  if (!p) return "";
  for (p++; *p && *p != ')'; p++) {
    switch (*p) {
      case 'I': case 'Z': case 'B': case 'C': case 'S': (void)va_arg(va, int); break;
      case 'F': case 'D': (void)va_arg(va, double); break;
      case 'J': (void)va_arg(va, long long); break;
      case '[':
        (void)va_arg(va, void *);
        if (p[1] == 'L') { p++; while (*p && *p != ';') p++; } else if (p[1]) p++;
        break;
      case 'L': {
        const char *s = obj_str(va_arg(va, void *));
        while (*p && *p != ';') p++;
        if (s && s[0]) return s;
        break;
      }
      default: break;
    }
  }
  return "";
}


/* jni_string_utf is defined far below (shared with unity_jni.c) and unity_jni.h
 * is included after this point, so forward-declare it for act_object's
 * getProperty()/locale arg reads. */
const char *jni_string_utf(void *jstr);

/* AudioManager.getProperty(PROPERTY_OUTPUT_SAMPLE_RATE / FRAMES_PER_BUFFER): FMOD reads these to
 * size its OpenSL output. The key string doesn't reach us (arrives "") and the two properties need
 * opposite answers -- rate must be 48000, frames-per-buffer must be small (48000 there fails init
 * with error 60). FMOD retries the same output with 3 getProperty calls per attempt, so we hedge:
 * attempt 1 (calls 0-2) treats the 3rd call as the buffer query, attempt 2 (calls 3-5) the 2nd;
 * whichever is right, one attempt sees rate=48000 + fpb=256 and inits cleanly. */
static int g_last_output_prop = 0;   /* set by field_object from the static-field read, if it reaches us */

/* AudioManager.getProperty(PROPERTY_OUTPUT_SAMPLE_RATE / FRAMES_PER_BUFFER).
 * FMOD (Real Drift's audio) reads these when it sets up its OpenSL output. When the key string
 * reaches us the SAMPLE_RATE/FRAMES_PER_BUFFER branches below select directly; the call_n hedge
 * is a fallback for the keyless pattern. We report RD_AUDIO_REPORTED_RATE /
 * RD_AUDIO_FRAMES_PER_BUFFER (opensles.h), values proven in an earlier FMOD port. The reported
 * rate is effectively cosmetic: opensles.c opens the SDL device at the Switch's native 48000
 * and resamples every player, so the game's mixer rate is resampled regardless. FRAMES_PER_BUFFER
 * must stay small and nonzero: 0/"" fails OpenSL init with error 60. FMOD picks OpenSL ES
 * because hasSystemFeature("android.hardware.audio.low_latency") is true (rd_features.h). */
void *getproperty_value(const char *key) {
  static int call_n = 0;
  int which;
  if (key && strstr(key, "SAMPLE_RATE"))            which = 1;
  else if (key && strstr(key, "FRAMES_PER_BUFFER")) which = 2;
  else if (g_last_output_prop)                      which = g_last_output_prop;
  else                                              which = (call_n == 2 || call_n == 4) ? 2 : 1;
  call_n++;
  const char *val = (which == 2) ? RD_NUM_STR(RD_AUDIO_FRAMES_PER_BUFFER)
                                : RD_NUM_STR(RD_AUDIO_REPORTED_RATE);
  /* Never hand back "" here: FMOD parses an empty PROPERTY_OUTPUT_FRAMES_PER_BUFFER as
   * framesPerBuffer == 0 and fails its OpenSL init with error 60 (an earlier port hit
   * exactly that). Log the first few so the boot log shows the audio config is queried. */
  if (call_n <= 6)
    debugPrintf(RD_LOG_TAG_JNI " getProperty(%s) -> %s\n", key ? key : "(null)", val);
  return jni_make_string(val);
}

void *act_object(const FakeID *id, va_list va) {
  /* FRAME PACING -- report the Android Choreographer as UNAVAILABLE (return null).
   * On Android, Choreographer.getInstance() + postFrameCallback drives Unity's
   * per-frame vsync wait inside nativeRender (the "UnityChoreographer" object at
   * libunity 0x628fd4). With no Java Choreographer we can never deliver the vsync
   * frame callback, so that wait blocks FOREVER on the first real render frame --
   * the frame-2 black screen. A null Choreographer makes the engine log
   * "Choreographer NOT available" and take its non-vsync fallback (render as fast
   * as the loop drives it), which unblocks nativeRender. Also covers the Swappy
   * SwappyDisplayManager/ChoreographerCallback helpers. Must precede the generic
   * non-null object handlers below. */
  if ((name_has(id->cls, "Choreographer") || name_has(id->cls, "SwappyDisplayManager")) &&
      (name_has(id->name, "getInstance") || sig_returns(id->sig, "Landroid/view/Choreographer;")))
    return NULL;
  /* Uri.encode/decode: Unity round-trips PlayerPrefs keys through these. We are
   * the storage, so identity (return the input string) round-trips correctly and
   * keeps keys non-empty. Must precede the generic handlers. */
  if (name_has(id->cls, "net/Uri") && (name_has(id->name, "encode") || name_has(id->name, "decode")))
    /* Return a COPY. Returning the argument itself was a use-after-free:
     * Unity DeleteLocalRef()s the input right after encode(), and once the 512-entry string
     * pool is full the input is a freeable local -> the "encoded" key/value read back as ""
     * (empty PlayerPrefs keys + empty stored values -> FormatException, no car; run 10). */
    return jni_make_string(jni_string_utf(va_arg(va, void *)));
  if (name_has(id->name, "AssetManager") || sig_returns(id->sig, "Landroid/content/res/AssetManager;"))
    return get_asset_manager_obj();
  if (name_has(id->name, "ClassLoader") || sig_returns(id->sig, "Ljava/lang/ClassLoader;"))
    return get_classloader_obj();
  if (sig_returns(id->sig, "Ljava/lang/Class;"))
    return intern_class("java/lang/Object");
  // version / package / device / storage strings
  if (name_has(id->name, "VersionName")) return jni_make_string(RD_GAME_VERSION_NAME);
  if (name_has(id->name, "PackageName")) return jni_make_string(RD_GAME_PACKAGE);
  if (name_has(id->name, "DeviceModel")) return jni_make_string("Switch");
  if (name_has(id->name, "getProperty"))
    return getproperty_value(jni_string_utf(va_arg(va, void *)));
  if (name_has(id->name, "Language") || name_has(id->name, "language"))
    return jni_make_string(RD_DEV_LANGUAGE);   /* Locale.getLanguage(): language subtag only */
  // Environment.getExternalStorageState() must return the SAME token as the
  // Environment.MEDIA_MOUNTED field ("mounted", see field_object) or the engine
  // decides external storage is unavailable and the save path never initialises.
  if (name_has(id->cls, "os/Environment")) {
    if (name_has(id->name, "ExternalStorageState")) return jni_make_string("mounted");
    if (name_has(id->name, "Directory")) return jni_make_object("java/io/File"); /* ->getAbsolutePath */
  }
  // Locale.getCountry/getISO3*/toString/getDisplayName, guarded by the Locale class so
  // toString()/getCountry on other objects are not hijacked. Fixed locale: rd_device.h.
  if (name_has(id->cls, "Locale")) {
    if (!strcmp(id->name, "getCountry"))      return jni_make_string(RD_DEV_COUNTRY);
    if (!strcmp(id->name, "getISO3Language")) return jni_make_string(RD_DEV_ISO3_LANGUAGE);
    if (!strcmp(id->name, "getISO3Country"))  return jni_make_string(RD_DEV_ISO3_COUNTRY);
    if (!strcmp(id->name, "toString") || name_has(id->name, "getDisplayName") ||
        name_has(id->name, "getDisplayLanguage"))
      return jni_make_string(RD_DEV_LOCALE);
  }
  if (name_has(id->name, "DataPath") || name_has(id->name, "StoragePath") ||
      name_has(id->name, "FilesDir") || name_has(id->name, "RootPath") ||
      name_has(id->name, "ObbDir") || name_has(id->name, "AssetPath") ||
      name_has(id->name, "Path"))
    return jni_make_string(managed_path(RD_GAME_HOME));
  // Android object getters that must NOT be null, or the engine aborts the
  // chain. getPackageInfo()/getApplicationInfo() are how Unity reaches
  // PackageInfo.versionName/versionCode (-> Application.version); returning null
  // here is why the version stayed blank even with field access fixed -- Unity
  // got a null PackageInfo and never read the field. Hand back live (opaque)
  // objects; the subsequent field reads then resolve via field_object/field_int.
  if (name_has(id->name, "getPackageInfo"))     return jni_make_object("android/content/pm/PackageInfo");
  if (name_has(id->name, "getApplicationInfo")) return jni_make_object("android/content/pm/ApplicationInfo");
  if (name_has(id->name, "getPackageManager"))  return jni_make_object("android/content/pm/PackageManager");
  if (name_has(id->name, "getResources"))       return jni_make_object("android/content/res/Resources");
  if (name_has(id->name, "getConfiguration"))   return jni_make_object("android/content/res/Configuration");
  if (sig_returns(id->sig, "Ljava/lang/String;"))
    return jni_make_string(""); // UUID, asset-pack name, etc.
  (void)va;
  // Default for any otherwise-unhandled object method: a live (opaque) fake object, not NULL.
  // Plugin code (billing, ads, ...) typically stores such a result as a native handle and
  // throws on null, which can stall boot for good (seen in an earlier port). The fake object's
  // own methods resolve through the act_* benign defaults. Explicit NULLs above still win.
  return jni_make_object(id->cls);
}

juint act_int(const FakeID *id, va_list va) {
  // java.lang.Integer.parseInt(String[,radix]) / Long.parseLong: FMOD's audio
  // path parses getProperty()'s "48000"/"64" results through these. The old
  // act_int fall-through returned 0 -> framesPerBuffer parsed to 0 -> FMOD's
  // OpenSL output init failed with "Error initializing output device" (60).
  if (name_has(id->name, "parseInt") || name_has(id->name, "parseLong")) {
    const char *s = first_string_arg(id->sig, va);
    juint v = (juint)(s ? strtol(s, NULL, 10) : 0);
    debugPrintf(RD_LOG_TAG_JNI " %s(\"%s\") -> %u\n", id->name, s ? s : "", v);
    return v;
  }
  // Play Asset Delivery: with NO Play Core on Switch, the engine MUST take the
  // "missing" path, where it treats every asset pack as install-time/local and
  // reads assets synchronously from the APK/bundle. Returning false (the old
  // default) tells Unity Play Core IS present, so it uses the ASYNC AssetPackManager
  // path -- it calls getAssetPackState() with a callback that we never invoke and
  // then waits forever for the pack, so the resident scene never loads and
  // ResidentSystem.Awake never runs (the live boot stall). Return true.
  if (name_has(id->name, "playCoreApiMissing")) return 1;
  // The other Google check whose default (0 == ConnectionResult.SUCCESS) wrongly
  // means "Play Services available": isGooglePlayServicesAvailable(). Return
  // SERVICE_MISSING (1) so the Play Games plugin cleanly disables itself instead
  // of trying to sign in against GMS that isn't there. (Not hit during boot --
  // Play Games activates on user action -- but correct for when it is.)
  if (name_has(id->name, "isGooglePlayServicesAvailable")) return 1; /* ConnectionResult.SERVICE_MISSING */
  (void)va;
  // every other "is something open / clicked / ok" probe -> false/0
  return 0;
}

float act_float(const FakeID *id, va_list va) {
  (void)va;
  float x, y, z;
  android_get_orientation(&x, &y, &z);
  if (name_has(id->name, "OrientationX")) return x;
  if (name_has(id->name, "OrientationY")) return y;
  if (name_has(id->name, "OrientationZ")) return z;
  return 0.0f;
}

void act_void(const FakeID *id, va_list va) {
  (void)va;
  if (!strcmp(id->name, "finish") || name_has(id->name, "appEnd") ||
      name_has(id->name, "exitApp"))
    jni_quit_requested = 1;
  // openStore / sendBroadcast / IME open / Mobage / web view: no-op
}


// --- fields -----------------------------------------------------------------
// The engine and the game DO read Java fields: android.os.Build.* (device id),
// Build.VERSION.SDK_INT (API gating), PackageInfo.versionName/versionCode (the
// Application.version the boot path logs as blank today), DisplayMetrics.*, and
// Configuration.*. Returning null/0 universally (the old stub) blanks the app
// version -- which can throw in version-parsing boot code -- and zeroes display
// metrics. Route every field read through a name-based dispatcher. fid is the
// FakeID GetFieldID handed back, so cls/name/sig are all available.
//
// Placeholders marked CHECK are safe defaults, not the shipped values; the game
// never verifies them against anything but a stubbed RemoteConfig, so exact
// numbers don't gate boot. Easy to correct once known.

/* 1 if the field ID is `name` of a class whose name contains cls_sub. */
static int fld_is(const FakeID *id, const char *cls_sub, const char *name) {
  return name_has(id->cls, cls_sub) && !strcmp(id->name, name);
}

// One-line-per-unique-field diagnostic: tells the next run exactly which Java
// fields the game reads (and lets us confirm versionName/currentActivity/etc.
// are being exercised). Dedup by interned name pointer, like log_app_upcall.
void log_field_read(const FakeID *id, char kind) {
  static const void *seen[128]; static int seen_n = 0;
  for (int i = 0; i < seen_n; i++) if (seen[i] == id->name) return;
  if (seen_n < 128) seen[seen_n++] = id->name;
  debugPrintf(RD_LOG_TAG_JNI " field(%c): %s.%s %s\n", kind, id->cls, id->name, id->sig);
}

void *field_object(const FakeID *id) {
  const char *n = id->name, *c = id->cls;
  // PackageInfo / ApplicationInfo version string
  if (!strcmp(n, "versionName")) return jni_make_string(RD_GAME_VERSION_NAME);
  // UnityPlayer statics: currentActivity is THE Activity -- null here NPEs every
  // UnityPlayer.currentActivity.getXxx() in managed code, so hand back a live
  // (opaque) Activity that our method dispatch then services.
  if (name_has(c, "unity3d/player/UnityPlayer")) {
    if (!strcmp(n, "currentActivity")) return jni_make_object("android/app/Activity");
    if (!strcmp(n, "MANUFACTURER"))    return jni_make_string("Nintendo");
  }
  // AudioManager.PROPERTY_OUTPUT_* are static String field keys the engine reads
  // just before AudioManager.getProperty(key) to size FMOD's audio path. Return
  // the real Android property-name strings AND record which one was read
  // (g_last_output_prop) so getProperty() can answer even when the key argument
  // is lost on the JNI call path (see getproperty_value). These are AudioManager
  // fields, NOT UnityPlayer -- gating them on the wrong class meant they never
  // matched, getProperty saw "", and FMOD got framesPerBuffer 0 -> error 60.
  if (name_has(c, "media/AudioManager")) {
    if (!strcmp(n, "PROPERTY_OUTPUT_FRAMES_PER_BUFFER")) { g_last_output_prop = 2; return jni_make_string("android.media.property.OUTPUT_FRAMES_PER_BUFFER"); }
    if (!strcmp(n, "PROPERTY_OUTPUT_SAMPLE_RATE"))       { g_last_output_prop = 1; return jni_make_string("android.media.property.OUTPUT_SAMPLE_RATE"); }
  }
  // Context.*_SERVICE name constants -> the strings getSystemService() expects
  if (name_has(c, "content/Context")) {
    if (!strcmp(n, "AUDIO_SERVICE"))        return jni_make_string("audio");
    if (!strcmp(n, "DISPLAY_SERVICE"))      return jni_make_string("display");
    if (!strcmp(n, "WINDOW_SERVICE"))       return jni_make_string("window");
    if (!strcmp(n, "LOCATION_SERVICE"))     return jni_make_string("location");
    if (!strcmp(n, "CONNECTIVITY_SERVICE")) return jni_make_string("connectivity");
    if (!strcmp(n, "MEDIA_ROUTER_SERVICE")) return jni_make_string("media_router");
    if (!strcmp(n, "VIBRATOR_SERVICE"))     return jni_make_string("vibrator");
  }
  // Environment.MEDIA_MOUNTED MUST equal getExternalStorageState()'s return
  // ("mounted", set in act_object) or the storage check fails and save data is
  // disabled. Keep both in lockstep.
  if (name_has(c, "os/Environment")) {
    if (!strcmp(n, "MEDIA_MOUNTED"))           return jni_make_string("mounted");
    if (!strcmp(n, "MEDIA_MOUNTED_READ_ONLY")) return jni_make_string("mounted_ro");
  }
  if (name_has(c, "pm/PackageManager")) {
    if (!strcmp(n, "FEATURE_AUDIO_LOW_LATENCY")) return jni_make_string("android.hardware.audio.low_latency");
    if (!strcmp(n, "FEATURE_AUDIO_PRO"))         return jni_make_string("android.hardware.audio.pro");
  }
  // android.os.Build identity strings (all public static final String)
  if (name_has(c, "os/Build")) {
    if (!strcmp(n, "MODEL"))        return jni_make_string("Switch");
    if (!strcmp(n, "MANUFACTURER")) return jni_make_string("Nintendo");
    if (!strcmp(n, "BRAND"))        return jni_make_string("Nintendo");
    if (!strcmp(n, "DEVICE"))       return jni_make_string("Switch");
    if (!strcmp(n, "PRODUCT"))      return jni_make_string("Switch");
    if (!strcmp(n, "HARDWARE"))     return jni_make_string("nx");
    if (!strcmp(n, "BOARD"))        return jni_make_string("nx");
    if (!strcmp(n, "DISPLAY"))      return jni_make_string("nx");
    if (!strcmp(n, "ID"))           return jni_make_string("REL");
    if (!strcmp(n, "TYPE"))         return jni_make_string("user");
    if (!strcmp(n, "TAGS"))         return jni_make_string("release-keys");
    if (!strcmp(n, "FINGERPRINT"))  return jni_make_string(RD_DEV_FINGERPRINT);
    if (!strcmp(n, "BOOTLOADER"))   return jni_make_string("unknown");
    if (!strcmp(n, "HOST"))         return jni_make_string("localhost");
    if (!strcmp(n, "USER"))         return jni_make_string("nx");
    if (!strcmp(n, "SERIAL"))       return jni_make_string("unknown");
    if (!strcmp(n, "RELEASE"))      return jni_make_string(RD_DEV_RELEASE);  /* Build.VERSION.* */
    if (!strcmp(n, "CODENAME"))     return jni_make_string("REL");
    if (!strcmp(n, "INCREMENTAL"))  return jni_make_string(RD_DEV_INCREMENTAL);
    if (!strcmp(n, "SECURITY_PATCH")) return jni_make_string(RD_DEV_SECURITY_PATCH);
    if (!strcmp(n, "BASE_OS"))      return jni_make_string("");
  }
  /* ApplicationInfo.metaData must be a live Bundle (rd_meta.c), otherwise
   * Unity never asks for "unity.build-id" and ignores the OBB ("build <unknown>"). */
  if (rd_meta_is_bundle_field(n, id->sig)) return jni_make_object("android/os/Bundle");
  // Any other String-typed field -> "" (non-null avoids NPEs in string ops).
  if (sig_returns(id->sig, "Ljava/lang/String;")) return jni_make_string("");
  // Any other object field stays null; array fields handled by the caller.
  return NULL;
}

/* The render resolution main.c resolved (config.txt override or 1440x2560 auto). All
 * screen-metric reporting derives from it so the game lays out for exactly the buffer we
 * render into. Density tracks the resolution to keep the logical width ~360dp (a real
 * phone at any of these resolutions), matching standard Android density buckets:
 *   720->320dpi/2.0, 1080->480dpi/3.0, 1440->640dpi/4.0. */
extern int screen_width, screen_height;
/* Display density reported to the game, scaled so the screen is 360 dp wide whatever the
 * render size (Unity lays out its UI in dp). */
static int   nx_report_density_dpi(void){ int w = screen_width > 0 ? screen_width : 720;
                                          return (int)(w * 160.0f / 360.0f + 0.5f); }
/* DisplayMetrics.density for the same 360 dp-wide screen. */
static float nx_report_density(void){     int w = screen_width > 0 ? screen_width : 720;
                                          return w / 360.0f; }

juint field_int(const FakeID *id) {
  const char *n = id->name, *c = id->cls;
  if (!strcmp(n, "versionCode")) return RD_GAME_VERSION_CODE;
  // UnityPlayer integer statics
  if (name_has(c, "unity3d/player/UnityPlayer")) {
    if (!strcmp(n, "SDK_INT"))     return RD_DEV_SDK_INT;
    if (!strcmp(n, "densityDpi"))  return nx_report_density_dpi();
    if (!strcmp(n, "widthPixels")) return screen_width  > 0 ? (juint)screen_width  : 720;
    if (!strcmp(n, "heightPixels"))return screen_height > 0 ? (juint)screen_height : 1280;
    if (!strcmp(n, "STREAM_MUSIC"))return 3;   /* AudioManager.STREAM_MUSIC      */
    if (!strcmp(n, "GET_DEVICES_OUTPUTS")) return 2; /* AudioManager.GET_DEVICES_OUTPUTS */
    if (!strcmp(n, "ROUTE_TYPE_LIVE_VIDEO")) return 1;
    if (!strcmp(n, "SCREEN_ORIENTATION_UNSPECIFIED"))       return -1;
    if (!strcmp(n, "SCREEN_ORIENTATION_LANDSCAPE"))         return 0;
    if (!strcmp(n, "SCREEN_ORIENTATION_PORTRAIT"))          return 1;
    if (!strcmp(n, "SCREEN_ORIENTATION_REVERSE_LANDSCAPE")) return 8;
    if (!strcmp(n, "SCREEN_ORIENTATION_REVERSE_PORTRAIT"))  return 9;
    if (!strcmp(n, "SCREEN_ORIENTATION_FULL_USER"))         return 13;
    if (!strcmp(n, "SCREEN_ORIENTATION_FULL_SENSOR"))       return 10;
  }
  if (name_has(c, "content/Context") && !strcmp(n, "MODE_PRIVATE")) return 0;
  if (name_has(c, "pm/PackageManager")) {
    if (!strcmp(n, "PERMISSION_GRANTED")) return 0;   /* == granted              */
    if (!strcmp(n, "PERMISSION_DENIED"))  return (juint)-1;
  }
  if (name_has(c, "os/Build")) {
    if (!strcmp(n, "SDK_INT"))          return RD_DEV_SDK_INT;
    if (!strcmp(n, "PREVIEW_SDK_INT"))  return 0;
  }
  // DisplayMetrics integer fields (width/height/dpi)
  if (name_has(c, "DisplayMetrics")) {
    if (!strcmp(n, "widthPixels"))  return screen_width  > 0 ? (juint)screen_width  : 720;
    if (!strcmp(n, "heightPixels")) return screen_height > 0 ? (juint)screen_height : 1280;
    if (!strcmp(n, "densityDpi"))   return nx_report_density_dpi();
  }
  return 0;
}

/* DisplayMetrics.density / xdpi / ydpi / scaledDensity are float fields. 0 would
 * make dp->px scaling collapse, so hand back a sane xhdpi density (2.0). */
float field_float(const FakeID *id) {
  const char *n = id->name;
  if (name_has(id->cls, "DisplayMetrics")) {
    if (!strcmp(n, "density") || !strcmp(n, "scaledDensity")) return nx_report_density();
    if (!strcmp(n, "xdpi") || !strcmp(n, "ydpi"))             return (float)nx_report_density_dpi();
  }
  (void)fld_is;
  return 0.0f;
}
