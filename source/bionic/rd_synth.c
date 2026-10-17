/* rd_synth.c -- see rd_synth.h. MIT (see LICENSE). */
#include <stdio.h>
#include <unistd.h>
#include "rd_synth.h"

FILE *rd_fopen_fd(int fd) {
  if (fd < 0) return NULL;
  FILE *f = fdopen(fd, "r");
  if (!f) close(fd);
  return f;
}
