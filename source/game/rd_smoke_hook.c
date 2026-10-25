/* rd_smoke_hook.c -- applies smoke_size / smoke_amount to the game's tyre smoke (see
 * rd_smoke.h). MIT (see LICENSE).
 *
 * The engine icall ParticleSystem::Internal_EmitOld is wrapped at registration (rd_icall.h).
 * For a particle system whose GameObject is a smoke system it drops a share of the particles
 * and scales the particle's start size before handing it to the engine. The smoke systems
 * (ParticleTrack, ParticleGrass, ...) are found by their renderer's material,
 * RD_SMOKE_MATERIAL, from the race HUD hook every RD_SMOKE_SCAN_FRAMES -- they are created
 * with the track and per surface. Outside a race the set is empty, so nothing is touched.
 * Independently of the knobs, nothing is emitted while Time.timeScale is 0 (the pause menu):
 * see rd_smoke.h.
 * icalls take the managed arguments only, `this` first. */
#include <stdint.h>
#include "rd_smoke.h"
#include "rd_il2cpp.h"
#include "rd_game_symbols.h"
#include "rd_platform.h"
#include "config.h"
#include "util.h"

typedef void (*emit_old_fn)(void *ps, void *particle);   /* (this, ref Particle) */

static emit_old_fn g_emit_old;                           /* the engine's, set at registration */
static struct {
  void *(*find_objects)(void *type);                     /* Object[] FindObjectsOfType(Type) */
  void *(*object_name)(void *obj);                       /* string */
  void *(*game_object)(void *component);
  void *(*shared_material)(void *renderer);
  void  *type_renderer;                                  /* System.Type */
  uint32_t start_size;                                   /* Particle.m_StartSize offset */
} g;

static int g_state;                                      /* 0 = not tried, 1 = ready, -1 = off */
static rd_smoke_set g_smoke;                             /* smoke GameObjects of this race */
static uint32_t g_last_frame, g_last_scan;
static int g_acc;                                        /* rd_smoke_keep remainder */
static float (*g_time_scale)(void);                      /* Time.get_timeScale icall */
static int g_time_scale_tried;

/* 1 if game time runs (Time.timeScale > 0), looked up on the first emit (icalls are all
 * registered by then); 1 if the icall is missing (logged once), as before. */
static int time_runs(void) {
  if (!g_time_scale_tried) {
    g_time_scale_tried = 1;
    g_time_scale = g_il2cpp.resolve_icall ? g_il2cpp.resolve_icall(RD_ICALL_TIME_SCALE) : NULL;
    if (!g_time_scale)
      debugPrintf("[smoke] icall " RD_ICALL_TIME_SCALE " not found: smoke is emitted on pause\n");
  }
  return !g_time_scale || rd_smoke_emit_allowed(g_time_scale());
}

/* Wrapper of ParticleSystem::Internal_EmitOld: nothing while time stands still; in a race, for
 * the smoke systems, drops a share of the particles and scales the size of the rest, then calls
 * the engine. */
static void ic_emit_old(void *ps, void *particle) {
  if (!time_runs()) return;
  const int in_race = port_frame_count() - g_last_frame <= RD_ACTIVE_WINDOW_FRAMES;
  if (g_state > 0 && in_race && ps && particle && g_smoke.n &&
      rd_smoke_set_has(&g_smoke, g.game_object(ps))) {
    if (!rd_smoke_keep(&g_acc, config.smoke_amount)) return;
    float *size = (float *)((char *)particle + g.start_size);
    for (int i = 0; i < 3; i++) size[i] = rd_smoke_scale_size(size[i], config.smoke_size);
  }
  if (g_emit_old) g_emit_old(ps, particle);
}

static const rd_icall_override g_icalls[] = {
  { RD_ICALL_EMIT_OLD, (void *)ic_emit_old, (void **)&g_emit_old },
  { NULL, NULL, NULL },
};
const rd_icall_override *rd_smoke_icall_table(void) { return g_icalls; }

