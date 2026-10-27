/* rd_player.h -- the player's name and country in the game's saves (config.txt switch_name,
 * country).
 *
 * The game keeps both in PlayerPrefs (prefs.kv): playerName, which it fills with
 * "Drifter_<n>" when empty, and CountryCode, which it asks a web service for by IP. Offline that
 * lookup fails and the game stores Andorra, the first country of its list. At boot, before the
 * engine starts, the port writes the Switch profile's nickname as playerName (switch_name = 1)
 * and, when no country was ever picked, the `country` key (rd_game_symbols.h
 * RD_PREF_*). The flag stays the player's choice in the profile screen.
 * Pure logic here (host-tested, tests/c/test_rd_player.c); the glue in rd_player_hook.c.
 * MIT (see LICENSE). */
#ifndef RD_PLAYER_H
#define RD_PLAYER_H

#include <stddef.h>

/* The player name made from a profile nickname: surrounding spaces removed, cut before the
 * first byte that is not valid UTF-8 or a control character (so nothing half-encoded reaches
 * the game's text). Writes it to out[0..n) and returns its length; 0 = no usable name. */
size_t rd_player_name(const char *nickname, char *out, size_t n);

/* 1 if the stored CountryCode is no choice of the player: missing (NULL), empty, or the
 * failed lookup's Andorra. */
int rd_player_country_unset(const char *stored);

/* ---- glue (rd_player_hook.c) --------------------------------------------------------- */
/* Writes playerName / CountryCode into the loaded prefs. Call once at boot, after
 * unity_environment_init (prefs.kv loaded) and before the engine runs. Main thread. */
void rd_player_boot(void);

#endif
