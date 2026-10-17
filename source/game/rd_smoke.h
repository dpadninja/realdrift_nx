/* rd_smoke.h -- optional lighter tyre smoke (config.txt smoke_size, smoke_amount).
 *
 * In heavy smoke the GPU needs 30-80 ms a frame (analysis/research/09-performance.md): the
 * smoke is many large alpha-blended particles. The game emits them one by one with the legacy
 * ParticleSystem.Emit(position, velocity, size, lifetime, color), which lands in the engine
 * icall ParticleSystem::Internal_EmitOld(ref Particle). That icall is wrapped (rd_icall.h):
 * for the smoke systems it scales the particle's start size and lets only a share of the
 * particles through. Both knobs at 100 % = the original game (the default).
 * Pure logic here (host-tested, tests/c/test_rd_smoke.c); the il2cpp glue in rd_smoke_hook.c.
 * MIT (see LICENSE). */
#ifndef RD_SMOKE_H
#define RD_SMOKE_H

#include "rd_icall.h"

#define RD_SMOKE_PERCENT_ORIG 100    /* percent = leave it alone */
#define RD_SMOKE_PERCENT_MIN  10     /* smoke_size / smoke_amount lower limit */
#define RD_SMOKE_MAX_SYSTEMS  32     /* smoke GameObjects tracked per race */

/* 1 if the settings change anything. */
int rd_smoke_active(int size, int amount);

/* Emit filter: 1 = let this particle through. *acc carries the remainder between calls, so
 * exactly `amount` of every 100 particles pass, spread evenly. */
int rd_smoke_keep(int *acc, int amount);

/* A start size scaled to `percent`. */
float rd_smoke_scale_size(float size, int percent);

/* The smoke systems' GameObjects (found by material each rescan). */
typedef struct { const void *go[RD_SMOKE_MAX_SYSTEMS]; int n; } rd_smoke_set;
/* 1 if go is in the set. */
int  rd_smoke_set_has(const rd_smoke_set *s, const void *go);
/* Adds go; NULL, duplicates and additions to a full set are ignored. */
void rd_smoke_set_add(rd_smoke_set *s, const void *go);

/* ---- glue (rd_smoke_hook.c) ---------------------------------------------------------- */
/* Called every race frame (rd_race_hook.c); rescans the smoke systems now and then. */
void rd_smoke_race_frame(void);
/* The Internal_EmitOld wrapper, for rd_icall_setup (libc_shim.c dlsym_fake). */
const rd_icall_override *rd_smoke_icall_table(void);

#endif
