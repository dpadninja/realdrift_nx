/* rd_quality.h -- QualitySettings.antiAliasing of the current quality level, read and written
 * by the port. The game runs on level 0 "Fastest" (antiAliasing 0) and the engine's setter is
 * not registered in this game, so the field is located through the registered getter
 * (rd_find_quality_aa) and written directly. Cameras that allow MSAA (all the game's
 * main cameras) then render their targets with that many samples (analysis/research/34).
 * Main thread, after il2cpp is up. MIT (see LICENSE). */
#ifndef RD_QUALITY_H
#define RD_QUALITY_H

/* Sets the current quality level's antiAliasing to `samples` (0, 2, 4 or 8). Returns the value
 * it had, or -1 if the field could not be located (logged once). */
int rd_quality_set_aa(int samples);

#endif
