/* jni_fake.h -- the fake JNIEnv / JavaVM the Unity engine talks to.
 *
 * There is no JVM on the Switch: libunity's calls into the Android framework (Context,
 * PackageManager, Display, SharedPreferences, AssetManager, Locale, ...) land on this table.
 * jni_fake.c answers the generic ones; unity_jni.c the classes that need real behaviour.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 * Forked from badpiggies_nx by ChanseyIsTheBest (https://github.com/ChanseyIsTheBest/badpiggies_nx,
 * commit 7726be3), from the so-loader lineage; changed for realdrift_nx. MIT (see LICENSE).
 */

#ifndef __JNI_FAKE_H__
#define __JNI_FAKE_H__

#include <stdint.h>

extern void *fake_vm;  // JavaVM *
extern void *fake_env; // JNIEnv *

// set when the engine asks the activity to finish
extern volatile int jni_quit_requested;

/* Builds the JNIEnv and JavaVM function tables; call once before JNI_OnLoad. */
void jni_init(void);

// the fake MyNativeActivity jobject handed to ANativeActivity.clazz

// fake Java object / string constructors
void *jni_make_string(const char *utf);
void *jni_make_object(const char *label);

// Resolve a captured UnityPlayer native (initJni/nativeRender/...) by name
void *jni_lookup_unity_native(const char *name);

#endif
