/* rd_codeaddr.c -- see rd_codeaddr.h. MIT (see LICENSE). */
#include "rd_codeaddr.h"

int rd_is_code_return_addr(uint64_t v, uint64_t virtbase, const Elf64_Phdr *ph, int n) {
  if ((v & 3) || v < virtbase + 4 || !ph) return 0;
  uint64_t rva = v - virtbase;   /* link-time address of the return site */
  for (int i = 0; i < n; i++) {
    if (ph[i].p_type != PT_LOAD || !(ph[i].p_flags & PF_X)) continue;
    uint64_t lo = ph[i].p_vaddr, hi = ph[i].p_vaddr + ph[i].p_memsz;
    if (rva - 4 >= lo && rva <= hi) return 1;
  }
  return 0;
}
