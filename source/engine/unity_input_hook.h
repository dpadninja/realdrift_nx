/* unity_input_hook.h -- the Switch touch state as UnityEngine.Input sees it (icall
 * replacements). See unity_input_hook.c.
 * Forked from badpiggies_nx by ChanseyIsTheBest (https://github.com/ChanseyIsTheBest/badpiggies_nx,
 * commit 7726be3), from the so-loader lineage; changed for realdrift_nx. MIT (see LICENSE). */
#ifndef UNITY_INPUT_HOOK_H
#define UNITY_INPUT_HOOK_H

#include <stdint.h>


/* current number of touches being reported to the game (frame-spike diagnostics) */
int nx_hook_touch_count(void);

/* Max simultaneous fingers we report to Unity (Switch panel tracks up to 16; 10 is plenty). */
#define NX_MAX_TOUCH 10

/* One finger, in Unity screen space (bottom-left origin, game px). `id` is the HID finger id:
 * it must stay stable while the finger is down so phases (Began/Moved/Ended) track correctly. */
typedef struct { int id; float x, y; } NxTouchIn;

/* Push ALL currently-down fingers each frame (n may be 0). Phases are derived by matching
 * finger ids against last frame. This is the multi-touch entry point. */
void nx_input_hook_update_multi(const NxTouchIn *in, int n);

/* Name -> replacement table for UnityEngine.Input icalls (rd_icall.h). */
#include "rd_icall.h"
const rd_icall_override *nx_input_icall_table(void);

#endif /* UNITY_INPUT_HOOK_H */
