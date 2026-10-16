/* rd_sig.c -- see rd_sig.h. MIT (see LICENSE). */
#include "rd_sig.h"

int rd_sig_scan(const uint32_t *code, size_t nwords, const rd_sigw *pat, size_t plen,
                size_t *first) {
  if (!code || !pat || !plen || plen > nwords) return 0;
  int hits = 0;
  for (size_t i = 0; i + plen <= nwords; i++) {
    if ((code[i] & pat[0].mask) != pat[0].val) continue;
    size_t k = 1;
    while (k < plen && (code[i + k] & pat[k].mask) == pat[k].val) k++;
    if (k < plen) continue;
    if (hits == 0 && first) *first = i;
    if (++hits == 2) break;
  }
  return hits;
}
