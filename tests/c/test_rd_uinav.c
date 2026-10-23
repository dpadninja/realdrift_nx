/* sources: source/engine/rd_uinav.c */
/* Host unit test of rd_uinav (focus navigation geometry). MIT (see LICENSE). */
#include "check.h"
#include "rd_uinav.h"

/* A 100x40 button at column c, row r of a grid with 200 px columns and 100 px rows. */
static rd_uinav_target cell(uintptr_t id, int c, int r) {
  return (rd_uinav_target){ id, { 200.0f * (float)c, 100.0f * (float)r, 100.0f, 40.0f } };
}

/* In a 3x2 grid every direction lands on the neighbour, and the edges lead nowhere (no wrap,
 * so the frame never jumps across the screen). */
static void test_grid(void) {
  const rd_uinav_target t[] = { cell(1, 0, 0), cell(2, 1, 0), cell(3, 2, 0),
                                cell(4, 0, 1), cell(5, 1, 1), cell(6, 2, 1) };
  CHECK(rd_uinav_step(t, 6, 1, RD_UINAV_RIGHT) == 2);
  CHECK(rd_uinav_step(t, 6, 1, RD_UINAV_LEFT) == 0);
  CHECK(rd_uinav_step(t, 6, 1, RD_UINAV_DOWN) == 4);
  CHECK(rd_uinav_step(t, 6, 4, RD_UINAV_UP) == 1);
  CHECK(rd_uinav_step(t, 6, 2, RD_UINAV_RIGHT) == -1);
  CHECK(rd_uinav_step(t, 6, 0, RD_UINAV_UP) == -1);
  CHECK(rd_uinav_step(t, 6, 0, RD_UINAV_NONE) == -1 && rd_uinav_step(t, 6, 9, RD_UINAV_UP) == -1);
}

/* A target straight ahead beats a slightly nearer one off to the side: moving down a menu
 * column must not jump to the next column. */
static void test_prefers_straight_ahead(void) {
  const rd_uinav_target t[] = {
    { 1, { 0, 0, 100, 40 } },
    { 2, { 0, 200, 100, 40 } },          /* same column, 200 px down */
    { 3, { 150, 120, 100, 40 } },        /* nearer, but off to the right */
  };
  CHECK(rd_uinav_step(t, 3, 0, RD_UINAV_DOWN) == 1);
}

/* Items that overlap across the direction count as one row, so a wide button above several
 * small ones reaches the one closest along the move. */
static void test_overlap_is_same_line(void) {
  const rd_uinav_target t[] = {
    { 1, { 0, 0, 600, 40 } },            /* wide header button */
    { 2, { 0, 100, 100, 40 } },
    { 3, { 500, 80, 100, 40 } },         /* a bit higher: wins */
  };
  CHECK(rd_uinav_step(t, 3, 0, RD_UINAV_DOWN) == 2);
}

/* The main menu as the console logged it (720p, without the full-screen car-rotation area):
 * moves go where the buttons are, like a TV remote. */
static void test_main_menu_layout(void) {
  enum { PLAYER, IAP, GARAGE, QUICK, CAREER, BOARD, OPTIONS };
  const rd_uinav_target t[] = {
    { 1, { 998, 0, 202, 79 } },   { 2, { 1202, 0, 78, 79 } },  { 3, { 9, 597, 225, 112 } },
    { 4, { 927, 503, 338, 79 } }, { 5, { 927, 597, 338, 112 } }, { 6, { 8, 503, 225, 79 } },
    { 7, { 18, 11, 54, 54 } },
  };
  CHECK(rd_uinav_step(t, 7, BOARD, RD_UINAV_RIGHT) == QUICK);
  CHECK(rd_uinav_step(t, 7, GARAGE, RD_UINAV_RIGHT) == CAREER);
  CHECK(rd_uinav_step(t, 7, OPTIONS, RD_UINAV_DOWN) == BOARD);
  CHECK(rd_uinav_step(t, 7, OPTIONS, RD_UINAV_RIGHT) == PLAYER);
  CHECK(rd_uinav_step(t, 7, QUICK, RD_UINAV_UP) == PLAYER);
  CHECK(rd_uinav_step(t, 7, QUICK, RD_UINAV_DOWN) == CAREER);
  CHECK(rd_uinav_step(t, 7, CAREER, RD_UINAV_LEFT) == GARAGE);
  CHECK(rd_uinav_step(t, 7, PLAYER, RD_UINAV_RIGHT) == IAP);
  CHECK(rd_uinav_step(t, 7, BOARD, RD_UINAV_UP) == OPTIONS);
}

