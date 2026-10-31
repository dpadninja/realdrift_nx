/* rd_gcload.h -- garbage collection on loads only.
 *
 * The loader turns the Boehm GC off before the first frame (main.c: its stop-the-world once
 * hung; rd_gcbridge now stops threads) and nothing turned it back on: the managed heap grew
 * ~18 MB per minute of driving (the race replay recording) and was never collected, through
 * any number of reloads (analysis/research/36). A collection in a race would cost frames, so
 * the GC stays off there and the port collects once after each load: a long frame in which
 * the engine stopped the world (UnloadUnusedAssets, which Unity runs on every scene load and
 * where it would collect on Android). The screen still shows the loading frame meanwhile.
 *
 * Main thread (the engine's), between frames. The decision is pure (tests/c/test_rd_gcload.c);
 * rd_gcload_after_frame is the il2cpp glue. MIT (see LICENSE). */
#ifndef RD_GCLOAD_H
#define RD_GCLOAD_H

#include <stdint.h>

/* 1 if a frame that took `frame_ns`, during which the engine stopped the world `stops` times
 * (the GC bridge count), is a load to collect after: at least RD_GC_LOAD_FRAME_NS and a stop. */
int rd_gcload_due(uint64_t frame_ns, unsigned stops);

/* After every frame: when rd_gcload_due, enables the GC, collects, disables it again (only if
 * it was off) and logs "[gc] collected after a load: used A -> B MB of H MB in N ms". */
void rd_gcload_after_frame(uint64_t frame_ns, unsigned stops);

#endif
