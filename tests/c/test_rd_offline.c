/* sources: source/game/rd_offline.c */
/* Host unit test of rd_offline. MIT (see LICENSE). */
#include <string.h>
#include "check.h"
#include "rd_offline.h"

/* The table fits the glue's per-target array (a longer one would silently lose its tail) and
 * every row names a root and a path below it: Transform.Find with an empty path returns the
 * root itself, which would hide the whole menu or race UI. */
static void test_table_well_formed(void) {
  int n = 0;
  const rd_offline_target *t = rd_offline_targets(&n);
  CHECK(n > 0 && n <= RD_OFFLINE_MAX_TARGETS);
  for (int i = 0; i < n; i++) {
    CHECK(t[i].root == RD_OFFLINE_MENU || t[i].root == RD_OFFLINE_RACE);
    CHECK(t[i].path && t[i].path[0] && t[i].path[0] != '/');
    CHECK(t[i].path[strlen(t[i].path) - 1] != '/');
  }
}

/* Counts the rows whose path ends with `tail` under root r. */
static int count(rd_offline_root r, const char *tail) {
  int n = 0, hits = 0;
  const rd_offline_target *t = rd_offline_targets(&n);
  for (int i = 0; i < n; i++) {
    const size_t a = strlen(t[i].path), b = strlen(tail);
    if (t[i].root == r && a >= b && !strcmp(t[i].path + a - b, tail)) hits++;
  }
  return hits;
}

/* What the user asked to remove is in the table: the coin shop's "+", Google sign-in, the cloud
 * tools, the online leaderboard tabs, Share after a race and in photo mode, all nine music
 * tracks' "buy" links, and the profile's money-box picture (a link to the shop). */
static void test_covers_online_features(void) {
  CHECK(count(RD_OFFLINE_MENU, "MainPanel/ButtonIAP") == 1);
  CHECK(count(RD_OFFLINE_MENU, "GoogleSignInSignOutButton") == 1);
  CHECK(count(RD_OFFLINE_MENU, "TabProfile/BottomWindow") == 1);
  CHECK(count(RD_OFFLINE_MENU, "Leaderboard_Toggles") == 1);
  CHECK(count(RD_OFFLINE_MENU, "LeaderboardsPanel/Window/BottomWindow") == 1);   /* modes */
  CHECK(count(RD_OFFLINE_MENU, "ButtonPrivacy") == 1);
  CHECK(count(RD_OFFLINE_MENU, "/Buy") == 9);
  CHECK(count(RD_OFFLINE_RACE, "ButtonShareResult") == 1);
  CHECK(count(RD_OFFLINE_RACE, "ShareTexture") == 1);
  CHECK(count(RD_OFFLINE_MENU, "TopWindow/Frame/Bottom/RightWindow") == 1);   /* money box */
}

/* The player button moves right by exactly the hidden "+" button's width, so it ends at the
 * screen edge where "+" was; no other row moves (dx is only for RD_OFFLINE_MOVE_X). */
static void test_move(void) {
  int n = 0, moved = 0;
  const rd_offline_target *t = rd_offline_targets(&n);
  for (int i = 0; i < n; i++) {
    if (t[i].action != RD_OFFLINE_MOVE_X) { CHECK(t[i].dx == 0.0f); continue; }
    moved++;
    CHECK(t[i].dx == RD_OFFLINE_IAP_WIDTH);
    CHECK(!strcmp(t[i].path, "Canvas_MainMenu/MainPanel/ButtonPlayer"));
  }
  CHECK(moved == 1);
}

/* The name field is locked only while the name comes from the Switch profile: with
 * switch_name = 0 the player types the name in the game, as before. Everything else applies
 * either way. */
static void test_wants(void) {
  int n = 0, locked = 0;
  const rd_offline_target *t = rd_offline_targets(&n);
  for (int i = 0; i < n; i++) {
    CHECK(rd_offline_wants(&t[i], 1));
    if (t[i].action == RD_OFFLINE_NO_INPUT) {
      locked++;
      CHECK(!rd_offline_wants(&t[i], 0));
    } else {
      CHECK(rd_offline_wants(&t[i], 0));
    }
  }
  CHECK(locked == 1);
}

/* Outside a scene change the targets are checked every RD_OFFLINE_FRAMES frames and a
 * missing root (MenuManager in a race) is looked for every RD_OFFLINE_FIND_FRAMES: a
 * FindObjectsOfType per frame for the whole race would only make garbage. */
static void test_cadence_settled(void) {
  const uint32_t scene = 100, now = scene + RD_OFFLINE_FRESH_FRAMES + RD_OFFLINE_FRAMES * 10;
  int checks = 0;
  for (uint32_t f = now; f < now + RD_OFFLINE_FRAMES * 4; f++)
    checks += rd_offline_check_due(f, scene);
  CHECK(checks == 4);
  CHECK(!rd_offline_find_due(now, scene, now - 1));
  CHECK(!rd_offline_find_due(now, scene, now - RD_OFFLINE_FIND_FRAMES + 1));
  CHECK(rd_offline_find_due(now, scene, now - RD_OFFLINE_FIND_FRAMES));
}

/* Right after a scene change everything happens every frame, so the menu loaded after a race
 * never shows "+" long enough to be focused (the log showed it ~8 frames after the load); the
 * first lookup ever (last = 0) is always due. */
static void test_cadence_fresh(void) {
  const uint32_t scene = 1000;
  for (uint32_t f = scene; f < scene + RD_OFFLINE_FRESH_FRAMES; f++) {
    CHECK(rd_offline_check_due(f, scene));
    CHECK(rd_offline_find_due(f, scene, f - 1));
  }
  CHECK(!rd_offline_find_due(scene + RD_OFFLINE_FRESH_FRAMES, scene,
                             scene + RD_OFFLINE_FRESH_FRAMES - 1));
  CHECK(rd_offline_find_due(5000, 0, 0));
}

int main(void) {
  RUN(test_table_well_formed);
  RUN(test_covers_online_features);
  RUN(test_wants);
  RUN(test_move);
  RUN(test_cadence_settled);
  RUN(test_cadence_fresh);
  return g_failures != 0;
}
