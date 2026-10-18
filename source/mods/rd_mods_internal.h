/* rd_mods_internal.h -- state shared by rd_mods.c (boot, frames, patches, text) and
 * rd_mod_api.c (the functions of the rd_mod_api table). Not for other modules.
 * MIT (see LICENSE). */
#ifndef RD_MODS_INTERNAL_H
#define RD_MODS_INTERNAL_H

#include <stddef.h>
#include "rd_icall.h"
#include "rd_mod.h"
#include "rd_mod_ini.h"
#include "rd_modhost.h"
#include "rd_platform.h"   /* RD_PATH_MAX */

/* One loaded mod. `api` is handed to the mod; the functions find the slot back from it. */
typedef struct {
  rd_mod_info      info;
  char             dir[RD_PATH_MAX];
  rd_ini          *ini;        /* mod.ini, heap */
  rd_mod_api       api;
  void            *user;
  rd_mod_ready_fn  ready;
  rd_mod_scene_fn  scene;
  rd_mod_frame_fn  frame;
  int              index;      /* in load order */
} rd_mod_slot;

/* The slot behind a mod's api pointer. */
rd_mod_slot *rd_mods_slot(const rd_mod_api *m);

/* Fills slot->api (rd_mod_api.c). */
void rd_mods_api_fill(rd_mod_slot *slot);

/* 1 while a mod's rd_mod_entry runs (registration is only allowed then). */
extern int g_rd_mods_in_entry;

/* Icall overrides the mods asked for (NULL-terminated), owners and claims (rd_mod_api.c). */
#define RD_MODS_MAX_ICALLS 64
extern rd_icall_override g_rd_mods_icalls[RD_MODS_MAX_ICALLS + 1];
extern int g_rd_mods_nicalls;

/* Queues a line of text for the next frames (rd_mods.c, locked). */
void rd_mods_text(float x, float y, float size, uint32_t rgba, const char *text);

/* The name of an object (a component: its GameObject's name) as ASCII into buf; its length,
 * 0 if it has none (rd_mods.c). */
#define RD_MODS_NAME_LEN 128
int rd_mods_object_name(void *obj, char *buf, size_t n);

/* A managed string (may be NULL) as ASCII into buf, others as '?'; its length (rd_mods.c). */
int rd_mods_ascii(void *str, char *buf, size_t n);

/* patch.ini and cars.ini (rd_mods_patches.c), main thread. _load adds the values of mod `id`
 * (either ini may be NULL; id must outlive the patches) and returns how many; _resolve looks
 * the classes and fields up once the game is ready and hooks the game's car building;
 * _apply writes the values into live objects -- with scene_changed also the ones written once
 * per scene (torque curve, power). */
int  rd_mods_patches_load(const char *id, const rd_ini *patches, const rd_ini *cars);
void rd_mods_patches_resolve(void);
void rd_mods_patches_apply(int scene_changed);

/* Live objects of klass (rd_mods.c): up to max into out; count, or a negative RD_MOD_E*. */
int rd_mods_find_objects(void *klass, void **out, int max);

#endif