/* The pause menu: a 2x2 grid of buttons over a wide one. */
static void test_pause_layout(void) {
  enum { PHOTO, RESTART, OPTIONS, TO_MENU, RESUME };
  const rd_uinav_target t[] = {
    { 1, { 309, 343, 315, 112 } }, { 2, { 656, 208, 315, 112 } }, { 3, { 656, 343, 315, 112 } },
    { 4, { 309, 478, 662, 90 } },  { 5, { 309, 208, 315, 112 } },
  };
  CHECK(rd_uinav_step(t, 5, RESUME, RD_UINAV_RIGHT) == RESTART);
  CHECK(rd_uinav_step(t, 5, RESUME, RD_UINAV_DOWN) == PHOTO);
  CHECK(rd_uinav_step(t, 5, PHOTO, RD_UINAV_DOWN) == TO_MENU);
  CHECK(rd_uinav_step(t, 5, OPTIONS, RD_UINAV_DOWN) == TO_MENU);
  CHECK(rd_uinav_step(t, 5, TO_MENU, RD_UINAV_UP) == PHOTO);   /* nearest centre of the row */
  CHECK(rd_uinav_step(t, 5, RESTART, RD_UINAV_DOWN) == OPTIONS);
}

/* The settings page: a column of sliders goes down slider by slider, and from the bottom
 * slider down reaches the control-scheme buttons below it. */
static void test_settings_layout(void) {
  enum { SENS, DIFF, GYRO, GYRO_MAN, MUSIC, SOUNDS };
  const rd_uinav_target t[] = {
    { 1, { 34, 128, 578, 22 } }, { 2, { 34, 205, 578, 22 } }, { 3, { 11, 288, 280, 157 } },
    { 4, { 354, 288, 280, 157 } }, { 5, { 668, 263, 578, 22 } }, { 6, { 668, 342, 578, 22 } },
  };
  CHECK(rd_uinav_step(t, 6, SENS, RD_UINAV_DOWN) == DIFF);
  CHECK(rd_uinav_step(t, 6, DIFF, RD_UINAV_UP) == SENS);
  const int below = rd_uinav_step(t, 6, DIFF, RD_UINAV_DOWN);
  CHECK(below == GYRO || below == GYRO_MAN);
  CHECK(rd_uinav_step(t, 6, MUSIC, RD_UINAV_DOWN) == SOUNDS);
  CHECK(rd_uinav_step(t, 6, SOUNDS, RD_UINAV_UP) == MUSIC);
}

/* The garage: from the left car arrow, right goes to the right arrow on the same height, not to
 * the nearer CUSTOM button below (the console found the old weights doing that). */
static void test_garage_layout(void) {
  enum { LEFT, RIGHT, TUNING, CUSTOM };
  const rd_uinav_target t[] = {
    { 1, { 0, 328, 108, 108 } }, { 2, { 1172, 328, 108, 108 } },
    { 3, { 10, 631, 225, 79 } }, { 4, { 261, 631, 225, 79 } },
  };
  CHECK(rd_uinav_step(t, 4, LEFT, RD_UINAV_RIGHT) == RIGHT);
  CHECK(rd_uinav_step(t, 4, RIGHT, RD_UINAV_LEFT) == LEFT);
  CHECK(rd_uinav_step(t, 4, LEFT, RD_UINAV_DOWN) == TUNING);
  CHECK(rd_uinav_step(t, 4, TUNING, RD_UINAV_RIGHT) == CUSTOM);
  CHECK(rd_uinav_step(t, 4, TUNING, RD_UINAV_UP) == LEFT);
}

/* Settings, Basic tab: from Steer Sensitivity, right goes to an element in the same row if
 * there is one, else to the near volume slider -- never to the tab bar above. */
static void test_settings_right(void) {
  enum { SENS, ADVANCED, MUSIC, ROW };
  const rd_uinav_target t[] = {
    { 1, { 34, 128, 578, 22 } }, { 2, { 797, 14, 191, 51 } }, { 3, { 668, 263, 578, 22 } },
    { 4, { 1100, 120, 60, 40 } },        /* e.g. a sound switch on that row */
  };
  CHECK(rd_uinav_step(t, 3, SENS, RD_UINAV_RIGHT) == MUSIC);
  CHECK(rd_uinav_step(t, 4, SENS, RD_UINAV_RIGHT) == ROW);
}

