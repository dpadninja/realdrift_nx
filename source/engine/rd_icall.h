/* rd_icall.h -- replace IL2CPP internal calls by NAME at registration time.
 * Host-testable (tests/c/test_rd_icall.c).
 *
 * libunity registers every engine icall (e.g. "UnityEngine.Input::get_touchCount") through
 * il2cpp_add_internal_call(name, fn), which it resolves with dlsym(). dlsym_fake hands it
 * rd_add_internal_call() instead; for names in the override table we register our function,
 * everything else goes through unchanged. Unlike the inherited RVA hooks this needs no
 * per-build offsets: icall names are stable across builds of the same Unity version.
 * An override with `orig` set receives the engine's function there, so it can wrap it.
 * MIT (see LICENSE). */
#ifndef RD_ICALL_H
#define RD_ICALL_H

typedef struct { const char *name; void *fn; void **orig; } rd_icall_override;
typedef void (*rd_add_icall_fn)(const char *name, const void *fn);

/* n override tables (each NULL-name terminated) and the real il2cpp_add_internal_call. */
void rd_icall_setup(const rd_icall_override *const tables[], int n, rd_add_icall_fn real);

/* Adds a table consulted after the rd_icall_setup ones (mods register theirs at boot, before
 * libunity registers its icalls). Up to RD_ICALL_MAX_EXTRA tables; returns 0, -1 if full. */
#define RD_ICALL_MAX_EXTRA 4
int rd_icall_add_table(const rd_icall_override *table);

/* Replacement for il2cpp_add_internal_call. */
void rd_add_internal_call(const char *name, const void *fn);

/* The entry for `name` in one table, or NULL. */
const rd_icall_override *rd_icall_find(const rd_icall_override *table, const char *name);
/* Our function for `name`, or NULL. */
void *rd_icall_lookup(const rd_icall_override *table, const char *name);

/* Number of overrides applied so far (diagnostics). */
int rd_icall_applied(void);

#endif
