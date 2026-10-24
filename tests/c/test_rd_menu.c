/* sources: source/game/rd_menu.c source/engine/rd_uinav.c */
/* Host unit test of rd_menu (menu navigation rules). MIT (see LICENSE). */
#include <string.h>
#include "check.h"
#include "rd_menu.h"

static rd_ugui_target target(uintptr_t id, const char *name, const char *parent, float x, float y) {
  rd_ugui_target t = { .t = { id, { x, y, 100, 40 } } };
  strncpy(t.name, name, sizeof t.name - 1);
  strncpy(t.parent, parent, sizeof t.parent - 1);
  return t;
}

/* Back-like names are found in any case and position, but "Background" (every panel has one)
 * is not a back button. */
static void test_back_names(void) {
  CHECK(rd_menu_is_back_name("BackButton") && rd_menu_is_back_name("btn_back"));
  CHECK(rd_menu_is_back_name("CloseX") && rd_menu_is_back_name("ReturnToMenu"));
  CHECK(rd_menu_is_back_name("CancelButton"));
  CHECK(!rd_menu_is_back_name("Background") && !rd_menu_is_back_name("backdrop"));
  CHECK(rd_menu_is_back_name("Background_Back"));          /* a real "back" after it */
  CHECK(!rd_menu_is_back_name("PlayButton") && !rd_menu_is_back_name("") && !rd_menu_is_back_name(NULL));
}

/* B takes the rule's button first, else the topmost-then-leftmost back-like element (the
 * usual corner back arrow), also when only its parent carries the name; -1 means Escape. */
static void test_back_target(void) {
  const rd_ugui_target t[] = {
    target(1, "Play", "Main", 500, 300),
    target(2, "BackButton", "Garage", 900, 20),
    target(3, "Image", "CloseButton", 20, 20),            /* named by its parent, top-left */
    target(4, "Exit", "Main", 20, 600),
  };
  CHECK(rd_menu_back_target(t, 4, NULL) == 2);
  const rd_menu_rule rule = { "Main", "Exit", NULL, NULL, NULL };
  CHECK(rd_menu_back_target(t, 4, &rule) == 3);
  const rd_menu_rule stale = { "Main", "Gone", NULL, NULL, NULL };     /* a rule naming a missing element */
  CHECK(rd_menu_back_target(t, 4, &stale) == 2);
  CHECK(rd_menu_back_target(t, 1, NULL) == -1);
}

/* A rule applies when an element of its screen (or such an element's parent) is shown;
 * rules without a screen never match. */
static void test_rule_for(void) {
  const rd_ugui_target t[] = { target(1, "Buy", "ShopPanel", 0, 0), target(2, "Play", "Main", 0, 50) };
  const rd_menu_rule rules[] = { { "TrackSelection", NULL, NULL, NULL, NULL }, { "ShopPanel", "Close", "Buy", NULL, NULL },
                                 { NULL, NULL, NULL, NULL, NULL } };
  CHECK(rd_menu_rule_for(rules, 3, t, 2) == &rules[1]);
  CHECK(rd_menu_rule_for(rules, 1, t, 2) == NULL);
  CHECK(rd_menu_find(t, 2, "Play") == 1 && rd_menu_find(t, 2, "Nope") == -1 && rd_menu_find(t, 2, NULL) == -1);
}

/* The navigator stays out of the way while a car is driven, but takes over on the pause menu
 * and after the finish; off when disabled and while the loading screen is up (the menu's
 * buttons still exist behind it, so the frame would sit on top of it). */
static void test_wanted(void) {
  CHECK(rd_menu_wanted(1, 0, 0, 0, 0));
  CHECK(!rd_menu_wanted(1, 0, 1, 0, 0));
  CHECK(rd_menu_wanted(1, 0, 1, 1, 0) && rd_menu_wanted(1, 0, 1, 0, 1));
  CHECK(!rd_menu_wanted(0, 0, 0, 0, 0));
  CHECK(!rd_menu_wanted(1, 1, 0, 0, 0) && !rd_menu_wanted(1, 1, 1, 1, 0));
}

/* The screen fingerprint ignores order and changes with the set of elements, so a screen is
 * logged once, not every scan. */
