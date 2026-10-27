/* sources: source/game/rd_player.c */
/* Host unit test of rd_player. MIT (see LICENSE). */
#include <string.h>
#include "check.h"
#include "rd_player.h"

/* An ordinary nickname passes unchanged, Cyrillic and Japanese included: profile names are
 * UTF-8 and the game stores the string as it is. */
static void test_name_plain(void) {
  char out[33];
  CHECK(rd_player_name("Drifter", out, sizeof out) == 7 && !strcmp(out, "Drifter"));
  CHECK(rd_player_name("\xd0\x94\xd1\x80\xd0\xb8\xd1\x84\xd1\x82", out, sizeof out) == 10);
  CHECK(!strcmp(out, "\xd0\x94\xd1\x80\xd0\xb8\xd1\x84\xd1\x82"));   /* "Дрифт" */
  CHECK(rd_player_name("\xe3\x83\x89\xe3\x83\xaa", out, sizeof out) == 6);   /* "ドリ" */
}

/* Surrounding spaces go (they would look like a blank name in the leaderboard), inner ones
 * stay; a name of spaces only is no name, so the game keeps its own. */
static void test_name_spaces(void) {
  char out[33];
  CHECK(rd_player_name("  Ken Block  ", out, sizeof out) == 9 && !strcmp(out, "Ken Block"));
  CHECK(rd_player_name("   ", out, sizeof out) == 0 && out[0] == '\0');
  CHECK(rd_player_name("", out, sizeof out) == 0);
  CHECK(rd_player_name(NULL, out, sizeof out) == 0 && out[0] == '\0');
}

/* A broken byte sequence or a control character ends the name there: the nickname field is a
 * fixed 32-byte buffer that may end inside a character, and half a character would show as
 * garbage in the game's text. */
static void test_name_invalid(void) {
  char out[33];
  CHECK(rd_player_name("Ab\xd0", out, sizeof out) == 2 && !strcmp(out, "Ab"));     /* truncated */
  CHECK(rd_player_name("Ab\x80" "cd", out, sizeof out) == 2);                      /* stray */
  CHECK(rd_player_name("Ab\xc0\xaf", out, sizeof out) == 2);                       /* overlong */
  CHECK(rd_player_name("Ab\xed\xa0\x80", out, sizeof out) == 2);                   /* surrogate */
  CHECK(rd_player_name("Ab\tcd", out, sizeof out) == 2);                           /* control */
  CHECK(rd_player_name("\xc2\x85x", out, sizeof out) == 0);                        /* C1 control */
}

/* The name never overflows `out` and is never cut inside a character. */
static void test_name_fits(void) {
  char out[4];
  CHECK(rd_player_name("abcdef", out, sizeof out) == 3 && !strcmp(out, "abc"));
  CHECK(rd_player_name("a\xd0\x94\xd0\x94", out, sizeof out) == 3);   /* "aД", not half a "Д" */
  CHECK(rd_player_name("abc", out, 0) == 0);
  CHECK(rd_player_name("abc", NULL, 4) == 0);
}

/* The default country replaces only what the player never chose: no key, an empty one, or
 * Andorra, which the game stores when its web lookup fails. A picked flag stays. */
static void test_country_unset(void) {
  CHECK(rd_player_country_unset(NULL));
  CHECK(rd_player_country_unset(""));
  CHECK(rd_player_country_unset("AD"));
  CHECK(!rd_player_country_unset("RU"));
  CHECK(!rd_player_country_unset("JP"));
}

int main(void) {
  RUN(test_name_plain);
  RUN(test_name_spaces);
  RUN(test_name_invalid);
  RUN(test_name_fits);
  RUN(test_country_unset);
  return g_failures != 0;
}
