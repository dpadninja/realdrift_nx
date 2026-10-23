/* opensles_mixer.c -- the audio output behind the OpenSL ES shim: one SDL2 device at the
 * device rate; every playing player is resampled, gain-scaled and summed into it in the SDL
 * audio callback, which also fires each player's buffer-queue callback as Android would.
 *
 * Split out of opensles.c, forked from badpiggies_nx by ChanseyIsTheBest
 * (https://github.com/ChanseyIsTheBest/badpiggies_nx, commit 7726be3); changed for realdrift_nx.
 * MIT (see LICENSE). */

#include <string.h>
#include "opensles.h"
#include "util.h"
#include "opensles_internal.h"
#include "config.h"   /* debug_audio */
#include "rd_audiowatch.h"

// --- global SDL device + player registry ------------------------------------

SDL_AudioDeviceID g_dev = 0;
static int g_dev_rate = 44100;
Player *g_players[MAX_PLAYERS];
int g_player_count = 0;
SDL_mutex *g_reg_lock = NULL;



// Read one sample (at sample-index k within the buffer) and return it scaled to
// signed-16-bit range, regardless of the source format. The shim's accumulator
// and SDL device are S16; FMOD's master output player can be 16-bit int, 32-bit
// int, or 32-bit float, so normalise here.
static inline int32_t read_sample_s16(const void *buf, long k, int sbytes, int is_float) {
  if (is_float) {
    float f = ((const float *)buf)[k];
    if (f > 1.0f) f = 1.0f; else if (f < -1.0f) f = -1.0f;
    return (int32_t)(f * 32767.0f);
  }
  if (sbytes == 4)
    return ((const int32_t *)buf)[k] >> 16;   // S32 -> S16 range
  return (int32_t)((const int16_t *)buf)[k];  // S16
}

// mix one playing player into the S16 stereo accumulator (int32 to avoid clip).
// cur/cur_pos are touched only by this (audio) thread, so they need no lock;
// only the buffer queue is shared with Enqueue. Critically, the engine's
// completion callback is fired WITHOUT our lock held -- Android's contract --
// otherwise the engine's mixer thread (holding its own mutex, calling Enqueue
// which wants our lock) deadlocks against us.
static void mix_player(Player *p, int32_t *acc, int frames) {
  if (!p->playing)
    return;

  // A playing player with nothing queued is a finished one-shot SE the engine
  // fired and never Destroy'd; count the dry callbacks so alloc can recycle it.
  SDL_LockMutex(p->lock);
  const int dry = (!p->cur) && (p->q_head == p->q_tail);
  SDL_UnlockMutex(p->lock);
  if (dry) { if (p->drained < (1 << 20)) p->drained++; return; }
  p->drained = 0;

  const float g = p->muted ? 0.0f : p->gain;
  const int stereo = (p->channels >= 2);
  const int sbytes = p->sbytes > 0 ? p->sbytes : 2;    // bytes per sample
  const int is_float = p->is_float;
  const int bps = stereo ? sbytes * 2 : sbytes;        // bytes per input frame
  // resample the player's own rate to the device rate (players come in at 22050
  // AND 44100; without this, off-rate voices play at the wrong speed/pitch).
  const double ratio = g_dev_rate > 0 ? (double)p->rate / (double)g_dev_rate : 1.0;

  for (int i = 0; i < frames; i++) {
    // ensure cur holds a buffer whose integer sample index covers cur_fpos,
    // carrying the fractional remainder across buffer boundaries.
    for (;;) {
      if (!p->cur) {
        SDL_LockMutex(p->lock);
        const int have = (p->q_head != p->q_tail);
        BQBuffer b = { NULL, 0 };
        if (have) {
          b = p->q[p->q_head];
          p->q_head = (p->q_head + 1) % BQ_SLOTS;
        }
        SDL_UnlockMutex(p->lock);
        if (!have) {
          p->underruns++;
          return; // underrun: rest of the block stays silent
        }
        p->cur = b.data;
        p->cur_size = b.size;
      }
      const long n = (long)(p->cur_size / (SLuint32)bps);
      if (n > 0 && (long)p->cur_fpos < n)
        break; // position is inside the current buffer
      // buffer consumed (or empty): carry remainder, notify engine, fetch next
      p->cur_fpos -= (double)n;
      if (p->cur_fpos < 0.0) p->cur_fpos = 0.0;
      p->cur = NULL;
      if (p->cb) {
        p->callbacks++;
        static int once = 0;
        if (!once) { once = 1; debugPrintf("[fmod] OpenSL buffer-queue callback firing -> FMOD mixer is producing PCM\n"); }
        p->cb(&p->bq_vt, p->cb_ctx);
      }
    }

    const long n = (long)(p->cur_size / (SLuint32)bps);
    const long idx = (long)p->cur_fpos;
    const double frac = p->cur_fpos - (double)idx;
    const void *s = p->cur;
    int32_t l, r;
    if (stereo) {
      const long j0 = idx * 2, j1 = (idx + 1 < n ? idx + 1 : idx) * 2;
      const int32_t l0 = read_sample_s16(s, j0,     sbytes, is_float);
      const int32_t l1 = read_sample_s16(s, j1,     sbytes, is_float);
      const int32_t r0 = read_sample_s16(s, j0 + 1, sbytes, is_float);
      const int32_t r1 = read_sample_s16(s, j1 + 1, sbytes, is_float);
      l = (int32_t)(l0 * (1.0 - frac) + l1 * frac);
      r = (int32_t)(r0 * (1.0 - frac) + r1 * frac);
    } else {
      const int32_t a  = read_sample_s16(s, idx, sbytes, is_float);
      const int32_t b2 = (idx + 1 < n) ? read_sample_s16(s, idx + 1, sbytes, is_float) : a;
      l = r = (int32_t)(a * (1.0 - frac) + b2 * frac);
    }
    acc[i * 2 + 0] += (int32_t)(l * g);
    acc[i * 2 + 1] += (int32_t)(r * g);
    p->cur_fpos += ratio;
  }
}

