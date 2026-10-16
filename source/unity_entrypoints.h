/* unity_entrypoints.h -- UnityPlayer native method typedefs.
 *
 * Natives are resolved with NO hardcoded offsets: JNI_OnLoad is exported by libunity
 * (so_try_find_addr_rx); initJni / nativeRender / ... are file-local, but libunity's
 * JNI_OnLoad hands their {name -> fn} table to RegisterNatives, which the fake JNI captures
 * (jni_fake.c: jni_lookup_unity_native). main.c looks them up by name after JNI_OnLoad.
 *
 * Signatures of Real Drift's libunity (Unity 2018.4), from its JNINativeMethod table
 * (tools/jni_natives.py, analysis/research/04-jni-surface.md):
 *
 *   initJni                        (Landroid/content/Context;)V    3 args: env, thiz, Context
 *   nativeRecreateGfxState         (ILandroid/view/Surface;)V
 *   nativeSendSurfaceChangedEvent  ()V
 *   nativeRender                   ()Z
 *   nativeInjectEvent              (Landroid/view/InputEvent;)Z
 *   nativeResume                   ()V
 *   nativePause                    ()Z
 *   nativeFocusChanged             (Z)V
 *   nativeDone                     ()V
 *
 * (nativeApplicationUnload and nativeUnityPlayerSetRunning of newer Unity versions do not
 * exist in 2018.4.) */
#ifndef UNITY_ENTRYPOINTS_H
#define UNITY_ENTRYPOINTS_H

#include <stdint.h>
#include "so_util.h"

typedef void     (*fn_initJni)(void*,void*,void*);            /* env,thiz,Context  (3-arg!) */
typedef void     (*fn_gfxstate)(void*,void*,int32_t,void*);   /* env,thiz,int,Surface       */
typedef void     (*fn_v)(void*,void*);                        /* env,thiz -> void           */
typedef uint8_t  (*fn_z)(void*,void*);                        /* env,thiz -> bool           */
typedef void     (*fn_vz)(void*,void*,int32_t);               /* env,thiz,int -> void        */
typedef uint8_t  (*fn_inject)(void*,void*,void*);             /* env,thiz,InputEvent -> bool */
typedef void     (*fn_orient)(void*,void*,int32_t,int32_t);
typedef int      (*fn_jnionload)(void* /*vm*/, void* /*reserved*/);

/* Names as they appear in the UnityPlayer RegisterNatives table. main.c looks each
 * up via jni_lookup_unity_native() after JNI_OnLoad has run. */
#define RD_NATIVE_initJni                  "initJni"
#define RD_NATIVE_nativeRecreateGfxState   "nativeRecreateGfxState"
#define RD_NATIVE_nativeSendSurfaceChanged "nativeSendSurfaceChangedEvent"
#define RD_NATIVE_nativeRender             "nativeRender"
#define RD_NATIVE_nativeInjectEvent        "nativeInjectEvent"
#define RD_NATIVE_nativeResume             "nativeResume"
#define RD_NATIVE_nativePause              "nativePause"
#define RD_NATIVE_nativeFocusChanged       "nativeFocusChanged"
#define RD_NATIVE_nativeDone               "nativeDone"

#endif /* UNITY_ENTRYPOINTS_H */
