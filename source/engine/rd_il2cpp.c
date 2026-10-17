/* rd_il2cpp.c -- see rd_il2cpp.h. MIT (see LICENSE). */
#include <string.h>
#include "rd_il2cpp.h"

rd_il2cpp_api g_il2cpp;

static const struct { const char *name; size_t off; } API[] = {
  { "il2cpp_domain_get",                 offsetof(rd_il2cpp_api, domain_get) },
  { "il2cpp_domain_assembly_open",       offsetof(rd_il2cpp_api, domain_assembly_open) },
  { "il2cpp_assembly_get_image",         offsetof(rd_il2cpp_api, assembly_get_image) },
  { "il2cpp_class_from_name",            offsetof(rd_il2cpp_api, class_from_name) },
  { "il2cpp_class_get_method_from_name", offsetof(rd_il2cpp_api, class_get_method_from_name) },
  { "il2cpp_class_get_field_from_name",  offsetof(rd_il2cpp_api, class_get_field_from_name) },
  { "il2cpp_field_get_offset",           offsetof(rd_il2cpp_api, field_get_offset) },
  { "il2cpp_array_new",                  offsetof(rd_il2cpp_api, array_new) },
  { "il2cpp_gchandle_new",               offsetof(rd_il2cpp_api, gchandle_new) },
  { "il2cpp_string_new",                 offsetof(rd_il2cpp_api, string_new) },
  { "il2cpp_resolve_icall",              offsetof(rd_il2cpp_api, resolve_icall) },
  { "il2cpp_class_get_type",             offsetof(rd_il2cpp_api, class_get_type) },
  { "il2cpp_type_get_object",            offsetof(rd_il2cpp_api, type_get_object) },
  { "il2cpp_array_length",               offsetof(rd_il2cpp_api, array_length) },
  { "il2cpp_string_length",              offsetof(rd_il2cpp_api, string_length) },
  { "il2cpp_string_chars",               offsetof(rd_il2cpp_api, string_chars) },
  { "il2cpp_class_get_nested_types",     offsetof(rd_il2cpp_api, class_get_nested_types) },
  { "il2cpp_class_get_name",             offsetof(rd_il2cpp_api, class_get_name) },
};

int rd_il2cpp_load(rd_il2cpp_api *api, rd_sym_fn sym, void *ctx,
                   void (*missing)(const char *name)) {
  int n_missing = 0;
  memset(api, 0, sizeof *api);
  for (unsigned i = 0; i < sizeof API / sizeof API[0]; i++) {
    void *p = sym ? sym(ctx, API[i].name) : NULL;
    if (!p) { n_missing++; if (missing) missing(API[i].name); }
    memcpy((char *)api + API[i].off, &p, sizeof p);   /* every member is a function pointer */
  }
  return n_missing;
}

void *rd_il2cpp_class(const rd_il2cpp_api *api, const char *assembly, const char *ns,
                      const char *name) {
  if (!api->domain_get || !api->domain_assembly_open || !api->assembly_get_image ||
      !api->class_from_name)
    return NULL;
  void *dom = api->domain_get();
  void *as = dom ? api->domain_assembly_open(dom, assembly) : NULL;
  void *img = as ? api->assembly_get_image(as) : NULL;
  return img ? api->class_from_name(img, ns, name) : NULL;
}

void *rd_il2cpp_nested_class(const rd_il2cpp_api *api, void *outer, const char *name) {
  if (!api->class_get_nested_types || !api->class_get_name || !outer || !name) return NULL;
  void *iter = NULL, *k;
  while ((k = api->class_get_nested_types(outer, &iter)) != NULL) {
    const char *n = api->class_get_name(k);
    if (n && !strcmp(n, name)) return k;
  }
  return NULL;
}

int rd_il2cpp_str_eq(const rd_il2cpp_api *api, void *str, const char *s) {
  if (!api->string_length || !api->string_chars || !str || !s) return 0;
  const int32_t n = api->string_length(str);
  const uint16_t *c = api->string_chars(str);
  if (!c || n < 0 || (size_t)n != strlen(s)) return 0;
  for (int32_t i = 0; i < n; i++)
    if (c[i] != (unsigned char)s[i]) return 0;
  return 1;
}

int rd_il2cpp_field_offset(const rd_il2cpp_api *api, void *klass, const char *name,
                           uint32_t *offset) {
  if (!api->class_get_field_from_name || !api->field_get_offset || !klass) return 0;
  void *f = api->class_get_field_from_name(klass, name);
  if (!f) return 0;
  *offset = (uint32_t)api->field_get_offset(f);
  return 1;
}

int rd_resolve_fields(rd_field_offset_fn lookup, void *ctx, const char *const names[], int n,
                      uint32_t out[]) {
  uint32_t tmp[RD_MAX_FIELDS];
  if (!lookup || n <= 0 || n > RD_MAX_FIELDS) return 0;
  for (int i = 0; i < n; i++)
    if (!lookup(ctx, names[i], &tmp[i]) || tmp[i] == 0) return 0;
  memcpy(out, tmp, (size_t)n * sizeof tmp[0]);
  return 1;
}

struct field_ctx { const rd_il2cpp_api *api; void *klass; };
/* rd_field_offset_fn over the il2cpp API for one class (ctx is a struct field_ctx). */
static int api_lookup(void *ctx, const char *name, uint32_t *offset) {
  const struct field_ctx *c = ctx;
  return rd_il2cpp_field_offset(c->api, c->klass, name, offset);
}

int rd_il2cpp_fields(const rd_il2cpp_api *api, void *klass, const char *const names[], int n,
                     uint32_t out[]) {
  struct field_ctx c = { api, klass };
  return rd_resolve_fields(api_lookup, &c, names, n, out);
}

int rd_find_vtable_slot(const uint64_t *klass, int n_words, uint64_t method_ptr, uint64_t method) {
  if (!klass || !method_ptr || !method) return -1;
  for (int i = 0; i + 1 < n_words; i++)
    if (klass[i] == method_ptr && klass[i + 1] == method) return i;
  return -1;
}
