/* rd_mods.c -- see rd_mods.h. Boot (scan, files, patch.ini / cars.ini, mod.so), the per-frame
 * work (game ready, scene changes, patches via rd_mods_patches.c, callbacks), live objects and
 * the mods' text overlay.
 * MIT (see LICENSE). */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <switch.h>
#include "config.h"
#include "imports.h"          /* nx_resolve_imports */
#include "mem_layout.h"       /* mem_so_load */
#include "rd_game.h"
#include "rd_game_symbols.h"
#include "rd_il2cpp.h"
#include "rd_log.h"
#include "rd_mods.h"
#include "rd_mods_internal.h"
#include "rd_overlay.h"
#include "rd_text.h"
#include "rd_vfile.h"
#include "so_util.h"
#include "util.h"

#define MODS_DIR        "mods"
#define MOD_INI         "mod.ini"
#define MOD_SO          "mod.so"
#define PATCH_INI       "patch.ini"
#define CARS_INI        "cars.ini"
#define FILES_APK       "files/apk"
#define FILES_OBB       "files/obb"
#define MAX_INI_BYTES   (64 * 1024)
#define MAX_FILE_CLAIMS RD_VFILE_MAX_FILES
#define MAX_DIR_DEPTH   8
#define TEXT_REF_HEIGHT 720.0f       /* draw_text coordinates are 720p pixels */
#define MAX_TEXT_RECTS  (RD_MODTEXT_LEN * RD_TEXT_GLYPH_W * RD_TEXT_GLYPH_H)

static rd_mod_slot *g_mods[RD_MODS_MAX];
static int          g_nmods;
static int          g_ready;
static int32_t      g_scene = -1;

/* ---- live objects ---- */

int rd_mods_find_objects(void *klass, void **out, int max) {
  static void *(*find)(void *type);
  if (!find && g_il2cpp.resolve_icall) find = g_il2cpp.resolve_icall(RD_ICALL_FIND_OBJECTS);
  if (!klass || !out || max < 0) return RD_MOD_EARG;
  if (!find || !g_il2cpp.class_get_type || !g_il2cpp.type_get_object || !g_il2cpp.array_length)
    return RD_MOD_ENOTFOUND;
  void *arr = find(g_il2cpp.type_get_object(g_il2cpp.class_get_type(klass)));
  if (!arr) return 0;
  const int n = (int)g_il2cpp.array_length(arr);
  for (int i = 0; i < n && i < max; i++) out[i] = rd_il2cpp_array_at(arr, (uint32_t)i);
  return n;
}

int rd_mods_ascii(void *str, char *buf, size_t n) {
  if (!n) return 0;
  size_t len = 0;
  if (str && g_il2cpp.string_length && g_il2cpp.string_chars) {
    const uint16_t *c = g_il2cpp.string_chars(str);
    const int32_t sl = g_il2cpp.string_length(str);
    for (; len + 1 < n && (int32_t)len < sl; len++) buf[len] = c[len] < 128 ? (char)c[len] : '?';
  }
  buf[len] = 0;
  return (int)len;
}

int rd_mods_object_name(void *obj, char *buf, size_t n) {
  static void *(*game_object)(void *component);
  static void *(*get_name)(void *obj);
  if (!game_object && g_il2cpp.resolve_icall) {
    game_object = g_il2cpp.resolve_icall(RD_ICALL_GAME_OBJECT);
    get_name = g_il2cpp.resolve_icall(RD_ICALL_OBJECT_NAME);
  }
  if (!n) return 0;
  buf[0] = 0;
  if (!obj || !get_name) return 0;
  /* a component is named after its GameObject; anything else by its own name */
  void *go = game_object ? game_object(obj) : NULL;
  return rd_mods_ascii(get_name(go ? go : obj), buf, n);
}

/* ---- text ---- */

static rd_modtext g_text;
static Mutex      g_text_lock;

void rd_mods_text(float x, float y, float size, uint32_t rgba, const char *text) {
  mutexLock(&g_text_lock);
  rd_modtext_add(&g_text, x, y, size, rgba, text);
  mutexUnlock(&g_text_lock);
}

