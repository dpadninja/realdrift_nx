/* rd_sdcommit.c -- see rd_sdcommit.h. MIT (see LICENSE). */
#include <string.h>
#include "rd_game.h"
#include "rd_sdcommit.h"

#define IL2CPP_PREFIX RD_GAME_HOME_DIR "/" RD_IL2CPP_DATA_DIR "/"

int rd_sdcommit_on_close(const char *path) {
  if (!path) return 1;
  if (!strncmp(path, "sdmc:", 5)) path += 5;
  return strncmp(path, IL2CPP_PREFIX, sizeof IL2CPP_PREFIX - 1) != 0;
}
