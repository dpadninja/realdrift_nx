/* rd_mod.h -- the whole API a realdrift_nx mod uses. A mod is a folder on the SD card:
 *
 *   sdmc:/switch/realdrift_nx/mods/<id>/
 *     mod.ini     id, name, version, api, enabled, priority, plus the mod's own keys
 *     mod.so      optional native code (this header), built by tools/mod_new.py's Makefile
 *     patch.ini   optional field values set on every live object of a class
 *     files/apk/  optional files replacing (or added to) base.apk members, same paths
 *     files/obb/  the same for the OBB
 *
 * mod.so exports rd_mod_entry (RD_MOD_ENTRY below). The port calls it once at boot, before
 * Unity starts, with a table of functions bound to this mod. Register callbacks and icall
 * overrides there; everything that touches game objects runs in the callbacks.
 *
 * Threads: entry and all callbacks run on the main thread. Icall overrides and swapped
 * methods run on whatever thread the engine calls them from.
 * Errors: functions returning int give 0 or a negative RD_MOD_E*.
 * Plain C, no port headers: build with -nostdlib; libc functions (memcpy, snprintf, malloc,
 * ...) are resolved from the port at load time. MIT (see LICENSE). */
#ifndef RD_MOD_H
#define RD_MOD_H

#include <stdint.h>

#define RD_MOD_API_VERSION 1   /* mod.ini "api" must not be newer than the port's */

#define RD_MOD_EARG    (-1)    /* NULL or invalid argument */
#define RD_MOD_ELATE   (-2)    /* only allowed inside rd_mod_entry */
#define RD_MOD_EFULL   (-3)    /* a fixed-size table is full */
#define RD_MOD_ENOTFOUND (-4)  /* class, method, field or icall does not exist */

typedef struct rd_mod_api rd_mod_api;
typedef void (*rd_mod_ready_fn)(void *user);
typedef void (*rd_mod_scene_fn)(void *user, const char *scene);   /* scene name, never NULL */
typedef void (*rd_mod_frame_fn)(void *user, uint32_t frame);

struct rd_mod_api {
  uint32_t    version;  /* RD_MOD_API_VERSION of the port */
  uint32_t    size;     /* sizeof(rd_mod_api) in the port: newer fields exist if larger */
  const char *id;       /* this mod's id (mod.ini) */
  const char *dir;      /* this mod's folder, for its own files */

  /* ---- basics ---- */

  /* printf-style line in debug.log, prefixed "[mod:<id>]"; a trailing newline is added. */
  void (*log)(const rd_mod_api *m, const char *fmt, ...);
  /* Value of `key` in this mod's mod.ini, or NULL. */
  const char *(*ini)(const rd_mod_api *m, const char *key);

  /* ---- lifecycle: register in rd_mod_entry (later calls return RD_MOD_ELATE) ---- */

  /* fn(user) once, when the game's C# classes can be looked up (first frames). */
  int (*on_game_ready)(const rd_mod_api *m, rd_mod_ready_fn fn);
  /* fn(user, name) after each scene change, after patch.ini values were applied. */
  int (*on_scene)(const rd_mod_api *m, rd_mod_scene_fn fn);
  /* fn(user, frame) once per frame, before the engine renders it. */
  int (*on_frame)(const rd_mod_api *m, rd_mod_frame_fn fn);

  /* ---- il2cpp: the game's C# by name (find names with tools/il2cpp_find.py) ---- */

  /* Class `ns.name` in assembly ("Assembly-CSharp" for the game, "UnityEngine.CoreModule",
   * ...; ns "" for the game's classes), or NULL. Valid from on_game_ready on. */
  void *(*find_class)(const char *assembly, const char *ns, const char *name);
  /* Nested class (C# Outer.Inner) of klass, or NULL. */
  void *(*find_nested)(void *klass, const char *name);
  /* Offset of instance field `name` in objects of klass: 0 and *offset, or RD_MOD_ENOTFOUND.
   * Read/write fields at (char *)obj + offset. */
  int (*field)(void *klass, const char *name, uint32_t *offset);
  /* MethodInfo of klass.name with argc parameters (-1: any), or NULL. */
  void *(*method)(void *klass, const char *name, int argc);
  /* Calls a method: obj = this (NULL for static), args[i] points at argument i (value types
   * by pointer, objects as the pointer itself). A thrown exception goes to *exc (may be NULL).
   * Returns the result; value types come back boxed (data at +0x10). */
  void *(*invoke)(void *method, void *obj, void **args, void **exc);
  /* New object of klass, constructor NOT run (invoke ".ctor" yourself). */
  void *(*new_object)(void *klass);
  /* New C# string from UTF-8 / ASCII. */
  void *(*new_string)(const char *s);
  /* 1 if the C# string str equals the ASCII string s. */
  int (*string_eq)(void *str, const char *s);
  /* Live objects of klass (a UnityEngine.Object subclass, e.g. a MonoBehaviour): up to max
   * into out. Returns how many exist (may exceed max), or a negative RD_MOD_E*. */
  int (*find_objects)(void *klass, void **out, int max);
  /* Engine function of a registered icall ("UnityEngine.Transform::get_position_Injected"),
   * or NULL. Check names with tools/unity_ref.py icalls (status "game"). */
  void *(*icall)(const char *name);

  /* ---- hooks ---- */

  /* Replaces engine icall `name` with fn; *orig (if not NULL) receives the engine's function.
   * Only in rd_mod_entry; the first mod (by priority) to claim a name wins. */
  int (*icall_override)(const rd_mod_api *m, const char *name, void *fn, void **orig);
  /* Points `method` (a MethodInfo from method()) at fn; *orig gets the old code. Affects calls
   * the engine makes by MethodInfo -- Unity messages (Update, Start, OnTriggerEnter, ...),
   * coroutines, invoke() -- not direct calls compiled into other C# methods. fn has the
   * method's C signature plus a trailing MethodInfo*: void fn(void *self, ..., void *mi). */
  int (*method_swap)(void *method, void *fn, void **orig);

  /* ---- screen ---- */

  /* Draws text this frame (call from on_frame): top-left (x, y) in screen pixels at 720p
   * (scaled to the render size), `size` pixels per font pixel (2-4 reads well), colour
   * 0xRRGGBBAA. Built-in 3x5 font: digits, A-Z, common punctuation. */
  void (*draw_text)(float x, float y, float size, uint32_t rgba, const char *text);
};

/* The entry point: return 0 to stay loaded (anything else: the mod is skipped). *user is
 * passed back to every callback of this mod. */
#define RD_MOD_ENTRY \
  __attribute__((visibility("default"))) int rd_mod_entry(const rd_mod_api *m, void **user)

#endif
