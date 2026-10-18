/* rd_mod_patch.c -- see rd_mod_patch.h. MIT (see LICENSE). */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "rd_il2cpp.h"
#include "rd_mod_patch.h"

const char *rd_patch_type_name(int type) {
  switch (type) {
    case RD_IL2CPP_TYPE_BOOLEAN: return "bool";
    case RD_IL2CPP_TYPE_I4:      return "int";
    case RD_IL2CPP_TYPE_U4:      return "uint";
    case RD_IL2CPP_TYPE_I8:      return "long";
    case RD_IL2CPP_TYPE_R4:      return "float";
    case RD_IL2CPP_TYPE_R8:      return "double";
    default:                     return "unsupported";
  }
}

int rd_patch_parse(int type, const char *text, rd_patch_value *out) {
  char *end;
  errno = 0;
  out->type = type;
  if (!text || !*text) return -1;
  switch (type) {
    case RD_IL2CPP_TYPE_BOOLEAN:
      if (!strcmp(text, "1") || !strcasecmp(text, "true")) { out->v.b = 1; return 0; }
      if (!strcmp(text, "0") || !strcasecmp(text, "false")) { out->v.b = 0; return 0; }
      return -1;
    case RD_IL2CPP_TYPE_I4: {
      const long long v = strtoll(text, &end, 0);
      if (*end || errno || v < INT32_MIN || v > INT32_MAX) return -1;
      out->v.i32 = (int32_t)v;
      return 0;
    }
    case RD_IL2CPP_TYPE_U4: {
      const unsigned long long v = strtoull(text, &end, 0);
      if (*end || errno || text[0] == '-' || v > UINT32_MAX) return -1;
      out->v.u32 = (uint32_t)v;
      return 0;
    }
    case RD_IL2CPP_TYPE_I8: {
      const long long v = strtoll(text, &end, 0);
      if (*end || errno) return -1;
      out->v.i64 = v;
      return 0;
    }
    case RD_IL2CPP_TYPE_R4:
    case RD_IL2CPP_TYPE_R8: {
      const double v = strtod(text, &end);
      if (*end || errno || v != v) return -1;   /* trailing junk, out of range, NaN */
      if (type == RD_IL2CPP_TYPE_R4) out->v.f32 = (float)v; else out->v.f64 = v;
      return 0;
    }
    default:
      return -2;
  }
}

int rd_name_match(const char *pattern, const char *name) {
  const char *star = NULL, *resume = NULL;   /* last '*' and where its match would continue */
  while (*name) {
    if (*pattern == '*') { star = pattern++; resume = name; continue; }
    if (*pattern == '?' || *pattern == *name) { pattern++; name++; continue; }
    if (!star) return 0;
    pattern = star + 1;                      /* let the last '*' eat one more character */
    name = ++resume;
  }
  while (*pattern == '*') pattern++;
  return *pattern == 0;
}

void rd_patch_write(void *obj, uint32_t offset, const rd_patch_value *v) {
  char *p = (char *)obj + offset;
  switch (v->type) {
    case RD_IL2CPP_TYPE_BOOLEAN: memcpy(p, &v->v.b, 1); break;
    case RD_IL2CPP_TYPE_I4:      memcpy(p, &v->v.i32, 4); break;
    case RD_IL2CPP_TYPE_U4:      memcpy(p, &v->v.u32, 4); break;
    case RD_IL2CPP_TYPE_I8:      memcpy(p, &v->v.i64, 8); break;
    case RD_IL2CPP_TYPE_R4:      memcpy(p, &v->v.f32, 4); break;
    case RD_IL2CPP_TYPE_R8:      memcpy(p, &v->v.f64, 8); break;
    default: break;
  }
}
