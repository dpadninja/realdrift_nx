/* rd_mod_ini.c -- see rd_mod_ini.h. MIT (see LICENSE). */
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_mod_ini.h"

const rd_mod_ini_key RD_MOD_INI_KEYS[] = {
  { "id",       1, "folder name of the mod: lower-case letters, digits, '_' and '-'" },
  { "name",     0, "shown in the log; defaults to id" },
  { "version",  0, "the mod's own version, free text" },
  { "api",      1, "RD_MOD_API_VERSION the mod was built for (sdk/rd_mod.h)" },
  { "enabled",  0, "0 = skip this mod, 1 = load it (default 1)" },
  { "priority", 0, "higher wins when two mods replace the same file or icall (default 0)" },
};
const int RD_MOD_INI_NKEYS = (int)(sizeof RD_MOD_INI_KEYS / sizeof RD_MOD_INI_KEYS[0]);

/* Copies [b, e) trimmed of spaces into out (n bytes). 0, or -1 if it does not fit. */
static int copy_trim(char *out, size_t n, const char *b, const char *e) {
  while (b < e && isspace((unsigned char)*b)) b++;
  while (e > b && isspace((unsigned char)e[-1])) e--;
  if ((size_t)(e - b) >= n) return -1;
  memcpy(out, b, (size_t)(e - b));
  out[e - b] = 0;
  return 0;
}

int rd_ini_parse(const char *text, rd_ini *ini) {
  ini->n = 0;
  char section[RD_INI_SECTION_LEN] = "";
  int line = 0;
  for (const char *p = text; p && *p;) {
    const char *eol = strchr(p, '\n');
    const char *end = eol ? eol : p + strlen(p);
    line++;
    const char *s = p;
    while (s < end && isspace((unsigned char)*s)) s++;
    p = eol ? eol + 1 : end;
    if (s == end || *s == '#' || *s == ';') continue;
    if (*s == '[') {
      const char *close = memchr(s, ']', (size_t)(end - s));
      if (!close || copy_trim(section, sizeof section, s + 1, close)) return line;
      continue;
    }
    const char *eq = memchr(s, '=', (size_t)(end - s));
    if (!eq || ini->n == RD_INI_MAX_ENTRIES) return line;
    for (const char *c = eq + 1; c < end; c++)   /* " # ..." after a value is a comment */
      if (*c == '#' && isspace((unsigned char)c[-1])) { end = c; break; }
    rd_ini_entry *e = &ini->e[ini->n];
    if (copy_trim(e->key, sizeof e->key, s, eq) || !e->key[0] ||
        copy_trim(e->value, sizeof e->value, eq + 1, end))
      return line;
    strcpy(e->section, section);
    e->line = line;
    ini->n++;
  }
  return 0;
}

const char *rd_ini_get(const rd_ini *ini, const char *section, const char *key) {
  for (int i = ini->n - 1; i >= 0; i--)
    if (!strcmp(ini->e[i].section, section) && !strcmp(ini->e[i].key, key)) return ini->e[i].value;
  return NULL;
}

/* *out = the integer value s; 0, or -1 if s is not one. */
static int to_int(const char *s, int *out) {
  char *end;
  const long v = strtol(s, &end, 10);
  if (!*s || *end || v < -1000000 || v > 1000000) return -1;
  *out = (int)v;
  return 0;
}

/* 1 if s is a valid mod id. */
static int valid_id(const char *s) {
  if (!*s) return 0;
  for (; *s; s++)
    if (!(islower((unsigned char)*s) || isdigit((unsigned char)*s) || *s == '_' || *s == '-'))
      return 0;
  return 1;
}

int rd_mod_info_read(const rd_ini *ini, const char *folder, rd_mod_info *info, char *err,
                     size_t errn) {
  memset(info, 0, sizeof *info);
  info->enabled = 1;
  for (int i = 0; i < RD_MOD_INI_NKEYS; i++) {
    if (RD_MOD_INI_KEYS[i].required && !rd_ini_get(ini, "", RD_MOD_INI_KEYS[i].key)) {
      snprintf(err, errn, "mod.ini: missing \"%s\"", RD_MOD_INI_KEYS[i].key);
      return -1;
    }
  }
  const char *id = rd_ini_get(ini, "", "id");
  if (strlen(id) > RD_MOD_ID_LEN || !valid_id(id)) {
    snprintf(err, errn, "mod.ini: id \"%s\" must be 1-%d of [a-z0-9_-]", id, RD_MOD_ID_LEN);
    return -1;
  }
  if (strcmp(id, folder)) {
    snprintf(err, errn, "mod.ini: id \"%s\" differs from the folder name \"%s\"", id, folder);
    return -1;
  }
  strcpy(info->id, id);
  const char *name = rd_ini_get(ini, "", "name"), *ver = rd_ini_get(ini, "", "version");
  snprintf(info->name, sizeof info->name, "%s", name ? name : id);
  snprintf(info->version, sizeof info->version, "%s", ver ? ver : "");
  static const struct { const char *key; size_t off; } INTS[] = {
    { "api", offsetof(rd_mod_info, api) },
    { "enabled", offsetof(rd_mod_info, enabled) },
    { "priority", offsetof(rd_mod_info, priority) },
  };
  for (size_t i = 0; i < sizeof INTS / sizeof INTS[0]; i++) {
    const char *v = rd_ini_get(ini, "", INTS[i].key);
    if (v && to_int(v, (int *)((char *)info + INTS[i].off))) {
      snprintf(err, errn, "mod.ini: \"%s\" must be a number, not \"%s\"", INTS[i].key, v);
      return -1;
    }
  }
  if (info->enabled != 0 && info->enabled != 1) {
    snprintf(err, errn, "mod.ini: \"enabled\" must be 0 or 1");
    return -1;
  }
  return 0;
}

/* Copies [b, e) into out (n bytes); 0, or -1 if empty or too long. */
static int part(char *out, size_t n, const char *b, const char *e) {
  return copy_trim(out, n, b, e) || !out[0] ? -1 : 0;
}

int rd_patch_target_parse(const char *section, rd_patch_target *t) {
  memset(t, 0, sizeof *t);
  const char *at = strchr(section, '@');
  const char *end = at ? at : section + strlen(section);
  if (at && part(t->filter, sizeof t->filter, at + 1, at + strlen(at))) return -1;
  const char *colon = memchr(section, ':', (size_t)(end - section));
  if (!colon || part(t->assembly, sizeof t->assembly, section, colon)) return -1;
  const char *name = colon + 1;
  /* the namespace ends at the last '.' before any nested-class '/' */
  const char *slash = memchr(name, '/', (size_t)(end - name));
  const char *dot = NULL;
  for (const char *q = name; q < (slash ? slash : end); q++) if (*q == '.') dot = q;
  if (dot) {
    if (part(t->ns, sizeof t->ns, name, dot)) return -1;
    name = dot + 1;
  }
  return part(t->cls, sizeof t->cls, name, end);
}
