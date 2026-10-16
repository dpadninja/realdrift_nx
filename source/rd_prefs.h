/* rd_prefs.h -- SharedPreferences getter semantics (Android contract) for the prefs.kv
 * store in unity_jni.c. Host-testable (tests/c/test_rd_prefs.c).
 *
 * Inherited bugs fixed here (run 06):
 *   - getString(key, def) ignored `def` and returned "" for a missing key;
 *   - getFloat(key, def) never consulted the store at all (fell through to the
 *     orientation stub in jni_fake.c act_float and returned 0.0f). */
#ifndef RD_PREFS_H
#define RD_PREFS_H

#include <stddef.h>
#include <stdio.h>

/* getString: stored value if present, else `def` (may be NULL == Java null). */
const char *rd_pref_string(const char *stored, const char *def);

/* getFloat: stored value parsed as float if present and numeric, else `def`. */
float rd_pref_float(const char *stored, float def);

/* ---- the prefs.kv file: one "T\tkey\tvalue\n" line per entry -----------------------------
 * T is the type letter (s/i/f/b/l). '\\', TAB and LF inside key/value are escaped as \\ \t \n.
 * Lines may be arbitrarily long: the game keeps its whole progress in one base64 string. */

/* Writes one entry. */
void rd_prefs_write_entry(FILE *f, char type, const char *key, const char *val);

/* Reads the next well-formed entry. (*line, *cap) is a caller-owned buffer that grows as needed
 * (start with NULL/0, free(*line) at the end). On success returns 1 and points *key / *val
 * into *line (unescaped, valid until the next call); returns 0 at end of file. Malformed
 * lines are skipped. */
int rd_prefs_read_entry(FILE *f, char **line, size_t *cap, char *type, char **key, char **val);

/* Saving: write to `tmp`, then rd_prefs_commit(tmp, path) replaces `path` (Horizon's rename
 * does not overwrite, so the old file is removed first). Returns 0 or -1.
 * Loading: rd_prefs_pick(path, tmp) is the file to read -- `tmp` only if `path` is missing,
 * i.e. a save was interrupted between the remove and the rename. NULL if neither exists. */
int rd_prefs_commit(const char *tmp, const char *path);
const char *rd_prefs_pick(const char *path, const char *tmp);

#endif
