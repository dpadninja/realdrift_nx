/* rd_mod_ini.h -- reading a mod's mod.ini and patch.ini.
 * Host-testable (tests/c/test_rd_mod_ini.c).
 *
 * INI text: "key = value" lines, "[section]" headers, '#' or ';' starts a comment line, blank
 * lines are ignored, spaces around keys and values are trimmed. mod.ini uses no sections;
 * patch.ini has one section per class: "[Assembly:Namespace.Class]" (nested classes as
 * "Outer/Inner", the game's classes have no namespace: "[Assembly-CSharp:Drivetrain]").
 *
 * mod.ini keys known to the port are the RD_MOD_INI_KEYS table (rd_mod_ini.c): docs/MODDING.md
 * and tools/mod_new.py are checked against it (tests/test_modding_docs.py). Other keys are the
 * mod's own settings (rd_mod_api.ini).
 * MIT (see LICENSE). */
#ifndef RD_MOD_INI_H
#define RD_MOD_INI_H

#include <stddef.h>

#define RD_INI_MAX_ENTRIES 128
#define RD_INI_SECTION_LEN 128
#define RD_INI_KEY_LEN     64
#define RD_INI_VALUE_LEN   192

typedef struct {
  char section[RD_INI_SECTION_LEN];   /* "" before the first [section] */
  char key[RD_INI_KEY_LEN];
  char value[RD_INI_VALUE_LEN];
  int  line;
} rd_ini_entry;

typedef struct {
  rd_ini_entry e[RD_INI_MAX_ENTRIES];
  int          n;
} rd_ini;

/* Parses INI text into *ini. Returns 0, or the 1-based number of the first bad line (no '=',
 * an empty key, an unclosed '[', a key or value too long, or more than RD_INI_MAX_ENTRIES). */
int rd_ini_parse(const char *text, rd_ini *ini);

/* The value of key in section ("" for none), or NULL. The last occurrence wins. */
const char *rd_ini_get(const rd_ini *ini, const char *section, const char *key);

#define RD_MOD_ID_LEN      32
#define RD_MOD_NAME_LEN    64
#define RD_MOD_VERSION_LEN 24

/* What the port reads from mod.ini. */
typedef struct {
  char id[RD_MOD_ID_LEN + 1];          /* [a-z0-9_-], required, equals the folder name */
  char name[RD_MOD_NAME_LEN + 1];      /* free text; defaults to id */
  char version[RD_MOD_VERSION_LEN + 1];
  int  api;                            /* RD_MOD_API_VERSION the mod was written for */
  int  enabled;                        /* 0 skips the mod (default 1) */
  int  priority;                       /* higher wins file and icall conflicts (default 0) */
} rd_mod_info;

/* One known mod.ini key, for docs and the mod_new template. */
typedef struct { const char *key; int required; const char *help; } rd_mod_ini_key;
extern const rd_mod_ini_key RD_MOD_INI_KEYS[];
extern const int RD_MOD_INI_NKEYS;

/* Fills *info from a parsed mod.ini of the mod in folder `folder` (its name, not a path).
 * Returns 0, or -1 with a one-line reason in err (missing or malformed key, id differing from
 * the folder name). */
int rd_mod_info_read(const rd_ini *ini, const char *folder, rd_mod_info *info, char *err,
                     size_t errn);

/* Splits a patch.ini section "Assembly:Namespace.Class" (class may be "Outer/Inner").
 * Returns 0, or -1 if it has no ':' or an empty part; ns is "" when there is no '.'. */
int rd_patch_section(const char *section, char *assembly, size_t an, char *ns, size_t nn,
                     char *cls, size_t cn);

#endif
