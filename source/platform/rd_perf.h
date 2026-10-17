/* rd_perf.h -- render resolution choice and CPU boost during loads (pure logic).
 * Host-testable (tests/c/test_rd_perf.c).
 * MIT (see LICENSE). */
#ifndef RD_PERF_H
#define RD_PERF_H

#include <stdint.h>

/* config.txt render_resolution: auto | 720 | 1080 */
enum { RD_RES_AUTO = 0, RD_RES_720 = 720, RD_RES_1080 = 1080 };

/* The two render sizes (landscape): the handheld panel and a docked 1080p TV. */
#define RD_HANDHELD_W 1280
#define RD_HANDHELD_H 720
#define RD_DOCKED_W   1920
#define RD_DOCKED_H   1080

/* auto = 1280x720 in handheld (the panel's native size: rendering 1080p there only to be
 * downscaled cost 2.25x the pixels -- smoke/particle overdraw is fill-rate bound) and
 * 1920x1080 docked. Chosen once at boot. */
void rd_pick_resolution(int setting, int docked, int *w, int *h);

/* CPU boost while the engine is stuck in a long (loading) frame.
 * Boost turns on once the current frame has run longer than `threshold_ns`, and stays on
 * until `tail_ns` after the last such frame ended (scene loads come as several long frames
 * separated by short ones). ApmCpuBoostMode_FastLoad also lowers the GPU clock, so it must
 * never be left on during normal gameplay -- hence a threshold well above any normal frame. */
typedef struct {
  int      on;
  uint64_t until_ns;
} rd_boost;

#define RD_BOOST_CPU_MHZ      "1785"   /* ApmCpuBoostMode_FastLoad CPU clock (config.txt help) */
#define RD_BOOST_THRESHOLD_NS (250ull * 1000 * 1000)
#define RD_BOOST_TAIL_NS      (500ull * 1000 * 1000)

/* `in_frame` = main thread is inside nativeRender, started at `frame_start_ns`.
 * Returns the new desired state (also stored in b->on). */
int rd_boost_update(rd_boost *b, uint64_t now_ns, int in_frame, uint64_t frame_start_ns,
                    uint64_t threshold_ns, uint64_t tail_ns);

#endif
