/* unity_jni.h -- real handlers for the Android classes Unity needs working behaviour from.
 *
 * jni_fake.c's dispatchers hand these classes over (unity_owns_class); the rest stay generic:
 *
 *   android/content/res/AssetManager      -> open()/openFd() of assets/ entries in base.apk
 *   java/io/InputStream                   -> read()/available()/close()
 *   android/content/res/AssetFileDescriptor + java/io/FileDescriptor (mmap path)
 *   android/content/SharedPreferences (+ $Editor) -> PlayerPrefs, stored in RD_PREFS_FILE
 *   android/view/Display + DisplayManager + Configuration + Resources -> screen
 *   android/content/Context               -> data/files paths, package code path
 *   com/unity3d/player/UnityPlayer        -> host queries (mostly no-op)
 *
 * unity_jni_init(RD_GAME_HOME) registers the class list at startup.
 * Forked from badpiggies_nx by ChanseyIsTheBest (https://github.com/ChanseyIsTheBest/badpiggies_nx,
 * commit 7726be3), from the so-loader lineage; changed for realdrift_nx. MIT (see LICENSE).
 */
#ifndef UNITY_JNI_H
#define UNITY_JNI_H

#include <stdarg.h>
#include <stdint.h>

/* FakeID is defined in jni_internal.h; we only read .cls/.name/.sig (all char[]). */

/* Registers the Java classes with real behaviour and opens prefs.kv under data_root. */
void  unity_jni_init(const char *data_root);

/* unity_glue.c: creates the fake UnityPlayer / Context / Surface objects and registers the
 * Java classes (unity_jni_init). Called once by main.c after jni_init(). */
void  unity_environment_init(const char *data_root);
int   unity_owns_class(const char *cls);          /* 1 if this module handles it */

/* Call a method of a class this module owns (id is the FakeID, arguments in va), by result
 * type: objects, integers (int/boolean/long) and void. */
void     *unity_dispatch_object(void *recv, const void *id, va_list va);
uint64_t  unity_dispatch_int   (void *recv, const void *id, va_list va);
void      unity_dispatch_void  (void *recv, const void *id, va_list va);

/* Boxed PlayerPrefs values returned by getAll() iteration. jni_fake.c routes
 * unbox calls (intValue/longValue/booleanValue/floatValue) and IsInstanceOf by
 * RECEIVER so only our own boxed handles are affected (other code's Integer/etc.
 * objects are untouched). */
int       unity_is_boxed   (void *recv);                 /* 1 if recv is our boxed value */
uint64_t  unity_boxed_int  (void *recv);                 /* int/long/bool payload */
float     unity_boxed_float (void *recv);                /* float payload */
int       unity_isinstance (void *obj, const char *clazz); /* 1/0 for boxed, -1 = not ours */

/* provided by jni_fake.c */
extern void       *jni_make_string(const char *utf);
extern void       *jni_make_object(const char *label);
extern void       *jni_bytearray_data(void *arr, int *len_out);
extern const char *jni_string_utf(void *jstr);

/* SharedPreferences.getFloat -> 1 and *out set if handled (unity_jni.c). */
int unity_prefs_get_float(const void *id, va_list va, float *out);

#endif /* UNITY_JNI_H */
