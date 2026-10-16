/* sources: source/rd_patchsites.c source/rd_sig.c source/rd_a64.c */
/* find_patch_sites -- run the loader's pattern finders (source/rd_patchsites.c) on .so files.
 *
 *   make tools        (or: python -m tools.hostc tools/find_patch_sites.c build-host/find_patch_sites)
 *   build-host/find_patch_sites analysis/lib64_508/libunity.so analysis/lib64_508/libil2cpp.so
 *
 * Prints key=value lines (addresses are link-time, i.e. relative to the module base) and
 * exits non-zero if a finder fails. tests/test_patch_sites.py checks the output for Real
 * Drift 5.0.7 and 5.0.8. */
#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_patchsites.h"

typedef struct { unsigned char *data; size_t size; const Elf64_Phdr *ph; int phnum; } elf_file;

static int load(const char *path, elf_file *ef) {
  FILE *f = fopen(path, "rb");
  if (!f) return -1;
  fseek(f, 0, SEEK_END); ef->size = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
  ef->data = malloc(ef->size);
  if (!ef->data || fread(ef->data, 1, ef->size, f) != ef->size) { fclose(f); return -1; }
  fclose(f);
  const Elf64_Ehdr *eh = (const Elf64_Ehdr *)ef->data;
  if (ef->size < sizeof *eh || memcmp(eh->e_ident, ELFMAG, SELFMAG) || eh->e_machine != EM_AARCH64)
    return -1;
  ef->ph = (const Elf64_Phdr *)(ef->data + eh->e_phoff);
  ef->phnum = eh->e_phnum;
  return 0;
}

/* String at a link-time address, via the PT_LOAD that maps it. */
static const char *cstr(void *ctx, uint64_t va) {
  const elf_file *ef = ctx;
  for (int i = 0; i < ef->phnum; i++) {
    const Elf64_Phdr *p = &ef->ph[i];
    if (p->p_type == PT_LOAD && va >= p->p_vaddr && va < p->p_vaddr + p->p_filesz)
      return (const char *)(ef->data + p->p_offset + (va - p->p_vaddr));
  }
  return NULL;
}

static int image_of(elf_file *ef, rd_image *im) {
  for (int i = 0; i < ef->phnum; i++) {
    const Elf64_Phdr *p = &ef->ph[i];
    if (p->p_type == PT_LOAD && (p->p_flags & PF_X)) {
      im->code = (const uint32_t *)(ef->data + p->p_offset);
      im->vaddr = p->p_vaddr;
      im->nwords = p->p_filesz / 4;
      im->cstr = cstr;
      im->ctx = ef;
      return 0;
    }
  }
  return -1;
}

int main(int argc, char **argv) {
  int failed = 0;
  for (int a = 1; a < argc; a++) {
    elf_file ef = {0};
    rd_image im;
    if (load(argv[a], &ef) || image_of(&ef, &im)) {
      fprintf(stderr, "%s: not an AArch64 ELF with an executable segment\n", argv[a]);
      failed = 1;
      continue;
    }
    printf("file=%s\n", argv[a]);
    if (strstr(argv[a], "libunity")) {
      uint64_t gate = 0, skip = 0;
      int r = rd_find_choreo_gate(&im, &gate, &skip);
      printf("choreo_gate.result=%d\n", r);
      if (r == RD_SITE_OK) printf("choreo_gate=0x%llx\nchoreo_skip=0x%llx\n",
                                  (unsigned long long)gate, (unsigned long long)skip);
      else failed = 1;
    }
    if (strstr(argv[a], "libil2cpp")) {
      rd_gc_vars v;
      int r = rd_find_gc_vars(&im, &v);
      printf("gc.result=%d\n", r);
      if (r == RD_SITE_OK)
        printf("gc.sig_suspend=0x%llx\ngc.sig_restart=0x%llx\ngc.retry_signals=0x%llx\n"
               "gc.suspend_ack_sem=0x%llx\ngc.stop_count=0x%llx\ngc.threads=0x%llx\n",
               (unsigned long long)v.sig_suspend, (unsigned long long)v.sig_restart,
               (unsigned long long)v.retry_signals, (unsigned long long)v.suspend_ack_sem,
               (unsigned long long)v.stop_count, (unsigned long long)v.threads);
      else failed = 1;
    }
    free(ef.data);
  }
  return failed;
}
