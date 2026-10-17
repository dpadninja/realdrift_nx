/* rd_mod_api.c -- the functions of the rd_mod_api table (sdk/rd_mod.h) handed to each mod.
 * Thin wrappers over g_il2cpp (engine/rd_il2cpp.h) and the mod host (rd_mods.c).
 * MIT (see LICENSE). */
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_hook.h"
#include "rd_il2cpp.h"
#include "rd_log.h"
#include "rd_mods_internal.h"
#include "util.h"

int g_rd_mods_in_entry;
rd_icall_override g_rd_mods_icalls[RD_MODS_MAX_ICALLS + 1];
int g_rd_mods_nicalls;
static int g_icall_owner[RD_MODS_MAX_ICALLS];

#define LOG_LINE_MAX 512

rd_mod_slot *rd_mods_slot(const rd_mod_api *m) {
  return (rd_mod_slot *)(void *)((char *)m - offsetof(rd_mod_slot, api));
}

static void api_log(const rd_mod_api *m, const char *fmt, ...) {
  char line[LOG_LINE_MAX];
  va_list va;
  va_start(va, fmt);
  vsnprintf(line, sizeof line, fmt ? fmt : "", va);
  va_end(va);
  debugPrintf(RD_LOG_TAG_MOD "%s] %s\n", m->id, line);
}

static const char *api_ini(const rd_mod_api *m, const char *key) {
  return key ? rd_ini_get(rd_mods_slot(m)->ini, "", key) : NULL;
}

/* Registration guard shared by the on_* functions. */
static int can_register(const void *fn) {
  if (!fn) return RD_MOD_EARG;
  return g_rd_mods_in_entry ? 0 : RD_MOD_ELATE;
}
static int api_on_game_ready(const rd_mod_api *m, rd_mod_ready_fn fn) {
  const int r = can_register((const void *)fn);
  if (!r) rd_mods_slot(m)->ready = fn;
  return r;
}
static int api_on_scene(const rd_mod_api *m, rd_mod_scene_fn fn) {
  const int r = can_register((const void *)fn);
  if (!r) rd_mods_slot(m)->scene = fn;
  return r;
}
static int api_on_frame(const rd_mod_api *m, rd_mod_frame_fn fn) {
  const int r = can_register((const void *)fn);
  if (!r) rd_mods_slot(m)->frame = fn;
  return r;
}

static void *api_find_class(const char *assembly, const char *ns, const char *name) {
  return assembly && ns && name ? rd_il2cpp_class(&g_il2cpp, assembly, ns, name) : NULL;
}
static void *api_find_nested(void *klass, const char *name) {
  return rd_il2cpp_nested_class(&g_il2cpp, klass, name);
}
static int api_field(void *klass, const char *name, uint32_t *offset) {
  if (!klass || !name || !offset) return RD_MOD_EARG;
  return rd_il2cpp_field_offset(&g_il2cpp, klass, name, offset) ? 0 : RD_MOD_ENOTFOUND;
}
static void *api_method(void *klass, const char *name, int argc) {
  if (!klass || !name || !g_il2cpp.class_get_method_from_name) return NULL;
  return (void *)g_il2cpp.class_get_method_from_name(klass, name, argc);
}
static void *api_invoke(void *method, void *obj, void **args, void **exc) {
  void *dummy = NULL;
  if (!method || !g_il2cpp.runtime_invoke) return NULL;
  return g_il2cpp.runtime_invoke(method, obj, args, exc ? exc : &dummy);
}
static void *api_new_object(void *klass) {
  return klass && g_il2cpp.object_new ? g_il2cpp.object_new(klass) : NULL;
}
static void *api_new_string(const char *s) {
  return s && g_il2cpp.string_new ? g_il2cpp.string_new(s) : NULL;
}
static int api_string_eq(void *str, const char *s) {
  return str && s ? rd_il2cpp_str_eq(&g_il2cpp, str, s) : 0;
}
static void *api_icall(const char *name) {
  return name && g_il2cpp.resolve_icall ? g_il2cpp.resolve_icall(name) : NULL;
}

static int api_icall_override(const rd_mod_api *m, const char *name, void *fn, void **orig) {
  if (!name || !fn) return RD_MOD_EARG;
  if (!g_rd_mods_in_entry) return RD_MOD_ELATE;
  for (int i = 0; i < g_rd_mods_nicalls; i++) {
    if (strcmp(g_rd_mods_icalls[i].name, name)) continue;
    api_log(m, "icall %s is already overridden by mod #%d, refused", name, g_icall_owner[i]);
    return RD_MOD_EFULL;
  }
  if (g_rd_mods_nicalls == RD_MODS_MAX_ICALLS) return RD_MOD_EFULL;
  char *copy = malloc(strlen(name) + 1);
  if (!copy) return RD_MOD_EFULL;
  strcpy(copy, name);
  g_icall_owner[g_rd_mods_nicalls] = rd_mods_slot(m)->index;
  g_rd_mods_icalls[g_rd_mods_nicalls++] = (rd_icall_override){ copy, fn, orig };
  g_rd_mods_icalls[g_rd_mods_nicalls] = (rd_icall_override){ NULL, NULL, NULL };
  return 0;
}

static int api_method_swap(void *method, void *fn, void **orig) {
  if (!method || !fn) return RD_MOD_EARG;
  void **mi = method;   /* MethodInfo.methodPointer is the first word (il2cpp v24) */
  if (orig) *orig = mi[0];
  mi[0] = fn;
  return 0;
}

static void *api_method_code(void *method) { return method ? *(void **)method : NULL; }

static int api_inline_hook(void *code, void *fn, void **orig) {
  if (!code || !fn) return RD_MOD_EARG;
  const int r = rd_hook_install(code, fn, orig);
  return r == 0 ? 0 : r == -1 ? RD_MOD_EARG : RD_MOD_EFULL;
}

static void api_draw_text(float x, float y, float size, uint32_t rgba, const char *text) {
  if (text) rd_mods_text(x, y, size, rgba, text);
}

void rd_mods_api_fill(rd_mod_slot *s) {
  s->api = (rd_mod_api){
    .version = RD_MOD_API_VERSION,
    .size = sizeof(rd_mod_api),
    .id = s->info.id,
    .dir = s->dir,
    .log = api_log,
    .ini = api_ini,
    .on_game_ready = api_on_game_ready,
    .on_scene = api_on_scene,
    .on_frame = api_on_frame,
    .find_class = api_find_class,
    .find_nested = api_find_nested,
    .field = api_field,
    .method = api_method,
    .invoke = api_invoke,
    .new_object = api_new_object,
    .new_string = api_new_string,
    .string_eq = api_string_eq,
    .find_objects = rd_mods_find_objects,
    .icall = api_icall,
    .icall_override = api_icall_override,
    .method_swap = api_method_swap,
    .method_code = api_method_code,
    .inline_hook = api_inline_hook,
    .draw_text = api_draw_text,
  };
}
