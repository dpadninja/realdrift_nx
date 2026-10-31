/* rd_gcload.h -- garbage collection where the engine asks for it, never on its own.
 *
 * The loader turns the Boehm GC off before the first frame (main.c: its stop-the-world once
 * hung; rd_gcbridge now stops threads) and nothing turned it back on: the managed heap grew
 * ~18 MB per minute of driving (the race replay recording) and was never collected, through
 * any number of reloads (analysis/research/36). Unity still asks for collections where it
 * wants one -- UnloadUnusedAssets on every scene load ("Race again" too), the game's
 * Resources.UnloadUnusedAssets -- by calling il2cpp_gc_collect, which libunity looks up with
 * dlsym; with the GC off those calls did nothing. shim_dl.c hands libunity a wrapper that
 * turns the GC on for that one collection and off again. The automatic collections Boehm
 * would start on its own while allocating stay off, so a race does not lose frames to them.
 * The one request the game made in a race, on every music track change, is removed
 * (rd_racemusic.h).
 *
 * The call sequence is pure (tests/c/test_rd_gcload.c); rd_gcload_hook.c is the il2cpp glue.
 * MIT (see LICENSE). */
#ifndef RD_GCLOAD_H
#define RD_GCLOAD_H

/* The GC functions one collection needs (il2cpp_gc_*). */
typedef struct {
  int  (*is_disabled)(void);
  void (*enable)(void);
  void (*disable)(void);
  void (*collect)(int max_generations);
} rd_gc_ops;

/* What rd_gcload_collect did. */
typedef enum {
  RD_GC_RAN = 0,          /* the GC was on: collected as asked */
  RD_GC_RAN_TURNED_ON,    /* it was off: turned on, collected, turned off again */
  RD_GC_STILL_OFF,        /* still off after one enable (another disable is held): no-op */
} rd_gc_result;

/* One collection of up to `max_generations` with the GC on for it: enables it if it is off,
 * collects, and disables it again only if it was off, so the disable count is unchanged. */
rd_gc_result rd_gcload_collect(const rd_gc_ops *ops, int max_generations);

/* For dlsym("il2cpp_gc_collect") (shim_dl.c): remembers `real` and returns the wrapper,
 * which runs rd_gcload_collect on the engine's thread and logs "[gc] collect ...". */
void *rd_gcload_wrap(void *real);

#endif