/* The overlay draw (rd_overlay_set_mod_draw): the shown lines, scaled from 720p. */
static void draw_text(int w, int h) {
  static rd_rect rects[MAX_TEXT_RECTS];
  mutexLock(&g_text_lock);
  if (g_text.nshown && rd_overlay_begin(w, h)) {
    const float k = (float)h / TEXT_REF_HEIGHT;
    for (int i = 0; i < g_text.nshown; i++) {
      const rd_modtext_line *l = &g_text.shown[i];
      const int n = rd_text_layout(l->text, l->x * k, l->y * k, l->size * k, rects, MAX_TEXT_RECTS);
      for (int at = 0; at < n; at += RD_OVERLAY_MAX_RECTS) {
        const int c = n - at < RD_OVERLAY_MAX_RECTS ? n - at : RD_OVERLAY_MAX_RECTS;
        rd_overlay_rects(rects + at, c, (float)(l->rgba >> 24) / 255.0f,
                         (float)((l->rgba >> 16) & 0xFF) / 255.0f,
                         (float)((l->rgba >> 8) & 0xFF) / 255.0f, (float)(l->rgba & 0xFF) / 255.0f);
      }
    }
    rd_overlay_end();
  }
  mutexUnlock(&g_text_lock);
}

/* ---- frames ---- */

/* 1 once the game's assembly can be opened (il2cpp is up and the game's classes resolve). */
static int game_ready(void) {
  if (!g_il2cpp.domain_get || !g_il2cpp.domain_assembly_open) return 0;
  void *dom = g_il2cpp.domain_get();
  return dom && g_il2cpp.domain_assembly_open(dom, RD_ASM_GAME) != NULL;
}

/* The active scene's handle and name; 0, or -1 when the icalls are missing. */
static int active_scene(int32_t *handle, char *name, size_t n) {
  static void (*get)(int32_t *out);
  static void *(*get_name)(int32_t handle);
  if (!get && g_il2cpp.resolve_icall) {
    get = g_il2cpp.resolve_icall(RD_ICALL_ACTIVE_SCENE);
    get_name = g_il2cpp.resolve_icall(RD_ICALL_SCENE_NAME);
  }
  if (!get || !get_name) return -1;
  get(handle);
  void *s = get_name(*handle);
  size_t len = 0;
  if (s && g_il2cpp.string_length && g_il2cpp.string_chars) {
    const uint16_t *c = g_il2cpp.string_chars(s);
    const int32_t sl = g_il2cpp.string_length(s);
    for (; len + 1 < n && (int32_t)len < sl; len++) name[len] = c[len] < 128 ? (char)c[len] : '?';
  }
  name[len] = 0;
  return 0;
}

void rd_mods_frame(uint32_t frame) {
  if (!g_nmods) return;
  mutexLock(&g_text_lock);
  rd_modtext_swap(&g_text);
  mutexUnlock(&g_text_lock);
  if (!g_ready) {
    if (!game_ready()) return;
    g_ready = 1;
    rd_mods_patches_resolve();
    for (int i = 0; i < g_nmods; i++)
      if (g_mods[i]->ready) g_mods[i]->ready(g_mods[i]->user);
  }
  if (frame % RD_MODS_SCENE_FRAMES == 0) {
    int32_t h;
    char name[RD_INI_VALUE_LEN];
    if (active_scene(&h, name, sizeof name) == 0 && h != g_scene) {
      g_scene = h;
      debugPrintf(RD_LOG_TAG_MODS " scene %s\n", name);
      rd_mods_patches_apply();
      for (int i = 0; i < g_nmods; i++)
        if (g_mods[i]->scene) g_mods[i]->scene(g_mods[i]->user, name);
    }
  }
  if (frame % RD_MODS_PATCH_FRAMES == 0) rd_mods_patches_apply();   /* objects spawned later */
  for (int i = 0; i < g_nmods; i++)
    if (g_mods[i]->frame) g_mods[i]->frame(g_mods[i]->user, frame);
}

/* ---- boot ---- */

/* out = a "/" b; 0, or -1 (nothing usable in out) when it does not fit RD_PATH_MAX. */
static int path_join(char *out, const char *a, const char *b) {
  const int n = snprintf(out, RD_PATH_MAX, "%s/%s", a, b);
  return n < 0 || n >= RD_PATH_MAX ? -1 : 0;
}

