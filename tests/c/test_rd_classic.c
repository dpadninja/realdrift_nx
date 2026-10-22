/* sources: source/game/rd_classic.c */
/* libs: -lm */
/* Host unit test of rd_classic (4.9 drift scoring). MIT (see LICENSE). */
#include "check.h"
#include "rd_classic.h"

#define DT 0.02f                                /* FixedUpdate step */

static rd_classic fresh(void) {
  return (rd_classic){ .combo = 1, .time_factor = 1.0f };
}

/* One frame at a given angle (degrees) and speed factor (velocity x 0.1), no proximity. */
static int32_t frame(rd_classic *s, float angle, float speed_factor) {
  const rd_classic_in in = { .active = 1, .angle = angle, .speed_factor = speed_factor,
                             .proximity = 1, .dt = DT };
  return rd_classic_step(s, &in);
}

/* n frames of drift, then `gap` frames of transition (angle 0). */
static void corner(rd_classic *s, int n, int gap, float angle, float speed_factor) {
  for (int i = 0; i < n; i++) frame(s, angle, speed_factor);
  for (int i = 0; i < gap; i++) frame(s, 0.0f, speed_factor);
}

/* Mathf.RoundToInt rounds halves to even, like the game; plain rounding would drift the
 * score by a point here and there. */
static void test_round(void) {
  CHECK(rd_classic_round(0.5f) == 0 && rd_classic_round(1.5f) == 2 && rd_classic_round(2.5f) == 2);
  CHECK(rd_classic_round(2.4f) == 2 && rd_classic_round(2.6f) == 3 && rd_classic_round(-1.5f) == -2);
}

/* Corner points are collected without the combo and banked as partial x combo on the first
 * frame without drift points -- the core of the 4.9 system. */
static void test_bank_at_transition(void) {
  rd_classic s = fresh();
  CHECK(frame(&s, 40.0f, 1.5f) == 6);           /* 4.0 x 1.5 x tf 1.0 */
  CHECK(s.partial == 6 && s.total == 0 && s.drift_combo);
  s.combo = 3;
  frame(&s, 0.0f, 1.5f);
  CHECK(s.total == 18 && s.partial == 0 && s.time_factor == 1.0f);
  CHECK(s.combo == 3 && s.drift_combo);         /* the combo outlives a short transition */
}

/* driftTimeFactor grows 1.0 per second of drift and restarts on every transition. */
static void test_time_factor(void) {
  rd_classic s = fresh();
  for (int i = 0; i < 50; i++) frame(&s, 40.0f, 1.5f);
  CHECK(s.time_factor > 1.99f && s.time_factor < 2.01f);
  frame(&s, 0.0f, 1.5f);
  CHECK(s.time_factor == 1.0f);
}

/* Below 10 m/s (speed factor < 1) the time factor does not count. */
static void test_slow_ignores_time_factor(void) {
  rd_classic s = fresh();
  s.time_factor = 3.0f;
  CHECK(frame(&s, 50.0f, 0.8f) == 4);           /* 5.0 x 0.8, no x3 */
  CHECK(frame(&s, 50.0f, 1.0f) == 15);          /* 5.0 x 1.0 x 3.02 */
}

/* 4.9 counts 5..140 degrees; outside that the frame is a transition and banks the corner. */
static void test_angle_window(void) {
  rd_classic s = fresh();
  frame(&s, 40.0f, 1.5f);
  CHECK(frame(&s, 150.0f, 1.5f) == 0 && s.total == 6 && s.partial == 0);
  CHECK(frame(&s, 4.9f, 1.5f) == 0);
  CHECK(frame(&s, 140.0f, 1.5f) == 21);         /* 14 x 1.5 */
}

/* The proximity factor multiplies the frame's points. */
static void test_proximity(void) {
  rd_classic s = fresh();
  const rd_classic_in in = { .active = 1, .angle = 40.0f, .speed_factor = 1.5f, .proximity = 20,
                             .dt = DT };
  CHECK(rd_classic_step(&s, &in) == 120);
}

/* A corner over 1000 points raises the combo by one; a big corner keeps raising it at
 * 1000 x 2^(combo-1); the 1000-point rule fires once per corner. */
