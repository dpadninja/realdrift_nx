/* sources: source/mods/rd_modhost.c */
/* Host unit test of rd_modhost: load order, claims, the text queue. MIT (see LICENSE). */
#include <string.h>
#include "check.h"
#include "rd_modhost.h"

/* Higher priority loads first and ties go by id, so the order (and every conflict decision
 * that follows from it) is the same on each boot. */
static void test_order(void) {
  rd_mod_info m[4] = { 0 };
  strcpy(m[0].id, "zeta"); m[0].priority = 0;
  strcpy(m[1].id, "alpha"); m[1].priority = 0;
  strcpy(m[2].id, "boss"); m[2].priority = 10;
  strcpy(m[3].id, "low"); m[3].priority = -1;
  int order[4];
  rd_mods_order(m, order, 4);
  CHECK(order[0] == 2 && order[1] == 1 && order[2] == 0 && order[3] == 3);
}

/* The first claim on a name wins and later claimers learn who holds it; a full table refuses
 * instead of overflowing. */
static void test_claims(void) {
  rd_claim c[2];
  rd_claims t = { c, 0, 2 };
  CHECK(rd_claim_take(&t, "assets/a", 0) == -1);
  CHECK(rd_claim_take(&t, "assets/a", 3) == 0);
  CHECK(rd_claim_take(&t, "assets/b", 3) == -1);
  CHECK(rd_claim_take(&t, "assets/c", 1) == -2);
  CHECK(t.n == 2);
}

/* Lines added during a frame are shown after the next swap and replaced by the swap after it
 * (text does not pile up), long text is cut, and a full queue refuses more lines. */
static void test_text_queue(void) {
  static rd_modtext q;
  CHECK(rd_modtext_add(&q, 1, 2, 3, 0xFFFFFFFFu, "HELLO") == 0);
  CHECK(q.nshown == 0);
  rd_modtext_swap(&q);
  CHECK(q.nshown == 1 && !strcmp(q.shown[0].text, "HELLO") && q.npending == 0);
  rd_modtext_swap(&q);
  CHECK(q.nshown == 0);
  char longt[200];
  memset(longt, 'x', sizeof longt - 1);
  longt[sizeof longt - 1] = 0;
  CHECK(rd_modtext_add(&q, 0, 0, 1, 0, longt) == 0);
  CHECK(strlen(q.pending[0].text) == RD_MODTEXT_LEN - 1);
  while (rd_modtext_add(&q, 0, 0, 1, 0, "x") == 0) {}
  CHECK(q.npending == RD_MODTEXT_MAX);
  CHECK(rd_modtext_add(&q, 0, 0, 1, 0, NULL) == -1);
}

int main(void) {
  RUN(test_order);
  RUN(test_claims);
  RUN(test_text_queue);
  return g_failures != 0;
}