static void test_signature(void) {
  const rd_ugui_target a[] = { target(1, "A", "", 0, 0), target(2, "B", "", 0, 0) };
  const rd_ugui_target b[] = { target(2, "B", "", 0, 0), target(1, "A", "", 0, 0) };
  const rd_ugui_target c[] = { target(1, "A", "", 0, 0), target(3, "C", "", 0, 0) };
  CHECK(rd_menu_signature(a, 2) == rd_menu_signature(b, 2));
  CHECK(rd_menu_signature(a, 2) != rd_menu_signature(c, 2));
  CHECK(rd_menu_signature(a, 1) != rd_menu_signature(a, 2));
}

/* Editing a slider: integer sliders step by 1, others by a twentieth of the range, the value
 * stays in range, A keeps it and B restores the value from before editing. */
static void test_slider_edit(void) {
  rd_menu_edit e;
  rd_menu_edit_begin(&e, 0.5f, 0.0f, 1.0f, 0);
  CHECK(e.on && e.step == 0.05f);
  CHECK(rd_menu_edit_move(&e, 1) == 0.55f);
  for (int i = 0; i < 30; i++) rd_menu_edit_move(&e, 1);
  CHECK(e.value == 1.0f);                                   /* clamped */
  CHECK(rd_menu_edit_end(&e, 1) == 0.5f && !e.on);          /* B: back to the old value */
  rd_menu_edit_begin(&e, 3.0f, 1.0f, 10.0f, 1);
  CHECK(e.step == 1.0f && rd_menu_edit_move(&e, -1) == 2.0f && rd_menu_edit_move(&e, -5) == 1.0f);
  CHECK(rd_menu_edit_end(&e, 0) == 1.0f);                   /* A: keep the new value */
  rd_menu_edit_begin(&e, 0.0f, 5.0f, -5.0f, 1);             /* reversed range is normalised */
  CHECK(e.min == -5.0f && e.max == 5.0f);
}

/* The direction along the slider changes it, the other axis does nothing; a reversed or
 * vertical slider maps its own way. */
static void test_slider_delta(void) {
  CHECK(rd_menu_edit_delta(RD_UINAV_RIGHT, 0) == 1 && rd_menu_edit_delta(RD_UINAV_LEFT, 0) == -1);
  CHECK(rd_menu_edit_delta(RD_UINAV_UP, 0) == 0 && rd_menu_edit_delta(RD_UINAV_DOWN, 0) == 0);
  CHECK(rd_menu_edit_delta(RD_UINAV_RIGHT, 1) == -1);
  CHECK(rd_menu_edit_delta(RD_UINAV_UP, 2) == 1 && rd_menu_edit_delta(RD_UINAV_UP, 3) == -1);
  CHECK(rd_menu_edit_delta(RD_UINAV_DOWN, 3) == 1 && rd_menu_edit_delta(RD_UINAV_LEFT, 2) == 0);
  CHECK(rd_menu_edit_delta(RD_UINAV_RIGHT, 7) == 0);
}

/* The pause menu's rule: B resumes instead of tapping "Back to main menu" (whose name looks
 * like a back button and opens the quit dialog). */
static void test_pause_rule(void) {
  const rd_ugui_target t[] = {
    target(1, "ButtonResume", "Background", 309, 208),
    target(2, "ButtonBackToMainMenu", "Background", 309, 478),
  };
  const rd_menu_rule rules[] = { { "ButtonResume", "ButtonResume", "ButtonResume", NULL, NULL } };
  CHECK(rd_menu_back_target(t, 2, NULL) == 1);              /* the heuristic alone: wrong */
  CHECK(rd_menu_back_target(t, 2, rd_menu_rule_for(rules, 1, t, 2)) == 0);
}

/* L / R: the car arrows of the garage and the leaderboard are found by name, a rule wins over
 * the names (tabs), and "Parent/Name" tells same-named elements apart. */