/* With nothing passing the edge rule (an overlapping neighbour), the centre rule still
 * moves, so the focus never gets stuck. */
static void test_fallback_moves(void) {
  const rd_uinav_target t[] = { { 1, { 0, 0, 300, 100 } }, { 2, { 100, 20, 300, 60 } } };
  CHECK(rd_uinav_step(t, 2, 0, RD_UINAV_RIGHT) == 1);
}

/* After the list changes the focus stays on the same element, else moves to the one nearest
 * to where it was, else starts top-left. */
static void test_refocus(void) {
  const rd_uinav_target t[] = { cell(10, 1, 1), cell(20, 0, 0), cell(30, 2, 1) };
  const rd_uinav_rect last = { 390, 90, 100, 40 };
  CHECK(rd_uinav_refocus(t, 3, 30, &last, 1) == 2);       /* still there */
  CHECK(rd_uinav_refocus(t, 3, 99, &last, 1) == 2);       /* gone: nearest to its place */
  CHECK(rd_uinav_refocus(t, 3, 99, &last, 0) == 1);       /* no history: top-left */
  CHECK(rd_uinav_refocus(t, 0, 10, &last, 1) == -1);
}

/* A held direction moves once, waits, then repeats at a steady rate. */
static void test_repeat(void) {
  int moves = 0;
  for (uint32_t f = 1; f <= RD_UINAV_REPEAT_DELAY; f++) moves += rd_uinav_repeat(f);
  CHECK(moves == 1);
  for (uint32_t f = RD_UINAV_REPEAT_DELAY + 1; f <= RD_UINAV_REPEAT_DELAY + 3 * RD_UINAV_REPEAT_EVERY; f++)
    moves += rd_uinav_repeat(f);
  CHECK(moves == 4);
}

/* Only rectangles with an area that overlap the screen are targets. */
static void test_on_screen(void) {
  const rd_uinav_rect in = { 10, 10, 50, 50 }, off = { 1300, 10, 50, 50 }, flat = { 10, 10, 50, 0 },
                      edge = { -40, -40, 50, 50 };
  CHECK(rd_uinav_on_screen(&in, 1280, 720) && rd_uinav_on_screen(&edge, 1280, 720));
  CHECK(!rd_uinav_on_screen(&off, 1280, 720) && !rd_uinav_on_screen(&flat, 1280, 720));
}

/* The screen share tells full-screen touch areas (the car-rotation button covers 89 %) from
 * buttons. */
static void test_screen_share(void) {
  const rd_uinav_rect rotate = { 0, 79, 1280, 641 }, button = { 927, 503, 338, 79 };
  CHECK(rd_uinav_screen_share(&rotate, 1280, 720) > 0.85f);
  CHECK(rd_uinav_screen_share(&button, 1280, 720) < 0.05f);
  CHECK(rd_uinav_screen_share(&button, 0, 720) == 0.0f);
}

/* The tap probes start at the centre and then try the middle of each quarter, all inside the
 * rectangle; an out-of-range index means the centre. */
static void test_probe(void) {
  const rd_uinav_rect r = { 100, 200, 40, 80 };
  float x, y;
  rd_uinav_probe(&r, 0, &x, &y);
  CHECK(x == 120.0f && y == 240.0f);
  rd_uinav_probe(&r, 4, &x, &y);
  CHECK(x == 130.0f && y == 260.0f);
  for (int i = 1; i < RD_UINAV_PROBES; i++) {
    rd_uinav_probe(&r, i, &x, &y);
    CHECK(x > 100.0f && x < 140.0f && y > 200.0f && y < 280.0f && !(x == 120.0f && y == 240.0f));
  }
  rd_uinav_probe(&r, 9, &x, &y);
  CHECK(x == 120.0f && y == 240.0f);
}

int main(void) {
  RUN(test_grid);
  RUN(test_prefers_straight_ahead);
  RUN(test_overlap_is_same_line);
  RUN(test_main_menu_layout);
  RUN(test_pause_layout);
  RUN(test_settings_layout);
  RUN(test_garage_layout);
  RUN(test_settings_right);
  RUN(test_fallback_moves);
  RUN(test_refocus);
  RUN(test_repeat);
  RUN(test_on_screen);
  RUN(test_screen_share);
  RUN(test_probe);
  return g_failures != 0;
}
