/* sources: source/loader/rd_exitwhy.c */
/* Host unit test of rd_exitwhy. MIT (see LICENSE). */
#include <string.h>
#include "check.h"
#include "rd_exitwhy.h"

/* Each kind reads like the call that was made, so the log line can be searched for. */
static void test_describe_calls(void) {
  char b[32];
  rd_exit_describe(b, sizeof b, RD_EXIT_ABORT, 0);  CHECK(!strcmp(b, "abort()"));
  rd_exit_describe(b, sizeof b, RD_EXIT_EXIT, 1);   CHECK(!strcmp(b, "exit(1)"));
  rd_exit_describe(b, sizeof b, RD_EXIT_QUICK, 0);  CHECK(!strcmp(b, "_exit(0)"));
}

/* raise() names Linux signals (the engine's numbering: SIGBUS is 7 there, 10 in newlib) and
 * prints the number of one it does not know. */
static void test_describe_raise(void) {
  char b[32];
  rd_exit_describe(b, sizeof b, RD_EXIT_RAISE, 6);  CHECK(!strcmp(b, "raise(SIGABRT)"));
  rd_exit_describe(b, sizeof b, RD_EXIT_RAISE, 7);  CHECK(!strcmp(b, "raise(SIGBUS)"));
  rd_exit_describe(b, sizeof b, RD_EXIT_RAISE, 42); CHECK(!strcmp(b, "raise(42)"));
}

/* A short buffer is truncated, not overrun. */
static void test_describe_truncates(void) {
  char b[6];
  rd_exit_describe(b, sizeof b, RD_EXIT_RAISE, 6);
  CHECK(!strcmp(b, "raise"));
}

/* Only the first claim wins: abort() goes on to _exit(), which must not log a second,
 * misleading reason. */
static void test_claim_once(void) {
  int flag = 0;
  CHECK(rd_exit_claim(&flag));
  CHECK(!rd_exit_claim(&flag));
  CHECK(!rd_exit_claim(&flag));
}

int main(void) {
  RUN(test_describe_calls);
  RUN(test_describe_raise);
  RUN(test_describe_truncates);
  RUN(test_claim_once);
  return g_failures != 0;
}
