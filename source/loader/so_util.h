/* so_util.h -- utils to load and hook .so modules
 *
 * Copyright (C) 2021 Andy Nguyen, fgsfds
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 * Forked from badpiggies_nx by ChanseyIsTheBest (https://github.com/ChanseyIsTheBest/badpiggies_nx,
 * commit 7726be3), from the so-loader lineage; changed for realdrift_nx. MIT (see LICENSE).
 */

#ifndef __SO_UTIL_H__
#define __SO_UTIL_H__

#include <stdint.h>
#include <stddef.h>
#include <elf.h>

#define ALIGN_MEM(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

#define SO_MAX_SEGMENTS 8

/* One import-table row: symbol name -> our implementation. */
typedef struct {
  char *symbol;
  uintptr_t func;
} DynLibFunction;

/* A loaded shared object: its mapping, program headers and the temporary file image. */
typedef struct so_module {
  struct so_module *next;
  char name[64];

  // entire LOAD zone
  void *load_base, *load_virtbase;
  size_t load_size;
  void *load_memrv; // VirtmemReservation *

  // copy of the unmodified program headers (link-time vaddrs),
  // needed for dl_iterate_phdr / exception unwinding
  Elf64_Phdr phdr[SO_MAX_SEGMENTS * 2];
  int phnum;

  // temporary file image
  void *so_base;
  size_t so_size;

  Elf64_Ehdr *elf_hdr;
  Elf64_Phdr *prog_hdr;
  Elf64_Shdr *sec_hdr;
  Elf64_Sym *syms;
  int num_syms;
  char *shstrtab;
  char *dynstrtab;
} so_module;

/* Loads an ELF shared object: reads `filename` into a temporary image and maps its LOAD
 * segments into the reserved region at `base` (at most max_size bytes). Returns 0 or -1. */
int so_load(so_module *mod, const char *filename, void *base, size_t max_size);
/* Applies the module's relative and absolute relocations (no imports yet). Returns 0. */
int so_relocate(so_module *mod);
/* Resolves imports against funcs[] and the already-loaded modules; with
 * taint_missing_imports, unresolved slots get a recognisable bad address instead of 0.
 * Unresolved names are logged. Returns 0. */
int so_resolve(so_module *mod, DynLibFunction *funcs, int num_funcs, int taint_missing_imports);
/* Maps the module as code memory (RX segments first, the rest RW); fatal on failure. */
void so_finalize(so_module *mod);
/* Flushes the D-cache and invalidates the I-cache over the module's mapped range. */
void so_flush_caches(so_module *mod);
/* Frees the temporary file image (after so_finalize). */
void so_free_temp(so_module *mod);
/* Runs the module's .init_array constructors in order. */
void so_execute_init_array(so_module *mod);

/* Address of an exported symbol in the executable mapping; fatal if it is missing. */
uintptr_t so_find_addr_rx(so_module *mod, const char *symbol);
/* Same, but returns 0 when the symbol is missing. */
uintptr_t so_try_find_addr_rx(so_module *mod, const char *symbol);
/* dlsym() backing: `name` among the exports of every loaded module, or NULL. */
void *so_resolve_external(const char *name);
/* The loaded module whose mapping contains `addr`, or NULL (backtraces). */
so_module *so_find_module_by_addr(const void *addr);
/* Writes /proc/self/maps-style lines for the loaded modules into buf (cap bytes, NUL
 * terminated); returns the bytes written without the NUL. */
int so_dump_maps(char *buf, size_t cap);

/* dl_iterate_phdr() over the loaded modules (the libunwind inside libc++_shared needs it). */
int so_dl_iterate_phdr(int (*callback)(void *info, size_t size, void *data), void *data);

/* Writes len bytes over already-mapped code at dst through a temporary writable alias, then
 * flushes the caches. Returns 0, or a negative value if no alias could be mapped. */
int so_patch_code(void *dst, const void *src, size_t len);

#endif
