/* rd_menu.h -- the game's menus with a controller (config.txt menu_navigation): a focus frame
 * moved with the D-pad or the left stick, A taps the focused element, B goes back.
 *
 * The targets are the game's uGUI elements on screen (rd_ugui.h), the moves come from
 * rd_uinav.h. "Tap" is a synthetic finger at the element's centre (nx_input_hook_synth), so
 * the game sees an ordinary touch and every kind of clickable reacts. Real Drift has no
 * generic "back" (Escape only opens the EXIT dialog of the main menu), so B taps the screen's
 * back button: the one its rule names, else a visible element named like a back button, else
 * Escape is reported for one frame. Rules (rd_menu_rule) describe single screens; with
 * debug_ui_nav = 1 every new screen's targets are logged, which is what rules are written from.
 * Active outside races, and in a race only on the pause menu or after the finish.
 * Pure logic here (host-tested, tests/c/test_rd_menu.c); the glue in rd_menu_hook.c.
 * MIT (see LICENSE). */
#ifndef RD_MENU_H
#define RD_MENU_H

#include <stdint.h>
#include "rd_ugui.h"
#include "rd_uinav.h"
#include "rd_icall.h"

#define RD_MENU_SCAN_FRAMES   6       /* targets are re-read this often (and after each tap) */
#define RD_MENU_TAP_FRAMES    2       /* frames the synthetic finger stays down */
#define RD_MENU_RACE_FRAMES   60      /* in a race, the RaceManager is looked up this often */
#define RD_MENU_KEY_ESCAPE    27      /* KeyCode.Escape = Android Back */
#define RD_MENU_FRAME_PX_720  3.0f    /* focus frame thickness at 720p (scales with height) */
#define RD_MENU_FRAME_RGBA    1.0f, 0.78f, 0.1f, 0.95f
#define RD_MENU_EDIT_RGBA     0.1f, 0.9f, 0.95f, 0.95f  /* the frame while a slider is edited */
#define RD_MENU_EDIT_PX_SCALE 1.6f    /* ...and it is this much thicker */
#define RD_MENU_SLIDER_STEPS  20      /* a non-integer slider moves 1/20 of its range a press */

/* One screen's navigation rule. `screen` is the name of an element (or of an element's parent)
 * that only that screen shows; the others name the elements to use, NULL = default. A name
 * may be "Parent/Name" where several elements share a name. */
typedef struct {
  const char *screen;
  const char *back;                  /* what B taps */
  const char *first;                 /* where the focus starts */
  const char *prev, *next;           /* what L / R tap (car arrows, tabs) */
} rd_menu_rule;

/* 1 if a GameObject name looks like a back button: contains "back", "close", "return" or
 * "cancel" in any case, but not as part of "background" / "backdrop". */
int rd_menu_is_back_name(const char *name);

/* 1 if a name looks like a "previous" control (contains "ArrowLeft" or "Prev", any case) or,
 * for next_name, a "next" one ("ArrowRight" or "Next"). */
int rd_menu_is_prev_name(const char *name);
int rd_menu_is_next_name(const char *name);

/* What L (step -1) or R (step +1) taps: the rule's prev / next element, else the first target
 * with a prev- / next-like name; -1 = none. */
int rd_menu_step_target(const rd_ugui_target *t, int n, const rd_menu_rule *rule, int step);

/* The first target named `name` ("Name" or "Parent/Name"), or -1. */
int rd_menu_find(const rd_ugui_target *t, int n, const char *name);

/* The rule whose screen is on display (a target or a target's parent carries its name), or
 * NULL. */
const rd_menu_rule *rd_menu_rule_for(const rd_menu_rule *rules, int nrules,
                                     const rd_ugui_target *t, int n);

/* What B taps: the rule's back element, else the topmost-then-leftmost target with a
 * back-like name; -1 = none (report Escape). */
int rd_menu_back_target(const rd_ugui_target *t, int n, const rd_menu_rule *rule);

/* 1 if the navigator should run: enabled, no loading screen up, and no car is being driven
 * unless the race is paused or over. */
int rd_menu_wanted(int enabled, int loading, int driving, int paused, int race_ended);

/* Order-independent fingerprint of a target list (logging a screen once). */
uint32_t rd_menu_signature(const rd_ugui_target *t, int n);

/* Editing a slider ("focus inside the focus"): A on a slider starts it, the direction along the
 * slider changes the value, A keeps the new value, B puts the old one back. */
typedef struct {
  int   on;
  float orig, value, min, max, step;
} rd_menu_edit;

/* Starts editing a slider at `value` in [min, max]; integer sliders step by 1, others by
 * (max - min) / RD_MENU_SLIDER_STEPS. */
void rd_menu_edit_begin(rd_menu_edit *e, float value, float min, float max, int whole);
/* The value change a direction means for a slider of Slider.Direction `direction` (0 left to
 * right, 1 right to left, 2 bottom to top, 3 top to bottom): +1, -1, or 0 for a direction
 * across the slider. */
int rd_menu_edit_delta(int dir, int direction);
/* Moves the value by delta steps within [min, max]; returns the new value. */
float rd_menu_edit_move(rd_menu_edit *e, int delta);
/* Stops editing; returns the value the slider should keep (the original one if cancel). */
float rd_menu_edit_end(rd_menu_edit *e, int cancel);

/* ---- glue (rd_menu_hook.c) ----------------------------------------------------------- */
/* Resolves the uGUI lookups once menu_navigation is on and UnityEngine.UI is loaded.
 * 0 = not yet (call again), 1 = done (on, off or failed; logged). */
int rd_menu_try_install(void);
/* Once per frame on the main thread, after the engine's frame: reads the pad, moves the focus,
 * taps, goes back. */
void rd_menu_frame(void);
/* The focus frame; from the eglSwapBuffers wrapper. */
void rd_menu_draw(void);
/* Wraps Input.GetKeyDownInt to report Escape for B (rd_icall.h, shim_dl.c). */
const rd_icall_override *rd_menu_icall_table(void);

#endif
