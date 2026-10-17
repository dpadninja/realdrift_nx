/* rd_modhost.h -- the decisions of the mod host that need no Switch: load order, who owns a
 * replaced file or icall, and the per-frame text queue. Host-testable
 * (tests/c/test_rd_modhost.c); the glue that scans the SD card, loads mod.so and talks to
 * il2cpp is rd_mods.c.
 * MIT (see LICENSE). */
#ifndef RD_MODHOST_H
#define RD_MODHOST_H

#include <stdint.h>
#include "rd_mod_ini.h"

#define RD_MODS_MAX 32

/* Sorts order[0..n) (indices into infos) into load order: higher priority first, then id. */
void rd_mods_order(const rd_mod_info *infos, int *order, int n);

/* A claim on a name (an archive member or an icall) by a mod. `key` is owned by the caller and
 * must outlive the table. */
typedef struct { const char *key; int owner; } rd_claim;
typedef struct { rd_claim *c; int n, max; } rd_claims;

/* Claims key for owner. Returns -1 if the claim was taken, the earlier owner's index if
 * another mod holds it (the claim is refused: earlier mods are in load order, so they win),
 * or -2 if the table is full. */
int rd_claim_take(rd_claims *t, const char *key, int owner);

/* Text drawn by mods. Mods add lines during a frame; rd_modtext_swap at the start of the
 * next frame makes them the lines shown until the swap after it. */
#define RD_MODTEXT_MAX   32
#define RD_MODTEXT_LEN   96
typedef struct { float x, y, size; uint32_t rgba; char text[RD_MODTEXT_LEN]; } rd_modtext_line;
typedef struct {
  rd_modtext_line pending[RD_MODTEXT_MAX], shown[RD_MODTEXT_MAX];
  int npending, nshown;
} rd_modtext;

/* Queues a line (truncated to RD_MODTEXT_LEN - 1 chars). Returns 0, or -1 when full. */
int rd_modtext_add(rd_modtext *q, float x, float y, float size, uint32_t rgba, const char *text);

/* The lines added since the last swap become the shown ones; pending is emptied. */
void rd_modtext_swap(rd_modtext *q);

#endif
