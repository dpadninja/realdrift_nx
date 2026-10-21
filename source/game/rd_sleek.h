/* rd_sleek.h -- the game's Graphics Enhancements (SleekRender post-process) without its
 * redundant clears (rd_sleek_hook.c).
 *
 * SleekRenderPostProcess.SetActiveRenderTextureAndClear sets the render target and clears it
 * (GL.Clear color + depth) before every pass. All five SleekRender shaders draw a full-screen
 * quad with Blend One Zero, ZTest Always, ZWrite Off: every pixel is overwritten, so the clears
 * only cost GPU time -- most of it in the full-resolution compose
 * (analysis/research/16-graphics-enhancements.md). The method's clear is branched over.
 * MIT (see LICENSE). */
#ifndef RD_SLEEK_H
#define RD_SLEEK_H

/* Patches the method once Assembly-CSharp-firstpass is loaded; call until it returns 1 (also
 * 1 when it gave up for good: the method was not found or has another shape, logged). */
int rd_sleek_try_install(void);

#endif
