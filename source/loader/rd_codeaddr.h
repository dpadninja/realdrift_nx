/* rd_codeaddr.h -- is a stack word a plausible return address into a loaded module?
 * Host-testable (tests/c/test_rd_codeaddr.c).
 * MIT (see LICENSE). */
#ifndef RD_CODEADDR_H
#define RD_CODEADDR_H

#include <stdint.h>
#include <elf.h>

/* 1 if `v` (runtime address) is 4-byte aligned and the instruction slot [v-4, v) lies
 * entirely inside an executable PT_LOAD segment of a module loaded at `virtbase` whose
 * link-time program headers are `ph[0..n)`. Only then is it safe to read *(v-4).
 *
 * The inherited watchdog stack scan read *(v-4) for any v within [base, base+load_size):
 * v == base faulted on base-4 (run 05: far = libil2cpp base - 4), and v in a gap
 * between segments or in RW data is not a code address at all. */
int rd_is_code_return_addr(uint64_t v, uint64_t virtbase, const Elf64_Phdr *ph, int n);

#endif
