/* rd_ugui.h -- the clickable uGUI elements on screen, for focus navigation (rd_uinav.h).
 *
 * A scan finds every UnityEngine.UI.Selectable (Button, Toggle, Slider, ...) that is active,
 * interactable, on screen and not covered: EventSystem.RaycastAll at its centre must hit it
 * or one of its children, which is also what a tap there would hit (a modal dialog hides the
 * buttons behind it); if a label covers the centre, the quarters are tried (rd_uinav_probe) and
 * the first point that reaches it is where it is tapped. Rectangles come from RectTransform.GetWorldCorners -- screen pixels
 * already for a Screen Space - Overlay canvas, through the canvas camera otherwise. Elements
 * covering more than RD_UGUI_MAX_SHARE of the screen are left out: they are touch areas and
 * dialog backdrops (e.g. the main menu's car-rotation button), not something to focus.
 * Knows Unity, not the game. All names are resolved through il2cpp at run time.
 * Main thread, outside the engine's own C# calls (between frames). MIT (see LICENSE). */
#ifndef RD_UGUI_H
#define RD_UGUI_H

#include "rd_uinav.h"

#define RD_UGUI_MAX_TARGETS 96
#define RD_UGUI_NAME        40       /* GameObject names are cut to this (ASCII) */
#define RD_UGUI_MAX_SHARE   0.5f     /* larger elements are not targets */

enum { RD_UGUI_OTHER, RD_UGUI_SLIDER };   /* rd_ugui_target.kind */

typedef struct {
  rd_uinav_target t;                 /* id = the Selectable; rect: screen px, top-left origin */
  int  kind;                         /* RD_UGUI_SLIDER for a UnityEngine.UI.Slider */
  char name[RD_UGUI_NAME];           /* its GameObject's name */
  char parent[RD_UGUI_NAME];         /* the parent GameObject's name ("" at the root) */
  char cls[RD_UGUI_NAME];            /* the component's class (Button, Toggle, Slider, ...) */
  float tap_x, tap_y;                /* where a tap reaches it (screen px, top-left origin) */
} rd_ugui_target;

/* Told about each Selectable a scan leaves out (debug logging): its name, why, and for
 * "covered" what a tap would hit instead ("" otherwise). */
typedef void (*rd_ugui_reject_fn)(const char *name, const char *why, const char *hit);

/* A Slider's state. direction is Slider.Direction: 0 left-to-right, 1 right-to-left,
 * 2 bottom-to-top, 3 top-to-bottom. */
typedef struct { float value, min, max; int whole, direction; } rd_ugui_slider;

/* Resolves the classes, methods and icalls once. 1 = ready, 0 = UnityEngine.UI not loaded
 * yet (call again), -1 = something is missing (logged once; navigation stays off). */
int rd_ugui_init(void);

/* Fills out[0..max) with the targets on a screen_w x screen_h screen; returns how many.
 * reject may be NULL. */
int rd_ugui_scan(rd_ugui_target *out, int max, int screen_w, int screen_h,
                 rd_ugui_reject_fn reject);

/* 1 if `obj` (any UnityEngine.Object) is still alive: its native object exists
 * (m_CachedPtr != 0, what `obj != null` tests in C#). A destroyed object's managed shell
 * outlives a scene change; an icall on it throws a managed exception, and one thrown under our
 * own C frames (rd_menu_frame calls between engine frames) cannot be caught: the process ends.
 * So anything kept across frames is checked before use. 0 for NULL or before rd_ugui_init. */
int rd_ugui_alive(const void *obj);

/* Reads a Slider target (id of a target of kind RD_UGUI_SLIDER from the latest scan). */
void rd_ugui_slider_get(uintptr_t id, rd_ugui_slider *out);
/* Slider.value = v: the slider clamps it and calls the game's onValueChanged, as a drag does. */
void rd_ugui_slider_set(uintptr_t id, float v);

#endif
