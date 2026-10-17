/* sources: source/game/rd_drive.c */
#include <string.h>
#include "check.h"
#include "rd_drive.h"

enum { ZR = 1 << 0, ZL = 1 << 1, B = 1 << 2, R = 1 << 3, L = 1 << 4, Y = 1 << 5,
       LEFT = 1 << 6, RIGHT = 1 << 7 };

static rd_bindings bindings(void) {
  rd_bindings b;
  memset(&b, 0, sizeof b);
  b.action[RD_ACT_THROTTLE] = ZR;  b.action[RD_ACT_BRAKE] = ZL;
  b.action[RD_ACT_HANDBRAKE] = B;  b.action[RD_ACT_GEAR_UP] = R;
  b.action[RD_ACT_GEAR_DOWN] = L;  b.action[RD_ACT_CAMERA] = Y;
  b.steer_left = LEFT; b.steer_right = RIGHT; b.deadzone = 0.2f;
  return b;
}

static void test_press_and_release_edges(void) {
  rd_bindings b = bindings(); rd_drive_state st = {0}; rd_drive_out o;
  rd_drive_step(&b, &st, ZR, 0, &o);
  CHECK(o.press[RD_ACT_THROTTLE] && !o.release[RD_ACT_THROTTLE]);
  rd_drive_step(&b, &st, ZR, 0, &o);                     /* held: no new edge */
  CHECK(!o.press[RD_ACT_THROTTLE] && !o.release[RD_ACT_THROTTLE]);
  rd_drive_step(&b, &st, ZR | B, 0, &o);
  CHECK(o.press[RD_ACT_HANDBRAKE] && !o.press[RD_ACT_THROTTLE]);
  rd_drive_step(&b, &st, 0, 0, &o);
  CHECK(o.release[RD_ACT_THROTTLE] && o.release[RD_ACT_HANDBRAKE]);
}

static void test_stick_deadzone_and_rescale(void) {
  rd_bindings b = bindings(); rd_drive_state st = {0}; rd_drive_out o;
  rd_drive_step(&b, &st, 0, 0.1f, &o);
  CHECK(!o.write_steer && o.steer == 0.0f);              /* inside deadzone, never steered */
  rd_drive_step(&b, &st, 0, 1.0f, &o);
  CHECK(o.write_steer && o.steer == 1.0f);
  rd_drive_step(&b, &st, 0, -0.6f, &o);
  CHECK(o.steer < -0.49f && o.steer > -0.51f);           /* (0.6-0.2)/0.8 = 0.5 */
  rd_drive_step(&b, &st, 0, -2.0f, &o);
  CHECK(o.steer == -1.0f);                               /* clamped */
}

static void test_return_to_centre_written_once(void) {
  rd_bindings b = bindings(); rd_drive_state st = {0}; rd_drive_out o;
  rd_drive_step(&b, &st, 0, 0.9f, &o);
  rd_drive_step(&b, &st, 0, 0.0f, &o);
  CHECK(o.write_steer && o.steer == 0.0f);               /* releases the wheel */
  rd_drive_step(&b, &st, 0, 0.0f, &o);
  CHECK(!o.write_steer);                                 /* then leaves it to touch input */
}

static void test_dpad_steering_when_stick_centred(void) {
  rd_bindings b = bindings(); rd_drive_state st = {0}; rd_drive_out o;
  rd_drive_step(&b, &st, LEFT, 0, &o);
  CHECK(o.write_steer && o.steer == -1.0f);
  rd_drive_step(&b, &st, LEFT | RIGHT, 0, &o);
  CHECK(o.steer == 0.0f);
  rd_drive_step(&b, &st, RIGHT, 0.5f, &o);               /* stick wins over D-pad */
  CHECK(o.steer > 0.37f && o.steer < 0.38f);
}

int main(void) {
  RUN(test_press_and_release_edges);
  RUN(test_stick_deadzone_and_rescale);
  RUN(test_return_to_centre_written_once);
  RUN(test_dpad_steering_when_stick_centred);
  return g_failures != 0;
}
