/* rd_offline.h -- the game's menus without the online features (config.txt offline_ui).
 *
 * On the Switch there is no Google Play, no App42 back end and no shop, yet the 5.0.8 menus
 * still offer them. The port hides them (analysis/research/32-offline-ui.md):
 *   - main menu: the "+" next to the coins (the coin shop, with Facebook / Remove Ads inside);
 *     the player button (name, flag, coins) moves right into its place;
 *   - player profile: Google sign-in, CLOUD TOOLS (cloud save / load), the right half with the
 *     money-box picture that links to the shop (the coin count is in the left half), and the
 *     name field when the name comes from the Switch profile (switch_name, rd_player.h);
 *   - help: PRIVACY (a web page) and the music tab's "buy this track" links;
 *   - leaderboards: renamed STATISTICS, the DEVICE / LOCAL / WORLDWIDE tabs hidden and only the
 *     on-device board shown (PersistentData.leaderboardType kept at local); the DRIFT RACE /
 *     TIME ATTACK bar hidden too, the board sorted by points (leaderboardRaceMode = drift):
 *     the on-device scores do not record the mode, so the bar only re-sorted the same runs;
 *   - after a race and in photo mode: Share (an Android share sheet).
 * The targets are paths below the scene's root object (MenuManager or RaceManager), found with
 * Transform.Find, so inactive ones are found too. The game turns some of them on again
 * (ManageSignIn after its sign-in check), so they are checked again every RD_OFFLINE_FRAMES.
 * Pure logic here (host-tested, tests/c/test_rd_offline.c); the il2cpp glue in rd_offline_hook.c.
 * MIT (see LICENSE). */
#ifndef RD_OFFLINE_H
#define RD_OFFLINE_H

#define RD_OFFLINE_FRAMES        4    /* the targets are checked this often (frames) */
#define RD_OFFLINE_FIND_FRAMES   30   /* a missing scene root is looked for this often */
#define RD_OFFLINE_MAX_TARGETS   32   /* the table fits the glue's per-target array */
#define RD_OFFLINE_STATISTICS    "STATISTICS"   /* the leaderboard button's new label */
#define RD_OFFLINE_IAP_WIDTH     70.0f /* MainPanel/ButtonIAP's width (canvas units) */

/* The scene root a target lives under. */
typedef enum { RD_OFFLINE_MENU, RD_OFFLINE_RACE, RD_OFFLINE_ROOTS } rd_offline_root;

/* What happens to a target. */
typedef enum {
  RD_OFFLINE_HIDE,         /* GameObject.SetActive(false) */
  RD_OFFLINE_MOVE_X,       /* its RectTransform.anchoredPosition.x moved by dx */
  RD_OFFLINE_NO_INPUT,     /* its TMP_InputField disabled: the text shows, cannot be edited */
  RD_OFFLINE_LABEL,        /* its TMP_Text says RD_OFFLINE_STATISTICS */
} rd_offline_action;

typedef struct {
  rd_offline_root   root;
  const char       *path;    /* below the root object, '/'-separated GameObject names */
  rd_offline_action action;
  int               needs_switch_name;   /* 1: only while the name comes from the Switch */
  float             dx;      /* RD_OFFLINE_MOVE_X: canvas units to the right */
} rd_offline_target;

/* The table of targets; *n gets its length. Static storage. */
const rd_offline_target *rd_offline_targets(int *n);

/* 1 if target t applies with the name taken from the Switch profile (switch_name) or not. */
int rd_offline_wants(const rd_offline_target *t, int switch_name);

/* ---- glue (rd_offline_hook.c) -------------------------------------------------------- */
/* Called from the main loop between engine frames (main thread); with offline_ui = 0 it
 * returns at once. Resolves its names on the first calls that find the game loaded. */
void rd_offline_frame(void);

#endif
