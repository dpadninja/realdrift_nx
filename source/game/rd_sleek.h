/* rd_sleek.h -- the game's Graphics Enhancements (SleekRender post-process): without its
 * redundant clears, and with bloom / vignette chosen in config.txt (rd_sleek_hook.c).
 *
 * SleekRenderPostProcess.SetActiveRenderTextureAndClear sets the render target and clears it
 * (GL.Clear color + depth) before every pass. All five SleekRender shaders draw a full-screen
 * quad with Blend One Zero, ZTest Always, ZWrite Off: every pixel is overwritten, so the clears
 * only cost GPU time -- most of it in the full-resolution compose
 * (analysis/research/16-graphics-enhancements.md). The method's clear is branched over.
 *
 * ge_bloom / ge_vignette: SleekRenderSettings.bloomEnabled / vignetteEnabled of every
 * SleekRenderPostProcess. The component compares them with what it applied last
 * (_isBloomAlreadyEnabled, ...) every frame and switches its shader keywords, so they can be set
 * at any time. The game's settings: bloom with threshold 0 (the whole picture glows and blurs),
 * vignette (dark corners). The pure part (rd_sleek.c) is host-tested.
 * MIT (see LICENSE). */
#ifndef RD_SLEEK_H
#define RD_SLEEK_H

#include <stdint.h>

/* Patches the method once Assembly-CSharp-firstpass is loaded; call until it returns 1 (also
 * 1 when it gave up for good: the method was not found or has another shape, logged). */
int rd_sleek_try_install(void);

#define RD_SLEEK_FRAMES 30   /* frames between checks of the effect's settings */

/* Sets the bool *flag (a C# bool) to `want` (0 / 1). Returns 1 if it changed. */
int rd_sleek_set_flag(uint8_t *flag, int want);

/* Main loop, between engine frames: applies ge_bloom / ge_vignette to the effect's settings
 * every RD_SLEEK_FRAMES frames. Does nothing when both are 1 (the game's own). Main thread. */
void rd_sleek_frame(void);

#endif
