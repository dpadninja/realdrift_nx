/* rd_player.h -- the player's flag in the game's saves (config.txt country).
 *
 * The game keeps CountryCode in PlayerPrefs (prefs.kv) and, without it, asks a web service for
 * it by IP. Offline that lookup fails and the game stores Andorra, the first country of its
 * list. At boot, before the engine starts, the port writes the `country` key there when no
 * country was ever picked (rd_game_symbols.h RD_PREF_COUNTRY). The flag stays the player's
 * choice in the profile screen. The name is not touched: the player types it in the profile
 * (MenuManager.OnPlayerNameSubmit), and the game would replace one written into playerName
 * with the name in its CloudOnce save (PersistentData.LoadCloudDataToLocal,
 * analysis/research/32-offline-ui.md).
 * Pure logic here (host-tested, tests/c/test_rd_player.c); the glue in rd_player_hook.c.
 * MIT (see LICENSE). */
#ifndef RD_PLAYER_H
#define RD_PLAYER_H

/* 1 if the stored CountryCode is no choice of the player: missing (NULL), empty, or the
 * failed lookup's Andorra. */
int rd_player_country_unset(const char *stored);

/* ---- glue (rd_player_hook.c) --------------------------------------------------------- */
/* Writes CountryCode into the loaded prefs. Call once at boot, after unity_environment_init
 * (prefs.kv loaded) and before the engine runs. Main thread. */
void rd_player_boot(void);

#endif
