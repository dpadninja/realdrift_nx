/* rd_audioout.c -- see rd_audioout.h. MIT (see LICENSE). */
#include <malloc.h>
#include <string.h>
#include <switch.h>
#include "rd_audioout.h"
#include "util.h"

#define BUFFERS        3          /* 3 x 21 ms queued: a late thread is a click, not a gap */
#define BUFFER_FRAMES  1024
#define BUFFER_BYTES   (BUFFER_FRAMES * 2 * 2)          /* S16 stereo; a multiple of 0x1000 */
#define THREAD_STACK   0x40000    /* FMOD's mixer callback runs on it */
#define THREAD_PRIO    0x2B       /* above the engine's threads (0x2C); Horizon: lower = higher */
#define THREAD_CORE    2          /* away from core 0, where every engine thread runs */
#define WAIT_NS        100000000ull                     /* re-check `g_stop` every 100 ms */

static AudioOutBuffer g_buf[BUFFERS];
static Thread g_thread;
static rd_audioout_fill g_fill;
static void *g_ctx;
static volatile int g_stop;
static int g_open;

/* A played buffer handed back by audout, or NULL after WAIT_NS. Takes one already released
 * before waiting, so buffers released together are all refilled. */
static AudioOutBuffer *next_released(void) {
  AudioOutBuffer *b = NULL;
  u32 n = 0;
  if (R_SUCCEEDED(audoutGetReleasedAudioOutBuffer(&b, &n)) && n && b) return b;
  b = NULL;
  n = 0;
  if (R_FAILED(audoutWaitPlayFinish(&b, &n, WAIT_NS)) || !n) return NULL;
  return b;
}

static void output_main(void *arg) {
  (void)arg;
  while (!g_stop) {
    AudioOutBuffer *b = next_released();
    if (!b || g_stop) continue;
    g_fill(g_ctx, b->buffer, (int)b->data_size);
    audoutAppendAudioOutBuffer(b);
  }
}

/* THREAD_CORE when the process may use it (not as an applet), else the default core. */
static int pick_core(void) {
  u64 mask = 0;
  if (R_FAILED(svcGetInfo(&mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0))) mask = 0;
  return (mask & (1ull << THREAD_CORE)) ? THREAD_CORE : -2;
}

static void free_buffers(void) {
  for (int i = 0; i < BUFFERS; i++) {
    free(g_buf[i].buffer);
    g_buf[i].buffer = NULL;
  }
}

int rd_audioout_open(rd_audioout_fill fill, void *ctx) {
  if (g_open) return (int)audoutGetSampleRate();
  Result rc = audoutInitialize();
  if (R_FAILED(rc)) { debugPrintf("[audio] audoutInitialize failed: 0x%x\n", rc); return 0; }
  rc = audoutStartAudioOut();
  if (R_FAILED(rc)) {
    debugPrintf("[audio] audoutStartAudioOut failed: 0x%x\n", rc);
    audoutExit();
    return 0;
  }
  g_fill = fill;
  g_ctx = ctx;
  for (int i = 0; i < BUFFERS; i++) {
    void *mem = memalign(0x1000, BUFFER_BYTES);
    if (!mem) { free_buffers(); audoutStopAudioOut(); audoutExit(); return 0; }
    memset(mem, 0, BUFFER_BYTES);
    g_buf[i] = (AudioOutBuffer){ .buffer = mem, .buffer_size = BUFFER_BYTES,
                                 .data_size = BUFFER_BYTES };
    audoutAppendAudioOutBuffer(&g_buf[i]);
  }
  const int core = pick_core();
  g_stop = 0;
  rc = threadCreate(&g_thread, output_main, NULL, NULL, THREAD_STACK, THREAD_PRIO, core);
  if (R_SUCCEEDED(rc)) rc = threadStart(&g_thread);
  if (R_FAILED(rc)) {
    debugPrintf("[audio] output thread failed: 0x%x\n", rc);
    threadClose(&g_thread);
    audoutStopAudioOut();
    audoutExit();
    free_buffers();
    return 0;
  }
  g_open = 1;
  debugPrintf("[audio] audout %u Hz, %d x %d frames, thread on core %d\n",
              audoutGetSampleRate(), BUFFERS, BUFFER_FRAMES, core);
  return (int)audoutGetSampleRate();
}

void rd_audioout_close(void) {
  if (!g_open) return;
  g_stop = 1;
  threadWaitForExit(&g_thread);
  threadClose(&g_thread);
  audoutStopAudioOut();
  audoutExit();
  free_buffers();
  g_open = 0;
}
