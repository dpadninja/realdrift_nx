/* rd_prefs.c -- see rd_prefs.h. MIT (see LICENSE). */
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "rd_prefs.h"

const char *rd_pref_string(const char *stored, const char *def) {
  return stored ? stored : def;
}

float rd_pref_float(const char *stored, float def) {
  if (!stored || !*stored) return def;
  char *end;
  float v = strtof(stored, &end);
  return (end != stored && *end == '\0') ? v : def;
}

static void put_escaped(FILE *f, const char *s) {
  for (; *s; s++) {
    if (*s == '\\' || *s == '\t' || *s == '\n') {
      fputc('\\', f);
      fputc(*s == '\t' ? 't' : *s == '\n' ? 'n' : '\\', f);
    } else {
      fputc(*s, f);
    }
  }
}

static char *unescape(char *s) {
  char *w = s;
  for (const char *o = s; *o; o++) {
    if (*o == '\\' && o[1]) {
      o++;
      *w++ = *o == 't' ? '\t' : *o == 'n' ? '\n' : *o;
    } else {
      *w++ = *o;
    }
  }
  *w = '\0';
  return s;
}

void rd_prefs_write_entry(FILE *f, char type, const char *key, const char *val) {
  fputc(type, f);
  fputc('\t', f);
  put_escaped(f, key ? key : "");
  fputc('\t', f);
  put_escaped(f, val ? val : "");
  fputc('\n', f);
}

/* One whole line without its '\n', any length. 0 at end of file. */
static int read_line(FILE *f, char **line, size_t *cap) {
  size_t n = 0;
  int c;
  while ((c = fgetc(f)) != EOF && c != '\n') {
    if (n + 1 >= *cap) {
      size_t ncap = *cap ? *cap * 2 : 256;
      char *p = realloc(*line, ncap);
      if (!p) return 0;
      *line = p;
      *cap = ncap;
    }
    (*line)[n++] = (char)c;
  }
  if (c == EOF && n == 0) return 0;
  if (!*line) {                      /* an empty first line: still hand back a buffer */
    *line = malloc(1);
    if (!*line) return 0;
    *cap = 1;
  }
  (*line)[n] = '\0';
  return 1;
}

int rd_prefs_read_entry(FILE *f, char **line, size_t *cap, char *type, char **key, char **val) {
  while (read_line(f, line, cap)) {
    char *s = *line;
    if (!s[0] || s[1] != '\t') continue;
    char *k = s + 2, *t = strchr(k, '\t');
    if (!t) continue;
    *t = '\0';
    *type = s[0];
    *key = unescape(k);
    *val = unescape(t + 1);
    return 1;
  }
  return 0;
}

static int exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0;
}

int rd_prefs_commit(const char *tmp, const char *path) {
  if (exists(path) && remove(path) != 0) return -1;
  return rename(tmp, path) == 0 ? 0 : -1;
}

const char *rd_prefs_pick(const char *path, const char *tmp) {
  if (exists(path)) return path;
  return exists(tmp) ? tmp : NULL;
}
