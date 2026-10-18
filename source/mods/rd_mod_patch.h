/* rd_mod_patch.h -- patch.ini values: parsed for the C# type of their field and written into
 * objects. Host-testable (tests/c/test_rd_mod_patch.c); finding the objects is rd_mods.c.
 *
 * Supported field types: bool (0/1/true/false), int, uint, long, float, double. A value that
 * does not fit its type is rejected when the patch is resolved, never written.
 * MIT (see LICENSE). */
#ifndef RD_MOD_PATCH_H
#define RD_MOD_PATCH_H

#include <stdint.h>

typedef struct {
  int type;                  /* RD_IL2CPP_TYPE_* of the field */
  union { uint8_t b; int32_t i32; uint32_t u32; int64_t i64; float f32; double f64; } v;
} rd_patch_value;

/* Parses text for a field of `type`. Returns 0, -1 if the text is not a valid value of the
 * type, -2 if the type is not supported. */
int rd_patch_parse(int type, const char *text, rd_patch_value *out);

/* Writes v into the field at `offset` of obj. */
void rd_patch_write(void *obj, uint32_t offset, const rd_patch_value *v);

/* 1 if `name` matches `pattern`: '*' is any run of characters (also none), '?' one character,
 * everything else itself (case-sensitive). The patch.ini "@ filter" of object names. */
int rd_name_match(const char *pattern, const char *name);

/* Name of a supported type ("float", ...), or "unsupported". */
const char *rd_patch_type_name(int type);

#endif