/* debug_audio: SDL callbacks so far (opensles_audio_watch checks that it keeps growing). */
static volatile uint32_t g_device_blocks;

/* debug_audio: one line per player -- state, gain, queue and its counters. Caller holds
 * g_reg_lock. */
static void log_players_locked(void) {
  for (int i = 0; i < g_player_count; i++) {
    Player *p = g_players[i];
    if (!p) continue;
    SDL_LockMutex(p->lock);
    const int queued = (p->q_tail - p->q_head + BQ_SLOTS) % BQ_SLOTS;
    debugPrintf("[audio]   player %d %p: in_use %d playing %d gain %.3f muted %d queued %d cur %d "
                "cb %d | enqueued %u callbacks %u underruns %u drained %d\n",
                i, (void *)p, p->in_use, p->playing, (double)p->gain, p->muted, queued,
                p->cur != NULL, p->cb != NULL, (unsigned)p->enqueued, (unsigned)p->callbacks,
                (unsigned)p->underruns, p->drained);
    SDL_UnlockMutex(p->lock);
  }
}

/* SDL audio thread: mixes every playing player into the device buffer (fixed-point, clipped)
 * and fires their buffer-queue callbacks, with its own bionic TLS so FMOD's callback finds its
 * stack guard. */
static void SDLCALL audio_callback(void *ud, Uint8 *stream, int len) {
  (void)ud;

  // The engine's completion callback (fired from here via mix_player) reads its
  // stack-guard from tpidr_el0+0x28; SDL's audio thread never set that up. Give
  // this thread its OWN bionic TLS block (single audio thread, so static is fine
  // and persists for its lifetime).
  static uint8_t audio_tls[BIONIC_TLS_SIZE] __attribute__((aligned(16)));
  static int tls_ready = 0;
  if (!tls_ready) { install_bionic_tls(audio_tls); tls_ready = 1; }

  const int frames = len / 4; // S16 stereo
  static int32_t acc[8192 * 2];
  if (frames > 8192) { memset(stream, 0, len); return; }
  memset(acc, 0, frames * 2 * sizeof(int32_t));

  SDL_LockMutex(g_reg_lock);
  for (int i = 0; i < g_player_count; i++)
    if (g_players[i] && g_players[i]->in_use)
      mix_player(g_players[i], acc, frames);
  SDL_UnlockMutex(g_reg_lock);
  __atomic_add_fetch(&g_device_blocks, 1, __ATOMIC_RELAXED);


  int16_t *out = (int16_t *)stream;
  int32_t peak = 0;
  for (int i = 0; i < frames * 2; i++) {
    int32_t v = acc[i];
    if (v > 32767) v = 32767;
    else if (v < -32768) v = -32768;
    out[i] = (int16_t)v;
    int32_t a = v < 0 ? -v : v;
    if (a > peak) peak = a;
  }
  if (peak > 64) {
    static int once = 0;
    if (!once) { once = 1; debugPrintf("[fmod] OpenSL: first non-silent audio to device (peak=%d) -- SOUND IS ON\n", peak); }
  }
  if (config.debug_audio) {
    static rd_audiowatch watch;
    double secs;
    const int ev = rd_audiowatch_block(&watch, frames, peak, g_dev_rate, &secs);
    if (ev != RD_AW_NONE) {
      debugPrintf(ev == RD_AW_SILENT ? "[audio] output silent for %.1f s; players:\n"
                                     : "[audio] sound back after %.1f s of silence; players:\n", secs);
      SDL_LockMutex(g_reg_lock);
      log_players_locked();
      SDL_UnlockMutex(g_reg_lock);
    }
  }
}

void opensles_audio_watch(void) {
  static uint32_t last;
  static int stopped;
  const uint32_t now = __atomic_load_n(&g_device_blocks, __ATOMIC_RELAXED);
  if (now == last && !stopped && g_dev) {
    stopped = 1;
    debugPrintf("[audio] the SDL audio callback stopped (%u blocks so far); players:\n", (unsigned)now);
    SDL_LockMutex(g_reg_lock);
    log_players_locked();
    SDL_UnlockMutex(g_reg_lock);
  } else if (now != last && stopped) {
    stopped = 0;
    debugPrintf("[audio] the SDL audio callback runs again\n");
  }
  last = now;
}

void ensure_device(int rate) {
  (void)rate; // players run at mixed rates (22050/44100); open at the Switch's
              // native 48000 and resample each player in mix_player instead of
              // letting the first player pin the device rate.
  if (!g_reg_lock)
    g_reg_lock = SDL_CreateMutex();
  if (g_dev)
    return;
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
    debugPrintf("opensles: SDL audio init failed: %s\n", SDL_GetError());
    return;
  }
  SDL_AudioSpec want, have;
  SDL_zero(want);
  want.freq = RD_AUDIO_DEVICE_RATE;
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  want.samples = 1024;
  want.callback = audio_callback;
  g_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if (!g_dev) {
    debugPrintf("opensles: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
    return;
  }
  g_dev_rate = have.freq;
  SDL_PauseAudioDevice(g_dev, 0);
}