/* An engine icall by name, or NULL (logged and counted in *missing). */
static void *icall(const char *name, int *missing) {
  void *p = g_il2cpp.resolve_icall ? g_il2cpp.resolve_icall(name) : NULL;
  if (!p) { debugPrintf("[smoke] icall %s not found\n", name); (*missing)++; }
  return p;
}

/* Resolves the icalls, the ParticleSystemRenderer type and the Particle.m_StartSize offset
 * once; returns 1 if the smoke hook can work, -1 (logged) if not. */
static int init(void) {
  int missing = 0;
  g.find_objects    = icall(RD_ICALL_FIND_OBJECTS, &missing);
  g.object_name     = icall(RD_ICALL_OBJECT_NAME, &missing);
  g.game_object     = icall(RD_ICALL_GAME_OBJECT, &missing);
  g.shared_material = icall(RD_ICALL_SHARED_MATERIAL, &missing);
  void *renderer = rd_il2cpp_class(&g_il2cpp, RD_ASM_PARTICLES, RD_NS_UNITY, RD_CLS_PARTICLE_RENDERER);
  if (renderer && g_il2cpp.class_get_type && g_il2cpp.type_get_object)
    g.type_renderer = g_il2cpp.type_get_object(g_il2cpp.class_get_type(renderer));
  void *ps = rd_il2cpp_class(&g_il2cpp, RD_ASM_PARTICLES, RD_NS_UNITY, RD_CLS_PARTICLE_SYSTEM);
  void *particle = rd_il2cpp_nested_class(&g_il2cpp, ps, RD_CLS_PARTICLE);
  uint32_t off = 0;
  if (rd_il2cpp_field_offset(&g_il2cpp, particle, RD_FLD_PARTICLE_START_SIZE, &off) &&
      off >= RD_IL2CPP_OBJECT_HEADER)
    g.start_size = off - RD_IL2CPP_OBJECT_HEADER;
  if (!g.type_renderer || !g.start_size || !g_il2cpp.array_length || !g_emit_old) missing++;
  debugPrintf("[smoke] %s (smoke_size=%d%% smoke_amount=%d%%, Particle.%s @0x%x, emit %s)\n",
              missing ? "unavailable, smoke left as is" : "ready",
              config.smoke_size, config.smoke_amount, RD_FLD_PARTICLE_START_SIZE,
              (unsigned)g.start_size, g_emit_old ? "wrapped" : "NOT wrapped");
  return missing ? -1 : 1;
}

/* 1 if the renderer's material is the tyre smoke (RD_SMOKE_MATERIAL). */
static int is_smoke(void *renderer) {
  void *mat = g.shared_material(renderer);
  return mat && rd_il2cpp_str_eq(&g_il2cpp, g.object_name(mat), RD_SMOKE_MATERIAL);
}

/* Adds the GameObjects of all smoke renderers in the scene to the set; logs when the count
 * changes. */
static void scan(void) {
  void *arr = g.find_objects(g.type_renderer);
  const uint32_t n = arr ? g_il2cpp.array_length(arr) : 0;
  const int before = g_smoke.n;
  for (uint32_t i = 0; i < n; i++) {
    void *r = rd_il2cpp_array_at(arr, i);
    if (r && is_smoke(r)) rd_smoke_set_add(&g_smoke, g.game_object(r));
  }
  if (g_smoke.n != before)
    debugPrintf("[smoke] %d smoke system(s) of %u particle renderers\n", g_smoke.n, (unsigned)n);
}

void rd_smoke_race_frame(void) {
  if (!rd_smoke_active(config.smoke_size, config.smoke_amount) || g_state < 0) return;
  const uint32_t now = port_frame_count();
  if (now - g_last_frame > RD_ACTIVE_WINDOW_FRAMES) {    /* a new race: new systems */
    g_smoke.n = 0;
    g_last_scan = now - RD_SMOKE_SCAN_FRAMES;
  }
  g_last_frame = now;
  if (now - g_last_scan < RD_SMOKE_SCAN_FRAMES) return;
  g_last_scan = now;
  if (!g_state) g_state = init();
  if (g_state > 0) scan();
}