static void test_combo_growth(void) {
  rd_classic s = fresh();
  s.partial = 1000;
  frame(&s, 10.0f, 1.0f);                       /* 1001 */
  CHECK(s.combo == 2 && s.combo_updated && s.best_combo == 2);
  frame(&s, 10.0f, 1.0f);
  CHECK(s.combo == 2);                          /* once per corner, 2000 not reached */
  s.partial = 2000;
  frame(&s, 10.0f, 1.0f);
  CHECK(s.combo == 3);                          /* > 1000 x 2^1 */
  s.partial = 3000;
  frame(&s, 10.0f, 1.0f);
  CHECK(s.combo == 3);                          /* needs > 4000 now */
  frame(&s, 0.0f, 1.0f);                        /* transition: the next corner may use the */
  CHECK(!s.combo_updated && s.combo == 3);      /* 1000-point rule again */
  s.partial = 1000;
  frame(&s, 10.0f, 1.0f);
  CHECK(s.combo == 4 && s.best_combo == 4);
}

/* More than 1 s without drift points ends the combo; a short transition does not. */
static void test_combo_timeout(void) {
  rd_classic s = fresh();
  s.combo = 5;
  corner(&s, 10, 0, 40.0f, 1.5f);
  const int32_t partial = s.partial;
  corner(&s, 0, 45, 40.0f, 1.5f);               /* 0.9 s: still on */
  CHECK(s.combo == 5 && s.drift_combo);
  for (int i = 0; i < 6; i++) frame(&s, 0.0f, 1.5f);   /* past 1.0 s */
  CHECK(s.combo == 1 && !s.drift_combo && s.timer == 0.0f);
  CHECK(s.total == partial * 5);                /* banked with the combo it had */
}

/* Wrong way, off track or points disabled: the corner and the combo are dropped, the banked
 * total stays. */
static void test_inactive(void) {
  rd_classic s = fresh();
  s.total = 500; s.partial = 300; s.combo = 4; s.time_factor = 2.5f;
  const rd_classic_in in = { .active = 0, .angle = 40.0f, .speed_factor = 1.5f, .proximity = 1,
                             .dt = DT };
  CHECK(rd_classic_step(&s, &in) == 0);
  CHECK(s.total == 500 && s.partial == 0 && s.combo == 1 && s.time_factor == 1.0f);
}

/* At the finish the running corner is banked with its combo, so the race result (read later
 * from totalPoints) includes it. */
static void test_race_end_banks(void) {
  rd_classic s = fresh();
  s.partial = 1500; s.combo = 3;
  const rd_classic_in in = { .active = 1, .race_ended = 1, .angle = 40.0f, .speed_factor = 1.5f,
                             .proximity = 1, .dt = DT };
  rd_classic_step(&s, &in);
  CHECK(s.total == (1500 + 6) * 3 && s.partial == 0);
}

/* The banked total saturates instead of wrapping. */
static void test_bank_saturates(void) {
  rd_classic s = fresh();
  s.total = INT32_MAX - 10; s.partial = 1000; s.combo = 50;
  frame(&s, 0.0f, 1.0f);
  CHECK(s.total == INT32_MAX);
}

/* Matches the idealised model of analysis/research/19-scoring_model.py (40 degrees, 15 m/s,
 * 3 s corners, 0.1 s transitions): 10 corners = 168 075 points at x12. */
static void test_matches_research_model(void) {
  rd_classic s = fresh();
  for (int i = 0; i < 10; i++) corner(&s, 150, 5, 40.0f, 1.5f);
  CHECK(s.total == 168075 && s.combo == 12);
}

/* ObscuredInt round trip under the field's key; a keyless field gets ACTk's default key. */
static void test_obscured(void) {
  rd_obscured_int o = { .key = 123456, .hidden = 0 };
  rd_obscured_set(&o, 987654);
  CHECK(o.key == 123456 && o.hidden == (987654 ^ 123456) && rd_obscured_get(o) == 987654);
  rd_obscured_int z = { 0, 0 };
  rd_obscured_set(&z, -5);
  CHECK(z.key == RD_OBSCURED_KEY && rd_obscured_get(z) == -5);
}

int main(void) {
  RUN(test_round);
  RUN(test_bank_at_transition);
  RUN(test_time_factor);
  RUN(test_slow_ignores_time_factor);
  RUN(test_angle_window);
  RUN(test_proximity);
  RUN(test_combo_growth);
  RUN(test_combo_timeout);
  RUN(test_inactive);
  RUN(test_race_end_banks);
  RUN(test_bank_saturates);
  RUN(test_matches_research_model);
  RUN(test_obscured);
  return g_failures != 0;
}