/* Reads a whole small text file (malloc'd, NUL-terminated) or NULL. */
static char *read_text(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  char *buf = malloc(MAX_INI_BYTES + 1);
  const size_t n = buf ? fread(buf, 1, MAX_INI_BYTES, f) : 0;
  const int too_big = buf && n == MAX_INI_BYTES && fgetc(f) != EOF;
  fclose(f);
  if (!buf || too_big) { free(buf); return NULL; }
  buf[n] = 0;
  return buf;
}

/* Parses an INI file into a heap rd_ini; NULL (and a log line naming the bad line). */
static rd_ini *read_ini(const char *path, const char *id) {
  char *text = read_text(path);
  if (!text) return NULL;
  rd_ini *ini = malloc(sizeof *ini);
  const int bad = ini ? rd_ini_parse(text, ini) : -1;
  free(text);
  if (bad) {
    debugPrintf(RD_LOG_TAG_MODS " %s: %s line %d is not \"key = value\"\n", id, path, bad);
    free(ini);
    return NULL;
  }
  return ini;
}

static rd_claim  g_file_claim[MAX_FILE_CLAIMS];
static rd_claims g_files = { g_file_claim, 0, MAX_FILE_CLAIMS };

/* Registers every file under dir (recursively) as member prefix/<relative path> of archive. */
static int add_files(int mod, const char *archive, const char *dir, const char *rel, int depth) {
  DIR *d = opendir(dir);
  if (!d) return 0;
  int n = 0;
  struct dirent *de;
  while ((de = readdir(d)) != NULL) {
    if (de->d_name[0] == '.') continue;
    char path[RD_PATH_MAX], member[RD_PATH_MAX];
    if (path_join(path, dir, de->d_name)) continue;
    if (rel[0] ? path_join(member, rel, de->d_name)
               : snprintf(member, sizeof member, "%s", de->d_name) >= (int)sizeof member)
      continue;
    struct stat st;
    if (stat(path, &st)) continue;
    if (S_ISDIR(st.st_mode)) {
      if (depth < MAX_DIR_DEPTH) n += add_files(mod, archive, path, member, depth + 1);
      continue;
    }
    char *key = malloc(strlen(archive) + strlen(member) + 2);
    if (!key) break;
    sprintf(key, "%s:%s", archive, member);
    const int owner = rd_claim_take(&g_files, key, mod);
    if (owner >= 0) {
      debugPrintf(RD_LOG_TAG_MODS " %s: %s is already replaced by %s, skipped\n",
                  g_mods[mod]->info.id, member, g_mods[owner]->info.id);
      free(key);
    } else if (owner == -2 || rd_vfile_put(archive, member, path)) {
      debugPrintf(RD_LOG_TAG_MODS " %s: cannot add %s\n", g_mods[mod]->info.id, member);
      if (owner == -2) free(key);
    } else {
      n++;
    }
  }
  closedir(d);
  return n;
}

/* Loads mod.so of slot and runs its entry. 0 = loaded (or no mod.so), -1 = skip the mod. */
static int load_code(rd_mod_slot *s) {
  char path[RD_PATH_MAX];
  struct stat st;
  if (path_join(path, s->dir, MOD_SO) || stat(path, &st)) return 0;
  so_module *so = calloc(1, sizeof *so);
  if (!so || mem_so_load(so, path) < 0) {
    debugPrintf(RD_LOG_TAG_MODS " %s: %s is not a loadable aarch64 library\n", s->info.id, MOD_SO);
    free(so);
    return -1;
  }
  nx_resolve_imports(so);
  so_finalize(so);
  so_flush_caches(so);
  so_execute_init_array(so);
  so_free_temp(so);
  typedef int (*entry_fn)(const rd_mod_api *, void **);
  const entry_fn entry = (entry_fn)so_try_find_addr_rx(so, "rd_mod_entry");
  if (!entry) {
    debugPrintf(RD_LOG_TAG_MODS " %s: %s does not export rd_mod_entry\n", s->info.id, MOD_SO);
    return -1;
  }
  const int icalls_before = g_rd_mods_nicalls;
  g_rd_mods_in_entry = 1;
  const int r = entry(&s->api, &s->user);
  g_rd_mods_in_entry = 0;
  if (r != 0) {
    debugPrintf(RD_LOG_TAG_MODS " %s: rd_mod_entry returned %d, mod skipped\n", s->info.id, r);
    g_rd_mods_nicalls = icalls_before;   /* drop its icall overrides */
    g_rd_mods_icalls[g_rd_mods_nicalls] = (rd_icall_override){ NULL, NULL, NULL };
    s->ready = NULL; s->scene = NULL; s->frame = NULL;
    return -1;
  }
  return 1;
}