static void test_prev_next(void) {
  CHECK(rd_menu_is_prev_name("ButtonArrowLeft") && rd_menu_is_prev_name("PrevCar"));
  CHECK(rd_menu_is_next_name("ButtonArrowRight") && rd_menu_is_next_name("btnNext"));
  CHECK(!rd_menu_is_prev_name("ButtonArrowRight") && !rd_menu_is_next_name("ButtonArrowLeft"));
  CHECK(!rd_menu_is_prev_name(NULL) && !rd_menu_is_next_name("Play"));
  const rd_ugui_target garage[] = {
    target(1, "ButtonTuning", "MenuSelectionPanel", 10, 631),
    target(2, "ButtonArrowLeft", "ButtonsArrow", 0, 328),
    target(3, "ButtonArrowRight", "ButtonsArrow", 1172, 328),
  };
  CHECK(rd_menu_step_target(garage, 3, NULL, -1) == 1 && rd_menu_step_target(garage, 3, NULL, 1) == 2);
  CHECK(rd_menu_step_target(garage, 1, NULL, 1) == -1);
  const rd_ugui_target options[] = {
    target(1, "ToggleBasic", "HorizontalGroup", 346, 14),
    target(2, "ToggleAdvanced", "HorizontalGroup", 797, 14),
  };
  const rd_menu_rule tabs = { "ToggleBasic", NULL, NULL, "ToggleBasic", "ToggleAdvanced" };
  CHECK(rd_menu_step_target(options, 2, &tabs, -1) == 0 && rd_menu_step_target(options, 2, &tabs, 1) == 1);
  const rd_ugui_target two[] = {
    target(1, "ButtonArrowLeft", "TopBar", 202, 8),
    target(2, "ButtonArrowLeft", "ButtonsArrow", 0, 328),
  };
  CHECK(rd_menu_find(two, 2, "ButtonsArrow/ButtonArrowLeft") == 1);
  CHECK(rd_menu_find(two, 2, "TopBar/ButtonArrowLeft") == 0);
  CHECK(rd_menu_find(two, 2, "Top/ButtonArrowLeft") == -1);
}

#define LIST 0x50                       /* the ScrollRect the cells are in */

/* A card of the track carousel (as logged on the console: 394x506, 422 px apart). */
static rd_ugui_target card(uintptr_t id, float x) {
  rd_ugui_target t = target(id, "TrackCellView(Clone)", "Container", x, 174);
  t.t.r.w = 394;
  t.t.r.h = 506;
  t.scroll = LIST;
  return t;
}

/* The flat rd_uinav targets of t[0..n). */
static void flatten(const rd_ugui_target *t, int n, rd_uinav_target *flat) {
  for (int i = 0; i < n; i++) flat[i] = t[i].t;
}

/* A horizontal list over the whole screen width, content 4248 px wide, at position x. */
static rd_ugui_scroll carousel(float x) {
  return (rd_ugui_scroll){ .horizontal = 1, .x = x, .y = 1,
                           .view = { 0, 174, 1280, 506 }, .content = { 0, 174, 4248, 506 } };
}

/* Track selection: right from the last card on screen scrolls the carousel one card (422 px
 * of a 2968 px range) and moves again afterwards; between cards on screen it only moves; at
 * the end of the list nothing scrolls and the plain move applies. Up leaves the list
 * (for the back button), and without a scroll state the move is rd_uinav_step's. */
static void test_carousel(void) {
  rd_ugui_target t[] = { target(1, "ButtonBack", "TrackSelectionPanel", 0, 0),
                         card(2, 28), card(3, 450), card(4, 872) };
  t[0].t.r.w = t[0].t.r.h = 68;
  rd_uinav_target flat[4];
  flatten(t, 4, flat);
  const rd_ugui_scroll start = carousel(0.0f), end = carousel(1.0f), mid = carousel(0.5f);
  rd_menu_move mv = rd_menu_plan_move(t, flat, 4, 3, RD_UINAV_RIGHT, &start);
  CHECK(mv.focus == 3 && mv.scroll && !mv.vertical && mv.again);
  CHECK(mv.to > 422.0f / 2968.0f - 1e-4f && mv.to < 422.0f / 2968.0f + 1e-4f);
  mv = rd_menu_plan_move(t, flat, 4, 1, RD_UINAV_RIGHT, &start);
  CHECK(mv.focus == 2 && !mv.scroll);
  mv = rd_menu_plan_move(t, flat, 4, 3, RD_UINAV_RIGHT, &end);
  CHECK(mv.focus == -1 && !mv.scroll);
  mv = rd_menu_plan_move(t, flat, 4, 1, RD_UINAV_LEFT, &start);   /* the first card, at the start: */
  CHECK(mv.focus == 0 && !mv.scroll);                              /* out to the back button */
  mv = rd_menu_plan_move(t, flat, 4, 1, RD_UINAV_LEFT, &mid);
  CHECK(mv.focus == 1 && mv.scroll && mv.again && mv.to < 0.5f);
  mv = rd_menu_plan_move(t, flat, 4, 2, RD_UINAV_UP, &mid);       /* the list does not scroll up */
  CHECK(mv.focus == 0 && !mv.scroll);
  mv = rd_menu_plan_move(t, flat, 4, 3, RD_UINAV_RIGHT, NULL);
  CHECK(mv.focus == -1 && !mv.scroll);
  CHECK(rd_menu_plan_move(t, flat, 4, 0, RD_UINAV_RIGHT, &start).scroll == 0);   /* not in a list */
}

