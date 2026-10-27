/* rd_fsdeny.c -- see rd_fsdeny.h. MIT (see LICENSE). */
#include <string.h>
#include "rd_game.h"
#include "rd_fsdeny.h"

#define UNITY_PREFIX RD_GAME_HOME_DIR "/" RD_UNITY_DATA_DIR "/"

static int g_analytics;

void rd_fsdeny_set_analytics(int on) { g_analytics = on != 0; }

int rd_fsdeny_write(const char *path) {
  if (!g_analytics || !path) return 0;
  if (!strncmp(path, "sdmc:", 5)) path += 5;
  if (strncmp(path, UNITY_PREFIX, sizeof UNITY_PREFIX - 1)) return 0;
  const char *id = path + sizeof UNITY_PREFIX - 1;          /* <project id>/Analytics... */
  const char *slash = strchr(id, '/');
  if (!slash || slash == id) return 0;
  const char *dir = slash + 1;
  const size_t n = sizeof RD_ANALYTICS_DIR - 1;
  return !strncmp(dir, RD_ANALYTICS_DIR, n) && (dir[n] == '\0' || dir[n] == '/');
}
