/* rd_modimage.c -- see rd_modimage.h. MIT (see LICENSE). */
#include "rd_modimage.h"

static const char *module_cstr(void *ctx, uint64_t va) {
  const so_module *m = ctx;
  if (va >= m->load_size) return NULL;
  return (const char *)m->load_virtbase + va;
}

int rd_image_from_module(const so_module *m, rd_image *im) {
  if (!m || !m->load_virtbase) return -1;
  for (int i = 0; i < m->phnum; i++) {
    const Elf64_Phdr *p = &m->phdr[i];
    if (p->p_type != PT_LOAD || !(p->p_flags & PF_X)) continue;
    if (p->p_vaddr + p->p_filesz > m->load_size) return -1;
    im->code   = (const uint32_t *)((const char *)m->load_virtbase + p->p_vaddr);
    im->vaddr  = p->p_vaddr;
    im->nwords = p->p_filesz / 4;
    im->cstr   = module_cstr;
    im->ctx    = (void *)m;
    return 0;
  }
  return -1;
}
