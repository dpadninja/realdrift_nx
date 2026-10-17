/* rd_race.h -- race HUD for a controller: hide the on-screen driving controls and pause on a
 * button. Pure logic in rd_race.c (host-tested, tests/c/test_rd_race.c); the il2cpp glue
 * (UIManager.Update wrapper) in rd_race_hook.c.
 * MIT (see LICENSE). */
#ifndef RD_RACE_H
#define RD_RACE_H

/* What a pause-button press does in a race. */
enum { RD_PAUSE_NONE, RD_PAUSE_ENABLE, RD_PAUSE_DISABLE };
/* The RD_PAUSE_* a press of the pause button means, given the pause panel's state; no action in
 * photo mode or after the race ended. */
int rd_pause_action(int pressed, int pause_panel_active, int photo_mode, int race_ended);

/* ---- glue (rd_race_hook.c) ----------------------------------------------------------- */
/* Hooks UIManager.Update (classes from Assembly-CSharp image / UnityEngine.CoreModule image).
 * 1 = hooked, 0 = classes not loaded yet, -1 = failed for good. */
int rd_race_ui_install(void *img_game, void *img_core);
int rd_race_ui_installed(void);
/* 1 while a race HUD is on screen (UIManager.Update ran in the last frames). */
int rd_race_active(void);

#endif
