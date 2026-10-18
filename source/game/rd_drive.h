/* rd_drive.h -- Switch controller -> Real Drift car controls (pure logic).
 * Host-testable (tests/c/test_rd_drive.c); the il2cpp glue lives in rd_drive_hook.c.
 *
 * The game's MobileCarController reads only its mobile* fields, which the on-screen buttons
 * set through OnThrottlePress/Release, OnBrakePress/Release, ... (dump.cs). We emulate those
 * buttons on press/release EDGES (so the game's own logic runs, e.g. the reverse flag at
 * +0xD3) and write analog steering straight into mobileSteerInput.
 *
 * The mobile controller has no clutch: its GetInput always returns clutchInput = 0, while the
 * car's physics (Drivetrain) does use it. The clutch button therefore sets GetInput's clutch
 * output to 1 after the game's own GetInput ran, for as long as it is held.
 * MIT (see LICENSE). */
#ifndef RD_DRIVE_H
#define RD_DRIVE_H

#include <stdint.h>

enum {
  RD_ACT_THROTTLE, RD_ACT_BRAKE, RD_ACT_HANDBRAKE,
  RD_ACT_GEAR_UP, RD_ACT_GEAR_DOWN, RD_ACT_CAMERA,
  RD_ACT_COUNT
};

typedef struct {
  uint64_t action[RD_ACT_COUNT];   /* button mask per action (any bit held => active) */
  uint64_t steer_left, steer_right;/* digital steering (D-pad) */
  uint64_t clutch;                 /* held => clutch fully pressed */
  float    deadzone;               /* stick deadzone, 0..1 */
} rd_bindings;                     /* part of rd_settings (rd_settings.h) */

typedef struct {
  int   press[RD_ACT_COUNT];       /* 1 on the frame the action became active */
  int   release[RD_ACT_COUNT];     /* 1 on the frame it stopped */
  int   write_steer;               /* 1 if `steer` must be written this frame */
  float steer;                     /* -1 (left) .. +1 (right) */
  int   clutch;                    /* 1 while the clutch button is held */
} rd_drive_out;

typedef struct {
  uint64_t held_prev;
  float    steer_prev;
} rd_drive_state;

/* One frame: `held` = buttons down now, `stick_x` = left stick -1..1. */
void rd_drive_step(const rd_bindings *b, rd_drive_state *st, uint64_t held, float stick_x,
                   rd_drive_out *out);

/* ---- glue (rd_drive_hook.c) ---------------------------------------------------------- */
/* Hooks MobileCarController.GetInput; call until it returns 1 (0 = game not loaded yet). */
int rd_drive_try_install(void);
/* 1 while the car reads input (GetInput ran in the last frames). */
int rd_drive_active(void);

#endif