/* Reads mods/<folder>/mod.ini into a new slot; NULL (logged) if the mod is skipped. */
static rd_mod_slot *read_mod(const char *mods_dir, const char *folder) {
  rd_mod_slot *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  char path[RD_PATH_MAX], err[160];
  if (path_join(s->dir, mods_dir, folder) || path_join(path, s->dir, MOD_INI)) { free(s); return NULL; }
  s->ini = read_ini(path, folder);
  if (!s->ini || rd_mod_info_read(s->ini, folder, &s->info, err, sizeof err)) {
    if (s->ini) debugPrintf(RD_LOG_TAG_MODS " %s: %s, skipped\n", folder, err);
    else debugPrintf(RD_LOG_TAG_MODS " %s: no readable %s, skipped\n", folder, MOD_INI);
    free(s->ini); free(s);
    return NULL;
  }
  if (!s->info.enabled || s->info.api > RD_MOD_API_VERSION) {
    if (s->info.enabled)
      debugPrintf(RD_LOG_TAG_MODS " %s: needs mod API %d, this port has %d, skipped\n", folder,
                  s->info.api, RD_MOD_API_VERSION);
    free(s->ini); free(s);
    return NULL;
  }
  return s;
}

void rd_mods_boot(const char *home) {
  mutexInit(&g_text_lock);
  if (!config.mods) { debugPrintf(RD_LOG_TAG_MODS " off (config.txt mods = 0)\n"); return; }
  char dir[RD_PATH_MAX];
  DIR *d = path_join(dir, home, MODS_DIR) ? NULL : opendir(dir);
  if (!d) return;
  rd_mod_slot *found[RD_MODS_MAX];
  int n = 0;
  struct dirent *de;
  while ((de = readdir(d)) != NULL && n < RD_MODS_MAX)
    if (de->d_name[0] != '.' && (found[n] = read_mod(dir, de->d_name)) != NULL) n++;
  closedir(d);

  rd_mod_info infos[RD_MODS_MAX];
  int order[RD_MODS_MAX];
  for (int i = 0; i < n; i++) infos[i] = found[i]->info;
  rd_mods_order(infos, order, n);
  for (int k = 0; k < n; k++) {
    rd_mod_slot *s = found[order[k]];
    s->index = g_nmods;
    g_mods[g_nmods++] = s;
    rd_mods_api_fill(s);
    char path[RD_PATH_MAX];
    const int napk = path_join(path, s->dir, FILES_APK) ? 0
                   : add_files(s->index, RD_BASE_APK_NAME, path, "", 0);
    const int nobb = path_join(path, s->dir, FILES_OBB) ? 0
                   : add_files(s->index, RD_GAME_OBB_NAME, path, "", 0);
    rd_ini *patches = path_join(path, s->dir, PATCH_INI) ? NULL : read_ini(path, s->info.id);
    rd_ini *cars = path_join(path, s->dir, CARS_INI) ? NULL : read_ini(path, s->info.id);
    const int nvalues = rd_mods_patches_load(s->info.id, patches, cars);
    free(patches);
    free(cars);
    const int code = load_code(s);
    debugPrintf(RD_LOG_TAG_MODS " loaded %s %s (api %d): %s, %d patch/car values, %d apk + %d obb files\n",
                s->info.id, s->info.version, s->info.api,
                code > 0 ? "mod.so" : code < 0 ? "mod.so FAILED" : "no code",
                nvalues, napk, nobb);
  }
  if (g_rd_mods_nicalls) rd_icall_add_table(g_rd_mods_icalls);
  if (g_nmods) rd_overlay_set_mod_draw(draw_text);
}
