/* rd_synth.h -- stdio access to synthetic /proc and /sys files. Host-testable
 * (tests/c/test_rd_synth.c).
 * MIT (see LICENSE). */
#ifndef RD_SYNTH_H
#define RD_SYNTH_H

#include <stdio.h>

/* Wrap a read-only fd of a materialized synthetic file in a FILE*.
 *
 * Unity's statically linked GNU libstdc++ opens files with fopen() and then reads
 * them with read(fileno(f)). A memory-backed FILE (fmemopen) has fileno() == -1,
 * so std::ifstream("/proc/cpuinfo") threw ios_base::failure ("basic_filebuf::
 * underflow error reading the file") in InitBigLittleConfiguration(). A FILE over
 * a real fd works for both stdio and fileno()+read() readers.
 *
 * Returns NULL if fd < 0; closes fd if fdopen fails. */
FILE *rd_fopen_fd(int fd);

#endif
