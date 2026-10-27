/* rd_player.c -- see rd_player.h. MIT (see LICENSE). */
#include <string.h>
#include "rd_player.h"
#include "rd_game_symbols.h"

/* Length of the valid UTF-8 sequence starting at s (at most `left` bytes), or 0 if it is not
 * one: a stray continuation byte, a truncated or overlong sequence, a surrogate, above
 * U+10FFFF, or a control character. */
static size_t utf8_char(const unsigned char *s, size_t left) {
  const unsigned c = s[0];
  if (c < 0x20 || c == 0x7f) return 0;
  if (c < 0x80) return 1;
  size_t len;
  unsigned cp;
  if (c >= 0xc2 && c <= 0xdf)      { len = 2; cp = c & 0x1f; }
  else if (c >= 0xe0 && c <= 0xef) { len = 3; cp = c & 0x0f; }
  else if (c >= 0xf0 && c <= 0xf4) { len = 4; cp = c & 0x07; }
  else return 0;
  if (len > left) return 0;
  for (size_t i = 1; i < len; i++) {
    if ((s[i] & 0xc0) != 0x80) return 0;
    cp = (cp << 6) | (s[i] & 0x3f);
  }
  if ((len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) || cp > 0x10ffff ||
      (cp >= 0xd800 && cp <= 0xdfff) || (cp >= 0x80 && cp < 0xa0))
    return 0;
  return len;
}

size_t rd_player_name(const char *nickname, char *out, size_t n) {
  if (!out || !n) return 0;
  out[0] = '\0';
  if (!nickname) return 0;
  const unsigned char *s = (const unsigned char *)nickname;
  while (*s == ' ') s++;
  size_t len = 0, end = 0;               /* end: length without trailing spaces */
  while (s[len]) {
    const size_t k = utf8_char(s + len, strlen((const char *)s + len));
    if (!k || len + k >= n) break;
    len += k;
    if (s[len - 1] != ' ') end = len;
  }
  memcpy(out, s, end);
  out[end] = '\0';
  return end;
}

int rd_player_country_unset(const char *stored) {
  return !stored || !stored[0] || !strcmp(stored, RD_COUNTRY_FAILED_LOOKUP);
}
