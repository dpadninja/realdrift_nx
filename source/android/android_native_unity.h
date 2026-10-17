/* android_native_unity.h -- the libandroid NDK surface libunity imports (ANativeWindow,
 * ALooper, ASensor*), implemented in android_native_unity.c, plus the lifecycle helpers main.c
 * calls (screen mode, HID feed). */
#ifndef ANDROID_NATIVE_UNITY_H
#define ANDROID_NATIVE_UNITY_H

#include <stdint.h>

typedef struct ANativeWindow ANativeWindow;
typedef struct ALooper       ALooper;

/* host helpers (called from main.c) */
void  android_native_input_init(void);
void  android_native_update_mode(void);
void  nx_window_reassert(void);   /* re-apply crop+transform after mesa swapchain create */
ANativeWindow *android_native_window(void);
void  android_native_feed_hid(void);

/* NDK functions libunity imports (register these in imports.c) */
void     ANativeWindow_acquire(ANativeWindow *);
void     ANativeWindow_release(ANativeWindow *);
ANativeWindow *ANativeWindow_fromSurface(void *, void *);
int32_t  ANativeWindow_getWidth(ANativeWindow *);
int32_t  ANativeWindow_getHeight(ANativeWindow *);
int32_t  ANativeWindow_setBuffersGeometry(ANativeWindow *, int32_t, int32_t, int32_t);

ALooper *ALooper_prepare(int);
ALooper *ALooper_forThread(void);
void     ALooper_acquire(ALooper *);
void     ALooper_release(ALooper *);
void     ALooper_wake(ALooper *);
int      ALooper_pollOnce(int, int *, int *, void **);
int      ALooper_addFd(ALooper *, int, int, int, void *, void *);
int      ALooper_removeFd(ALooper *, int);

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

void android_get_orientation(float *x, float *y, float *z); /* no sensor: always level */

#include "fakefd.h"   /* libc_shim.c routes pipe()/read/write/close there */

#endif /* ANDROID_NATIVE_UNITY_H */
