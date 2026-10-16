/* sources: source/rd_il2cpp.c */
#include <string.h>
#include "check.h"
#include "rd_il2cpp.h"

static void test_find_vtable_slot(void) {
  uint64_t klass[16] = {0};
  klass[3] = 0x7a09f8;           /* a different method using the same code: not a match */
  klass[9] = 0x7a09f8; klass[10] = 0xabc000;   /* {methodPtr, method} */
  CHECK(rd_find_vtable_slot(klass, 16, 0x7a09f8, 0xabc000) == 9);
  CHECK(rd_find_vtable_slot(klass, 10, 0x7a09f8, 0xabc000) == -1);  /* pair cut off */
  CHECK(rd_find_vtable_slot(klass, 16, 0x7a09f8, 0x1) == -1);
  CHECK(rd_find_vtable_slot(NULL, 16, 1, 1) == -1);
  CHECK(rd_find_vtable_slot(klass, 16, 0, 0xabc000) == -1);
}


static int fake_lookup(void *ctx, const char *name, uint32_t *off) {
  (void)ctx;
  static const struct { const char *n; uint32_t o; } F[] = {
    { "mobileSteerInput", 0x1dc }, { "raceManager", 0x18 }, { "pausePanel", 0x80 },
  };
  for (unsigned i = 0; i < sizeof F / sizeof F[0]; i++)
    if (!strcmp(F[i].n, name)) { *off = F[i].o; return 1; }
  return 0;
}


static void test_resolve_fields(void) {
  const char *const ok[] = { "raceManager", "pausePanel" };
  uint32_t out[2] = { 7, 7 };
  CHECK(rd_resolve_fields(fake_lookup, NULL, ok, 2, out) && out[0] == 0x18 && out[1] == 0x80);
  const char *const missing[] = { "mobileSteerInput", "renamedField" };
  out[0] = out[1] = 7;
  CHECK(!rd_resolve_fields(fake_lookup, NULL, missing, 2, out));
  CHECK(out[0] == 7 && out[1] == 7);                    /* untouched on failure */
  CHECK(!rd_resolve_fields(NULL, NULL, ok, 2, out));
}


/* ---- API table + helpers over a fake il2cpp ---- */
static int g_dom, g_asm, g_img, g_klass, g_field;
static void *f_domain_get(void) { return &g_dom; }
static void *f_assembly_open(void *d, const char *n) {
  return d == &g_dom && !strcmp(n, "Assembly-CSharp") ? &g_asm : NULL;
}
static void *f_get_image(void *a) { return a == &g_asm ? &g_img : NULL; }
static void *f_class_from_name(void *img, const char *ns, const char *n) {
  return img == &g_img && !strcmp(ns, "") && !strcmp(n, "UIManager") ? &g_klass : NULL;
}
static void *f_get_field(void *k, const char *n) {
  return k == &g_klass && !strcmp(n, "raceManager") ? &g_field : NULL;
}
static size_t f_field_offset(void *f) { return f == &g_field ? 0x18 : 0; }

static void *fake_sym(void *ctx, const char *name) {
  (void)ctx;
  static const struct { const char *n; void *p; } S[] = {
    { "il2cpp_domain_get", (void *)f_domain_get },
    { "il2cpp_domain_assembly_open", (void *)f_assembly_open },
    { "il2cpp_assembly_get_image", (void *)f_get_image },
    { "il2cpp_class_from_name", (void *)f_class_from_name },
    { "il2cpp_class_get_field_from_name", (void *)f_get_field },
    { "il2cpp_field_get_offset", (void *)f_field_offset },
  };
  for (unsigned i = 0; i < sizeof S / sizeof S[0]; i++)
    if (!strcmp(S[i].n, name)) return S[i].p;
  return NULL;
}

static int g_missing_calls;
static void on_missing(const char *name) { (void)name; g_missing_calls++; }

static void test_load_and_helpers(void) {
  rd_il2cpp_api api;
  int missing = rd_il2cpp_load(&api, fake_sym, NULL, on_missing);
  CHECK(missing == 4 && g_missing_calls == 4);   /* method, array, gchandle, string */
  CHECK(api.domain_get == f_domain_get && api.field_get_offset == f_field_offset);
  CHECK(api.class_get_method_from_name == NULL && api.string_new == NULL);
  CHECK(rd_il2cpp_class(&api, "Assembly-CSharp", "", "UIManager") == &g_klass);
  CHECK(rd_il2cpp_class(&api, "Other", "", "UIManager") == NULL);
  uint32_t off = 0;
  CHECK(rd_il2cpp_field_offset(&api, &g_klass, "raceManager", &off) && off == 0x18);
  CHECK(!rd_il2cpp_field_offset(&api, &g_klass, "nope", &off));
  const char *const names[] = { "raceManager" };
  uint32_t out[1] = { 0 };
  CHECK(rd_il2cpp_fields(&api, &g_klass, names, 1, out) && out[0] == 0x18);
  rd_il2cpp_api none;
  CHECK(rd_il2cpp_load(&none, NULL, NULL, NULL) == 10);
  CHECK(rd_il2cpp_class(&none, "Assembly-CSharp", "", "UIManager") == NULL);
}

int main(void) {
  RUN(test_find_vtable_slot);
  RUN(test_resolve_fields);
  RUN(test_load_and_helpers);
  return g_failures != 0;
}
