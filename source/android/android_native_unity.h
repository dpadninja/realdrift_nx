/* android_native_unity.h -- the libandroid NDK surface libunity imports (ANativeWindow,
 * ALooper, ASensor*), implemented in android_native_unity.c, plus the lifecycle helpers main.c
 * calls (screen mode, HID feed).
 * Forked from badpiggies_nx by ChanseyIsTheBest (https://github.com/ChanseyIsTheBest/badpiggies_nx,
 * commit 7726be3), from the so-loader lineage; changed for realdrift_nx. MIT (see LICENSE). */
#ifndef ANDROID_NATIVE_UNITY_H
#define ANDROID_NATIVE_UNITY_H

#include <stdint.h>

typedef struct ANativeWindow ANativeWindow;
typedef struct ALooper       ALooper;

/* ---- called by main.c and the EGL glue ------------------------------------------------- */

/* Initialises pad and touch input and the nx_pointer module (cursor, mouse, gyro). Call once at
 * boot, before the engine starts its threads (it reads cursor.png from the SD card). */
void  android_native_input_init(void);
/* Takes over the render size main.c picked (screen_width/height) for the window buffer. */
void  android_native_update_mode(void);
/* Re-applies the window crop and identity transform; mesa may reset them when it creates its
 * swapchain (egl.c calls this for the first frames). */
void  nx_window_reassert(void);
/* The one window (libnx default NWindow) sized to the render resolution. */
ANativeWindow *android_native_window(void);
/* Once per frame: polls pad/touch/mouse through nx_pointer and feeds every finger and the
 * cursor to UnityEngine.Input (unity_input_hook.h) in Unity's bottom-left coordinates. */
void  android_native_feed_hid(void);

/* ---- NDK functions libunity imports (referenced from the import tables) ----------------- */

/* ANativeWindow over the single NWindow: acquire/release are no-ops, fromSurface returns the
 * window, getWidth/Height report the render size, setBuffersGeometry resizes the buffer. */
void     ANativeWindow_acquire(ANativeWindow *);
void     ANativeWindow_release(ANativeWindow *);
ANativeWindow *ANativeWindow_fromSurface(void *, void *);
int32_t  ANativeWindow_getWidth(ANativeWindow *);
int32_t  ANativeWindow_getHeight(ANativeWindow *);
int32_t  ANativeWindow_setBuffersGeometry(ANativeWindow *, int32_t, int32_t, int32_t);

/* ALooper as a per-thread wait/wake primitive (Unity never polls real fds through it): every
 * thread gets a looper on demand; pollOnce waits for a wake or the timeout and returns
 * ALOOPER_POLL_WAKE (-1) or ALOOPER_POLL_TIMEOUT (-3); addFd/removeFd accept and ignore. */
ALooper *ALooper_prepare(int);
ALooper *ALooper_forThread(void);
void     ALooper_acquire(ALooper *);
void     ALooper_release(ALooper *);
void     ALooper_wake(ALooper *);
int      ALooper_pollOnce(int, int *, int *, void **);
int      ALooper_addFd(ALooper *, int, int, int, void *, void *);
int      ALooper_removeFd(ALooper *, int);

/* Sensors: the device has none. The manager and queues exist, but there are no sensors,
 * enabling one fails and no events ever arrive. */
void *ASensorManager_getInstance(void);
int   ASensorManager_getSensorList(void *, void **);
void *ASensorManager_getDefaultSensor(void *, int);
void *ASensorManager_createEventQueue(void *, void *, int, void *, void *);
int   ASensorManager_destroyEventQueue(void *, void *);
int   ASensorEventQueue_enableSensor(void *, const void *);
int   ASensorEventQueue_disableSensor(void *, const void *);
int   ASensorEventQueue_setEventRate(void *, const void *, int32_t);
int   ASensorEventQueue_getEvents(void *, void *, size_t);
int   ASensorEventQueue_hasEvents(void *);
const char *ASensor_getName(const void *);
const char *ASensor_getVendor(const void *);
int         ASensor_getType(const void *);
float       ASensor_getResolution(const void *);
int         ASensor_getMinDelay(const void *);

/* Device orientation for the Java getters (jni_fake.c): always level, there is no sensor. */
void android_get_orientation(float *x, float *y, float *z);

#include "fakefd.h"   /* libc_shim.c routes pipe()/read/write/close there */

#endif /* ANDROID_NATIVE_UNITY_H */
