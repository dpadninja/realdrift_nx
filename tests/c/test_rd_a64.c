/* sources: source/rd_a64.c */
#include <stdint.h>
#include "check.h"
#include "rd_a64.h"

/* Encodings below are taken from llvm-objdump of analysis/lib64/libunity.so. */

static void test_bcond_target_of_real_instruction(void) {
  /* 30f838: 5400032b  b.lt 0x30f89c */
  CHECK(rd_a64_branch_target(0x30f838, 0x5400032bu) == 0x30f89c);
}

static void test_b_forward_and_backward(void) {
  CHECK(rd_a64_b(0x30f838, 0x30f89c) == 0x14000019u);
  CHECK(rd_a64_branch_target(0x30f838, 0x14000019u) == 0x30f89c);
  uint32_t back = rd_a64_b(0x1000, 0x0ff0);
  CHECK(back == 0x17fffffcu);
  CHECK(rd_a64_branch_target(0x1000, back) == 0x0ff0);
}

static void test_range_and_alignment_errors(void) {
  CHECK(rd_a64_b(0x1000, 0x1002) == 0);
  CHECK(rd_a64_b(0, 1ULL << 27) == 0);
  CHECK(rd_a64_b(0, (1ULL << 27) - 4) != 0);
}

static void test_branch_target_rejects_other_insns(void) {
  CHECK(rd_a64_branch_target(0x30f834, 0x7100401fu) == 0);   /* cmp w0, #0x10 */
  CHECK(rd_a64_branch_target(0x0, 0xd65f03c0u) == 0);        /* ret */
  CHECK(rd_a64_branch_target(0x75b318, 0x9400000bu) == 0x75b344);   /* bl GC_suspend_handler_inner */
}

/* Real libil2cpp.so instructions (llvm-objdump), used by rd_gcbridge. */
static void test_adrp_ldr_add_decode(void) {
  unsigned rd = 99, rn = 99; uint32_t off = 0, imm = 0;
  CHECK(rd_a64_adrp_target(0x75b4d4, 0xf000bdd8u, &rd) == 0x1f16000 && rd == 24);
  CHECK(rd_a64_adrp_target(0x75b6ec, 0xf000c074u, &rd) == 0x1f6a000 && rd == 20);
  CHECK(rd_a64_adrp_target(0x753824, 0xf000c013u, &rd) == 0x1f56000 && rd == 19);
  CHECK(rd_a64_adrp_target(0x1000, 0x2a1f03f5u, &rd) == 0);            /* mov w21, wzr */
  CHECK(rd_a64_ldr_w_uimm(0xb94a9b01u, &rn, &off) && rn == 24 && off == 0xa98);
  CHECK(rd_a64_ldr_w_uimm(0xb94a9f01u, &rn, &off) && rn == 24 && off == 0xa9c);
  CHECK(rd_a64_ldr_w_uimm(0xb9490a68u, &rn, &off) && rn == 19 && off == 0x908);
  CHECK(!rd_a64_ldr_w_uimm(0x91308294u, &rn, &off));
  CHECK(rd_a64_add_x_imm(0x91308294u, &rd, &rn, &imm) && rd == 20 && rn == 20 && imm == 0xc20);
  CHECK(!rd_a64_add_x_imm(0xb94a9b01u, &rd, &rn, &imm));
}

static void test_encodings(void) {
  CHECK(rd_a64_is_bl(0x94000123u) && rd_a64_is_bl(0x97ffffffu));
  CHECK(!rd_a64_is_bl(0x14000001u) && !rd_a64_is_bl(RD_A64_RET));
}

int main(void) {
  RUN(test_encodings);
  RUN(test_adrp_ldr_add_decode);
  RUN(test_bcond_target_of_real_instruction);
  RUN(test_b_forward_and_backward);
  RUN(test_range_and_alignment_errors);
  RUN(test_branch_target_rejects_other_insns);
  return g_failures != 0;
}
