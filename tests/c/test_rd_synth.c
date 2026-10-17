/* sources: source/bionic/rd_synth.c */
/* Host unit test of rd_synth. MIT (see LICENSE). */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "rd_synth.h"

static char g_path[256];
static const char CPUINFO[] = "processor\t: 0\nprocessor\t: 1\nCPU part\t: 0xd07\n";

/* What libunity's GNU libstdc++ does in basic_filebuf::underflow: read(fileno(f)). */
static long gnustl_style_read(FILE *f, char *buf, size_t n) {
  return read(fileno(f), buf, n);
}

/* A synthetic /proc file opened as FILE* has a real fd: Unity's libstdc++ reads cpuinfo through
 * read(fileno(f)), and with a fmemopen stream (fd -1) std::ifstream threw ios_base::failure in
 * InitBigLittleConfiguration. */
static void test_file_over_fd_is_readable_via_fileno(void) {
  FILE *f = rd_fopen_fd(open(g_path, O_RDONLY));
  CHECK(f != NULL);
  if (!f) return;
  CHECK(fileno(f) >= 0);
  char buf[128] = {0};
  CHECK(gnustl_style_read(f, buf, sizeof buf - 1) == (long)strlen(CPUINFO));
  CHECK(strcmp(buf, CPUINFO) == 0);
  fclose(f);
}

/* The same FILE* also works with ordinary stdio reads. */
static void test_stdio_reads_still_work(void) {
  FILE *f = rd_fopen_fd(open(g_path, O_RDONLY));
  CHECK(f != NULL);
  if (!f) return;
  char line[64];
  CHECK(fgets(line, sizeof line, f) != NULL);
  CHECK(strcmp(line, "processor\t: 0\n") == 0);
  fclose(f);
}

/* An invalid fd gives NULL instead of a FILE* that fails later. */
static void test_invalid_fd_gives_null(void) {
  CHECK(rd_fopen_fd(-1) == NULL);
}

/* Documents the original bug: a memory-backed FILE has no fd to read() from. */
/* Documents why fmemopen is not used: its streams have no fd. */
static void test_fmemopen_has_no_fd(void) {
  char data[] = "x";
  FILE *f = fmemopen(data, 1, "r");
  CHECK(f != NULL);
  if (!f) return;
  CHECK(fileno(f) == -1);
  fclose(f);
}

int main(int argc, char **argv) {
  snprintf(g_path, sizeof g_path, "%s/.synth_proc_cpuinfo", argc > 1 ? argv[1] : ".");
  FILE *w = fopen(g_path, "w");
  fputs(CPUINFO, w);
  fclose(w);

  RUN(test_file_over_fd_is_readable_via_fileno);
  RUN(test_stdio_reads_still_work);
  RUN(test_invalid_fd_gives_null);
  RUN(test_fmemopen_has_no_fd);
  return g_failures != 0;
}