/* A card that sticks out of the viewport gets the focus and the list scrolls by exactly the
 * part outside (ensure visible), without a second move. */
static void test_carousel_partial(void) {
  rd_ugui_target t[] = { card(1, 450), card(2, 1100) };
  rd_uinav_target flat[2];
  flatten(t, 2, flat);
  const rd_ugui_scroll s = carousel(0.25f);
  const rd_menu_move mv = rd_menu_plan_move(t, flat, 2, 0, RD_UINAV_RIGHT, &s);
  const float want = 0.25f + (1100.0f + 394.0f - 1280.0f) / 2968.0f;
  CHECK(mv.focus == 1 && mv.scroll && !mv.again && mv.to > want - 1e-4f && mv.to < want + 1e-4f);
}

/* A vertical list (y = 1 at the top): down from the last row shown lowers y by one row,
 * clamped at 0; up at the top does not scroll. */
static void test_vertical_list(void) {
  rd_ugui_target t[3];
  for (int i = 0; i < 3; i++) {
    t[i] = target((uintptr_t)(i + 1), "Row", "Content", 0, 100.0f + 100.0f * (float)i);
    t[i].t.r.h = 90;
    t[i].scroll = LIST;
  }
  rd_uinav_target flat[3];
  flatten(t, 3, flat);
  rd_ugui_scroll s = { .vertical = 1, .x = 0, .y = 1, .view = { 0, 100, 100, 300 },
                       .content = { 0, 100, 100, 1300 } };
  rd_menu_move mv = rd_menu_plan_move(t, flat, 3, 2, RD_UINAV_DOWN, &s);
  CHECK(mv.focus == 2 && mv.scroll && mv.vertical && mv.again);
  CHECK(mv.to > 0.9f - 1e-4f && mv.to < 0.9f + 1e-4f);         /* 100 px of a 1000 px range */
  CHECK(!rd_menu_plan_move(t, flat, 3, 0, RD_UINAV_UP, &s).scroll);
  s.y = 0.05f;
  mv = rd_menu_plan_move(t, flat, 3, 2, RD_UINAV_DOWN, &s);
  CHECK(mv.scroll && mv.to == 0.0f);
  s.content.h = 300;                                           /* nothing to scroll */
  CHECK(!rd_menu_plan_move(t, flat, 3, 2, RD_UINAV_DOWN, &s).scroll);
}

/* A scene load is over when another scene is active, or after the time limit (a load that
 * leaves the active scene as it was must not hide the frame for good). */
static void test_load_over(void) {
  CHECK(!rd_menu_load_over(5, 5, 1));
  CHECK(rd_menu_load_over(5, 9, 1));
  CHECK(rd_menu_load_over(5, 5, RD_MENU_LOAD_MAX_FRAMES));
}

int main(void) {
  RUN(test_back_names);
  RUN(test_back_target);
  RUN(test_rule_for);
  RUN(test_wanted);
  RUN(test_signature);
  RUN(test_slider_edit);
  RUN(test_slider_delta);
  RUN(test_pause_rule);
  RUN(test_prev_next);
  RUN(test_carousel);
  RUN(test_carousel_partial);
  RUN(test_vertical_list);
  RUN(test_load_over);
  return g_failures != 0;
}
