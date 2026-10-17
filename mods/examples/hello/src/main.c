/* hello -- the smallest code mod: a line of text on screen and a log line per scene.
 * Template for new mods (tools/mod_new.py copies it). MIT (see LICENSE). */
#include "rd_mod.h"

static const rd_mod_api *api;
static const char *text;

static void on_scene(void *user, const char *scene) {
  (void)user;
  api->log(api, "scene %s", scene);
}

static void on_frame(void *user, uint32_t frame) {
  (void)user;
  (void)frame;
  api->draw_text(8, 8, 2, 0xFFFFFFFFu, text);   /* white, top-left, 2 px per font pixel */
}

RD_MOD_ENTRY {
  api = m;
  *user = 0;
  text = m->ini(m, "text");
  if (!text) text = "HELLO";
  m->log(m, "loaded, port mod API %u", m->version);
  m->on_scene(m, on_scene);
  m->on_frame(m, on_frame);
  return 0;
}
