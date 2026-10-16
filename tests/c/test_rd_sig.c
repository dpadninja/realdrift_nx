/* sources: source/rd_sig.c */
#include "check.h"
#include "rd_sig.h"

static const rd_sigw PAT[] = {
  RD_SIG_X(0x7100401f),          /* cmp w0, #0x10 */
  RD_SIG_BCOND(RD_A64_COND_LT),      /* b.lt <any> */
  RD_SIG_ADRP(0),                /* adrp x0, <any> */
  RD_SIG_ADD(0, 0),              /* add x0, x0, #<any> */
};

static void test_masks_ignore_immediates(void) {
  const uint32_t code[] = { 0xd503201f, 0x7100401f, 0x5400032b, 0xf0004540, 0x91124000, 0xd503201f };
  size_t at = 99;
  CHECK(rd_sig_scan(code, 6, PAT, 4, &at) == 1 && at == 1);
  const uint32_t other_imm[] = { 0x7100401f, 0x540004ab, 0xb0004560, 0x91034000 };  /* 5.0.8-style */
  CHECK(rd_sig_scan(other_imm, 4, PAT, 4, &at) == 1 && at == 0);
}

static void test_masks_keep_opcode_and_registers(void) {
  size_t at;
  const uint32_t wrong_cond[] = { 0x7100401f, 0x5400032a, 0xf0004540, 0x91124000 };   /* b.ge */
  CHECK(rd_sig_scan(wrong_cond, 4, PAT, 4, &at) == 0);
  const uint32_t wrong_reg[] = { 0x7100401f, 0x5400032b, 0xf0004541, 0x91124000 };    /* adrp x1 */
  CHECK(rd_sig_scan(wrong_reg, 4, PAT, 4, &at) == 0);
}

static void test_counts_duplicates_and_bounds(void) {
  const uint32_t twice[] = { 0x7100401f, 0x5400032b, 0xf0004540, 0x91124000,
                             0x7100401f, 0x5400032b, 0xf0004540, 0x91124000 };
  size_t at = 99;
  CHECK(rd_sig_scan(twice, 8, PAT, 4, &at) == 2 && at == 0);
  CHECK(rd_sig_scan(twice, 3, PAT, 4, &at) == 0);              /* pattern longer than code */
  CHECK(rd_sig_scan(twice, 7, PAT, 4, &at) == 1);              /* second copy cut off */
  CHECK(rd_sig_scan(NULL, 8, PAT, 4, &at) == 0);
}

int main(void) {
  RUN(test_masks_ignore_immediates);
  RUN(test_masks_keep_opcode_and_registers);
  RUN(test_counts_duplicates_and_bounds);
  return g_failures != 0;
}
