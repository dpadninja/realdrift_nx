/* rd_il2cpp.h -- the exported il2cpp runtime API the port calls, resolved ONCE from
 * libil2cpp.so (main.c: rd_il2cpp_load), plus small helpers built on it.
 * Host-testable (tests/c/test_rd_il2cpp.c): the symbol lookup is a callback. */
#ifndef RD_IL2CPP_H
#define RD_IL2CPP_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  void  *(*domain_get)(void);
  void  *(*domain_assembly_open)(void *domain, const char *name);
  void  *(*assembly_get_image)(void *assembly);
  void  *(*class_from_name)(void *image, const char *ns, const char *name);
  /* Returns a MethodInfo*; its first word is methodPointer (IL2CPP v24). */
  void **(*class_get_method_from_name)(void *klass, const char *name, int argc);
  void  *(*class_get_field_from_name)(void *klass, const char *name);
  size_t (*field_get_offset)(void *field);
  void  *(*array_new)(void *klass, uintptr_t length);
  uint32_t (*gchandle_new)(void *obj, int pinned);
  void  *(*string_new)(const char *s);
} rd_il2cpp_api;

extern rd_il2cpp_api g_il2cpp;

/* Fills *api from `sym(ctx, "il2cpp_...")`. Returns how many functions were NOT found
 * (each is logged by the caller via `missing`, may be NULL). */
typedef void *(*rd_sym_fn)(void *ctx, const char *name);
int rd_il2cpp_load(rd_il2cpp_api *api, rd_sym_fn sym, void *ctx,
                   void (*missing)(const char *name));

/* A class by assembly/namespace/name, or NULL (also if the API or assembly is missing). */
void *rd_il2cpp_class(const rd_il2cpp_api *api, const char *assembly, const char *ns,
                      const char *name);

/* Field offset by name; 1 on success. */
int rd_il2cpp_field_offset(const rd_il2cpp_api *api, void *klass, const char *name,
                           uint32_t *offset);

/* Offsets of n fields by name. Returns 1 and fills out[0..n) only if EVERY field was found
 * (offset 0 is the object header, never a field). `lookup(ctx, name, &off)`. */
#define RD_MAX_FIELDS 32
typedef int (*rd_field_offset_fn)(void *ctx, const char *name, uint32_t *offset);
int rd_resolve_fields(rd_field_offset_fn lookup, void *ctx, const char *const names[], int n,
                      uint32_t out[]);
int rd_il2cpp_fields(const rd_il2cpp_api *api, void *klass, const char *const names[], int n,
                     uint32_t out[]);

/* How far into an Il2CppClass blob to look for the vtable. sizeof(Il2CppClass) is ~0x130 in
 * v24 and the vtable follows it; 1024 words also covers classes with large vtables. */
#define RD_IL2CPP_CLASS_SCAN_WORDS 1024

/* Index (in 8-byte words) of the VirtualInvokeData {methodPtr, method} pair inside an
 * Il2CppClass blob of n_words words, or -1. Used to swap a virtual method in the class vtable
 * without knowing the Il2CppClass layout. */
int rd_find_vtable_slot(const uint64_t *klass, int n_words, uint64_t method_ptr, uint64_t method);

#endif
