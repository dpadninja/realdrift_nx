/* rd_racemusic.c -- see rd_racemusic.h: the set of preloaded track names. MIT (see LICENSE). */
#include <string.h>
#include "rd_racemusic.h"

int rd_music_cache_add(rd_music_cache *c, const char *name) {
  const size_t len = name ? strlen(name) : 0;
  if (!len || len >= RD_MUSIC_NAME_MAX) return -1;
  for (int i = 0; i < c->n; i++)
    if (!strcmp(c->names[i], name)) return 0;
  if (c->n >= RD_MUSIC_MAX) return -1;
  memcpy(c->names[c->n++], name, len + 1);
  return 1;
}
