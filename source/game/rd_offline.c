/* rd_offline.c -- see rd_offline.h. MIT (see LICENSE). */
#include "rd_offline.h"

/* Paths from the scene dump of level0 (menu) and the race scenes' RaceManager
 * (analysis/research/32-*.py). */
#define PROFILE "Canvas_MainMenu/PlayerProfilePanel/Window/Background/TabProfile/"
#define PLAYER  PROFILE "TopWindow/Frame/Bottom/LeftWindow/BackgroundPlayerName/"
#define HELP    "Canvas_MainMenu/HelpPanel/"
#define MUSIC   HELP "Window/Background/TabMusic/Frame/Scroll View/Viewport/Content/Item "
#define BUY(n)  { RD_OFFLINE_MENU, MUSIC "(" #n ")/Buy", RD_OFFLINE_HIDE, 0, 0 }

static const rd_offline_target TARGETS[] = {
  { RD_OFFLINE_MENU, "Canvas_MainMenu/MainPanel/ButtonIAP",           RD_OFFLINE_HIDE,     0, 0 },
  /* anchored to the top-right corner just left of ButtonIAP: closes the gap it leaves */
  { RD_OFFLINE_MENU, "Canvas_MainMenu/MainPanel/ButtonPlayer",        RD_OFFLINE_MOVE_X,   0,
    RD_OFFLINE_IAP_WIDTH },
  { RD_OFFLINE_MENU, PLAYER "GoogleSignInSignOutButton",              RD_OFFLINE_HIDE,     0, 0 },
  { RD_OFFLINE_MENU, PLAYER "InputField",                             RD_OFFLINE_NO_INPUT, 1, 0 },
  { RD_OFFLINE_MENU, PROFILE "BottomWindow",                          RD_OFFLINE_HIDE,     0, 0 },
  /* only the money-box picture (MoneyVisual -> the shop); CASH is in LeftWindow */
  { RD_OFFLINE_MENU, PROFILE "TopWindow/Frame/Bottom/RightWindow",    RD_OFFLINE_HIDE,     0, 0 },
  { RD_OFFLINE_MENU, HELP "TabsContainer/ToggleGroupTop/HorizontalGroup/ButtonPrivacy",
                                                                      RD_OFFLINE_HIDE,     0, 0 },
  BUY(1), BUY(2), BUY(3), BUY(4), BUY(5), BUY(6), BUY(7), BUY(8), BUY(9),
  { RD_OFFLINE_MENU, "Canvas_MainMenu/LeaderboardsPanel/Window/Leaderboard_Toggles",
                                                                      RD_OFFLINE_HIDE,     0, 0 },
  /* DRIFT RACE / TIME ATTACK: the local scores have no mode, the bar only re-sorted them */
  { RD_OFFLINE_MENU, "Canvas_MainMenu/LeaderboardsPanel/Window/BottomWindow",
                                                                      RD_OFFLINE_HIDE,     0, 0 },
  { RD_OFFLINE_MENU, "Canvas_MainMenu/MainPanel/MenuButtonsPanel/ButtonLeaderboard/TextMeshPro Text",
                                                                      RD_OFFLINE_LABEL,    0, 0 },
  { RD_OFFLINE_RACE, "RaceUI/MainPanel/Center/RaceStatPanel/BottomPanel/ButtonShareResult",
                                                                      RD_OFFLINE_HIDE,     0, 0 },
  { RD_OFFLINE_RACE, "RaceUI/PhotoModePanel/ShareTexture",            RD_OFFLINE_HIDE,     0, 0 },
};

const rd_offline_target *rd_offline_targets(int *n) {
  *n = (int)(sizeof TARGETS / sizeof TARGETS[0]);
  return TARGETS;
}

int rd_offline_wants(const rd_offline_target *t, int switch_name) {
  return !t->needs_switch_name || switch_name;
}

/* 1 while frame `now` is within RD_OFFLINE_FRESH_FRAMES of the last scene change. */
static int fresh(uint32_t now, uint32_t scene) {
  return now - scene < RD_OFFLINE_FRESH_FRAMES;
}

int rd_offline_check_due(uint32_t now, uint32_t scene) {
  return fresh(now, scene) || now % RD_OFFLINE_FRAMES == 0;
}

int rd_offline_find_due(uint32_t now, uint32_t scene, uint32_t last) {
  return !last || fresh(now, scene) || now - last >= RD_OFFLINE_FIND_FRAMES;
}
