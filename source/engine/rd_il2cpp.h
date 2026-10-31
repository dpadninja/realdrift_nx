/* rd_il2cpp.h -- the exported il2cpp runtime API the port calls, resolved ONCE from
 * libil2cpp.so (main.c: rd_il2cpp_load), plus small helpers built on it.
 * Host-testable (tests/c/test_rd_il2cpp.c): the symbol lookup is a callback.
 * MIT (see LICENSE). */
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
  void  *(*resolve_icall)(const char *name);           /* engine icall by registered name */
  const void *(*class_get_type)(void *klass);
  void  *(*type_get_object)(const void *type);         /* System.Type of an Il2CppType */
  uint32_t (*array_length)(void *array);
  int32_t (*string_length)(void *str);
  const uint16_t *(*string_chars)(void *str);          /* UTF-16 */
  void  *(*class_get_nested_types)(void *klass, void **iter);   /* NULL when done */
  const char *(*class_get_name)(void *klass);
  void  *(*object_new)(void *klass);                   /* fields zeroed, no constructor run */
  void  *(*object_get_class)(void *obj);
  int64_t (*gc_get_heap_size)(void);                   /* managed (Boehm) heap, bytes */
  int64_t (*gc_get_used_size)(void);                   /* of which in use */
  int    (*gc_is_disabled)(void);                      /* 1 while collections are off */
} rd_il2cpp_api;

extern rd_il2cpp_api g_il2cpp;

/* Symbol lookup callback: the address of `name`, or NULL. */
typedef void *(*rd_sym_fn)(void *ctx, const char *name);
/* Fills *api from `sym(ctx, "il2cpp_...")`. Returns how many functions were NOT found
 * (each is reported through `missing`, which may be NULL). */
int rd_il2cpp_load(rd_il2cpp_api *api, rd_sym_fn sym, void *ctx,
                   void (*missing)(const char *name));

/* A class by assembly/namespace/name, or NULL (also if the API or assembly is missing). */
void *rd_il2cpp_class(const rd_il2cpp_api *api, const char *assembly, const char *ns,
                      const char *name);

/* il2cpp_field_get_offset of a value type's field counts the boxed object header. */
#define RD_IL2CPP_OBJECT_HEADER 0x10

/* A nested class of `outer` by name (e.g. ParticleSystem/Particle), or NULL. */
void *rd_il2cpp_nested_class(const rd_il2cpp_api *api, void *outer, const char *name);

/* Element i of a managed object array (Il2CppArray: header, bounds, max_length, data). */
#define RD_IL2CPP_ARRAY_DATA 0x20
static inline void *rd_il2cpp_array_at(void *array, uint32_t i) {
  return ((void **)((char *)array + RD_IL2CPP_ARRAY_DATA))[i];
}

/* 1 if the managed string `str` equals the ASCII string `s`. */
int rd_il2cpp_str_eq(const rd_il2cpp_api *api, void *str, const char *s);

/* Field offset by name; 1 on success. */
int rd_il2cpp_field_offset(const rd_il2cpp_api *api, void *klass, const char *name,
                           uint32_t *offset);

#define RD_MAX_FIELDS 32
/* Field offset lookup callback: 1 and *offset if `name` exists. */
typedef int (*rd_field_offset_fn)(void *ctx, const char *name, uint32_t *offset);
/* Offsets of n fields by name. Returns 1 and fills out[0..n) only if EVERY field was found
 * (offset 0 is the object header, never a field); rd_il2cpp_fields looks them up in klass. */
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
