/* jni_internal.h -- shared definitions of the fake JNI modules (android layer, internal).
 *
 *   jni_objects.c    the object model: references, objects, strings, arrays, classes, IDs
 *   jni_framework.c  what framework methods and fields answer
 *   jni_fake.c       the JNIEnv / JavaVM function tables and call dispatch
 * Every JNI handle the engine holds points at one of the Fake* records below; the first word
 * is a TAG_* so a handle can be classified. Nothing outside the fake JNI includes this header.
 * MIT (see LICENSE). */
#ifndef JNI_INTERNAL_H
#define JNI_INTERNAL_H

#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#define JNI_OK 0
#define JNI_VERSION_1_6 0x00010006

/* JNI integer results (jint/jboolean/jlong) travel in a 64-bit register. */
typedef uint64_t juint;

/* ---- the fake object model ---- */

enum {
  TAG_OBJECT = 0x4f424a31, // 'OBJ1'  heap object (freeable)
  TAG_STRING = 0x53545231, // 'STR1'
  TAG_OBJARR = 0x4f415231, // 'OAR1'
  TAG_PRIARR = 0x50415231, // 'PAR1'
  TAG_ID     = 0x4d494431, // 'MID1'  pooled, never freed
  TAG_CLASS  = 0x434c5331, // 'CLS1'  pooled, never freed
};

typedef struct { uint32_t tag; char label[64]; } FakeObject;
typedef struct { uint32_t tag; char *utf; } FakeString;
typedef struct { uint32_t tag; int len; void **items; } FakeObjArray;
typedef struct { uint32_t tag; int len; int elem_size; void *data; } FakePriArray;
typedef struct { uint32_t tag; char cls[96]; char name[64]; char sig[160]; } FakeID;
typedef struct { uint32_t tag; char name[96]; } FakeClass;


/* 1 if the JNI method signature `sig` returns type `ret` (e.g. "Ljava/lang/String;"). */
static inline int sig_returns(const char *sig, const char *ret) {
  const char *rp = strchr(sig, ')');
  return rp && strstr(rp + 1, ret) == rp + 1;
}

/* 1 if `sub` occurs in `name` (class and method names are matched by substring). */
static inline int name_has(const char *name, const char *sub) { return strstr(name, sub) != NULL; }


/* ---- object model (jni_objects.c) --------------------------------------------------- */

/* Initialises the reference registry; called first by jni_init(). */
void jni_objects_init(void);
/* Registers ref as a local reference of the current frame (freed by PopLocalFrame); returns ref. */
void *reg_local(void *ref);
/* Frees a heap handle (string, array, object); pooled IDs, classes and interned strings stay. */
void free_ref(void *ref);
/* DeleteLocalRef: removes ref from the registry and frees it. */
void delete_local(void *ref);
/* A primitive array that takes ownership of `data` (len elements of elem_size bytes), local. */
void *make_pri_array_adopt(void *data, int len, int elem_size);
/* UTF-8 text of a fake String, or "" for anything else (never NULL). */
const char *obj_str(void *jstr);
/* Java length of UTF-8 text: UTF-16 code units (supplementary characters count 2). */
juint utf16_len(const char *str);
/* The pooled class handle for a class name (FindClass); never freed. */
void *intern_class(const char *name);
/* Name of a class handle, or "" for anything else. */
const char *class_name_of(void *cls);
/* The pooled method/field ID for (class, name, signature) (GetMethodID/GetFieldID). */
FakeID *get_id(const char *cls, const char *name, const char *sig);
/* The singleton AssetManager / ClassLoader objects. */
void *get_asset_manager_obj(void);
void *get_classloader_obj(void);
/* JNIEnv NewGlobalRef / PushLocalFrame / PopLocalFrame over the local-reference registry. */
void *j_NewGlobalRef(void *env, void *obj);
juint j_PushLocalFrame(void *env, int cap);
void *j_PopLocalFrame(void *env, void *result);

/* ---- framework answers (jni_framework.c) --------------------------------------------- */

/* Return value of a framework method (by its ID; arguments in va), per result type, for
 * classes without real behaviour in unity_jni.c. */
void *act_object(const FakeID *id, va_list va);
juint act_int(const FakeID *id, va_list va);
float act_float(const FakeID *id, va_list va);
void  act_void(const FakeID *id, va_list va);
/* Value of a Java field the engine reads (Build.*, PackageInfo.version*, DisplayMetrics.*, ...). */
void *field_object(const FakeID *id);
juint field_int(const FakeID *id);
float field_float(const FakeID *id);
/* AudioManager.getProperty answers for FMOD: the output sample rate or frames per buffer as a
 * Java string (never empty: FMOD fails its OpenSL init on 0). */
void *getproperty_value(const char *key);
/* Logs a field read once per field ("[jni] field(<kind>): Class.name sig"). */
void log_field_read(const FakeID *id, char kind);

#endif
