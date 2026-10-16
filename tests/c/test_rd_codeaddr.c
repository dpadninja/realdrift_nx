/* sources: source/rd_codeaddr.c */
#include <elf.h>
#include "check.h"
#include "rd_codeaddr.h"

/* Program headers of analysis/lib64/libil2cpp.so (llvm-readelf -lW). */
static const Elf64_Phdr PH[] = {
  { .p_type = PT_LOAD, .p_flags = PF_R | PF_X, .p_vaddr = 0x0,       .p_memsz = 0x1c72f54 },
  { .p_type = PT_LOAD, .p_flags = PF_R | PF_W, .p_vaddr = 0x1c83000, .p_memsz = 0x4fa130 },
  { .p_type = PT_DYNAMIC, .p_flags = PF_R | PF_W, .p_vaddr = 0x1c90000, .p_memsz = 0x200 },
};
#define BASE 0x2ab516000ull
#define N ((int)(sizeof PH / sizeof PH[0]))

static void test_module_base_is_rejected(void) {
  /* run 05 crash: v == base -> read of base-4 */
  CHECK(!rd_is_code_return_addr(BASE, BASE, PH, N));
}

static void test_text_addresses_accepted(void) {
  CHECK(rd_is_code_return_addr(BASE + 4, BASE, PH, N));
  CHECK(rd_is_code_return_addr(BASE + 0x73ecfc, BASE, PH, N));
  CHECK(rd_is_code_return_addr(BASE + 0x1c72f54, BASE, PH, N));   /* slot ends at segment end */
}

static void test_data_gap_and_misaligned_rejected(void) {
  CHECK(!rd_is_code_return_addr(BASE + 0x1c72f58, BASE, PH, N));  /* gap after text */
  CHECK(!rd_is_code_return_addr(BASE + 0x1c90010, BASE, PH, N));  /* RW data */
  CHECK(!rd_is_code_return_addr(BASE + 0x73ecfe, BASE, PH, N));   /* misaligned */
  CHECK(!rd_is_code_return_addr(BASE - 8, BASE, PH, N));          /* below module */
  CHECK(!rd_is_code_return_addr(0x10, BASE, PH, N));
  CHECK(!rd_is_code_return_addr(BASE + 8, BASE, NULL, 0));
}

int main(void) {
  RUN(test_module_base_is_rejected);
  RUN(test_text_addresses_accepted);
  RUN(test_data_gap_and_misaligned_rejected);
  return g_failures != 0;
}
