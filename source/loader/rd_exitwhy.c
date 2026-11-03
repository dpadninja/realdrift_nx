/* rd_exitwhy.c -- see rd_exitwhy.h. MIT (see LICENSE). */
#include <stdio.h>
#include "rd_exitwhy.h"

/* Linux/bionic name of signal `sig`, or NULL. */
static const char *signame(int sig) {
  switch (sig) {
    case 2:  return "SIGINT";
    case 4:  return "SIGILL";
    case 5:  return "SIGTRAP";
    case 6:  return "SIGABRT";
    case 7:  return "SIGBUS";
    case 8:  return "SIGFPE";
    case 9:  return "SIGKILL";
    case 11: return "SIGSEGV";
    case 15: return "SIGTERM";
    default: return NULL;
  }
}

void rd_exit_describe(char *buf, size_t cap, int kind, int code) {
  if (!buf || !cap) return;
  switch (kind) {
    case RD_EXIT_ABORT: snprintf(buf, cap, "abort()"); break;
    case RD_EXIT_EXIT:  snprintf(buf, cap, "exit(%d)", code); break;
    case RD_EXIT_QUICK: snprintf(buf, cap, "_exit(%d)", code); break;
    case RD_EXIT_RAISE: {
      const char *n = signame(code);
      if (n) snprintf(buf, cap, "raise(%s)", n);
      else snprintf(buf, cap, "raise(%d)", code);
      break;
    }
    default: snprintf(buf, cap, "exit kind %d (%d)", kind, code); break;
  }
}

int rd_exit_claim(int *flag) {
  return !__atomic_exchange_n(flag, 1, __ATOMIC_SEQ_CST);
}
