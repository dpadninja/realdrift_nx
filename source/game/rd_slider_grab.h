/* rd_slider_grab.h -- a slider edited with the controller is "held" the way a finger holds it,
 * for the game scripts that only follow a slider while it is pressed.
 *
 * The replay (ReplayManager) moves its slider with the playback every frame and reads it only
 * while ReplayManager.isReplaySliderPressed is set by the slider's touch handlers
 * (OnReplaySliderPress / OnReplaySliderRelease): a value set from the controller was undone the
 * next frame. Menu navigation (rd_menu_hook.c) calls rd_slider_grab when it starts and stops
 * editing a slider; for the replay slider this calls the same handlers as a touch, so the
 * replay pauses while the value is edited and continues from it afterwards. Other sliders are
 * left alone. Names are resolved through il2cpp (rd_game_symbols.h). Main thread, between
 * engine frames. MIT (see LICENSE). */
#ifndef RD_SLIDER_GRAB_H
#define RD_SLIDER_GRAB_H

/* Starts (down = 1) or ends (down = 0) holding `slider` (a UnityEngine.UI.Slider). Calls the
 * replay's press / release handler when `slider` is the replay's slider; does nothing for any
 * other slider, or when the game's ReplayManager is not found. An end without a start, or a
 * start of a slider already held, is ignored. */
void rd_slider_grab(void *slider, int down);

#endif
