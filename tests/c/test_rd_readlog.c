/* sources: source/bionic/rd_readlog.c */
/* Host unit test of rd_readlog (the launch prefetch of small archive reads). MIT (see LICENSE). */
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "rd_readlog.h"

#define APK 0x1111u
#define OBB 0x2222u

/* Byte i of a fake file: depends on key and offset, so a wrong range shows. */
static uint8_t byte_of(uint64_t key, int64_t i) { return (uint8_t)((i * 13 + (int64_t)key) & 0xff); }

static void fill(uint8_t *buf, uint64_t key, int64_t off, size_t n) {
  for (size_t i = 0; i < n; i++) buf[i] = byte_of(key, off + (int64_t)i);
}

static int same(const uint8_t *p, uint64_t key, int64_t off, size_t n) {
  if (!p) return 0;
  for (size_t i = 0; i < n; i++) if (p[i] != byte_of(key, off + (int64_t)i)) return 0;
  return 1;
}

static rd_readlog_range g_ranges[64];
static uint8_t g_data[4096];

/* Records a read of the fake file. */
static void add(rd_readlog *r, uint64_t key, int64_t off, size_t n) {
  uint8_t buf[512];
  fill(buf, key, off, n);
  CHECK(rd_readlog_add(r, key, off, buf, n));
}

/* The log of r under `stamp`, malloc'd; *len its size. */
static uint8_t *saved(rd_readlog *r, const char *stamp, size_t *len) {
  *len = rd_readlog_serialize(r, stamp, NULL, 0);
  uint8_t *out = malloc(*len);
  CHECK(rd_readlog_serialize(r, stamp, out, *len) == *len);
  return out;
}

/* Reads recorded out of order in two files come back from the saved log, also any part of one;
 * a read that is not wholly inside a recorded one, or of another file, is not served. */
static void test_round_trip(void) {
  rd_readlog r;
  rd_readlog_init(&r, g_ranges, 64, g_data, sizeof g_data);
  add(&r, OBB, 5000, 216);
  add(&r, APK, 100, 30);
  add(&r, OBB, 100, 216);
  add(&r, APK, 9000, 64);
  size_t len;
  uint8_t *file = saved(&r, "s1", &len);
  rd_readlog_view v;
  CHECK(rd_readlog_parse(&v, file, len, "s1") && v.n == 4);
  CHECK(same(rd_readlog_find(&v, OBB, 5000, 216), OBB, 5000, 216));
  CHECK(same(rd_readlog_find(&v, OBB, 5010, 20), OBB, 5010, 20));
  CHECK(same(rd_readlog_find(&v, APK, 100, 30), APK, 100, 30));
  CHECK(same(rd_readlog_find(&v, APK, 9000, 64), APK, 9000, 64));
  CHECK(rd_readlog_find(&v, OBB, 5100, 200) == NULL);      /* runs past the recorded end */
  CHECK(rd_readlog_find(&v, OBB, 99, 10) == NULL);         /* starts before */
  CHECK(rd_readlog_find(&v, APK, 5000, 10) == NULL);       /* other file */
  CHECK(rd_readlog_find(&v, OBB, 50, 1) == NULL);          /* before the first range */
  CHECK(rd_readlog_find(&v, 0x9999u, 0, 1) == NULL);
  free(file);
}

/* Overlapping and touching reads of a file become one range, so a read across them is served;
 * a gap keeps them apart, and a repeated read is stored once. */
static void test_merge(void) {
  rd_readlog r;
  rd_readlog_init(&r, g_ranges, 64, g_data, sizeof g_data);
  add(&r, OBB, 0, 100);
  add(&r, OBB, 50, 100);          /* overlaps */
  add(&r, OBB, 150, 50);          /* touches */
  add(&r, OBB, 10, 20);           /* inside */
  add(&r, OBB, 0, 100);           /* repeated */
  add(&r, OBB, 300, 10);          /* after a gap */
  size_t len;
  uint8_t *file = saved(&r, "s", &len);
  rd_readlog_view v;
  CHECK(rd_readlog_parse(&v, file, len, "s") && v.n == 2);
  CHECK(same(rd_readlog_find(&v, OBB, 0, 200), OBB, 0, 200));
  CHECK(same(rd_readlog_find(&v, OBB, 120, 60), OBB, 120, 60));
  CHECK(rd_readlog_find(&v, OBB, 190, 20) == NULL);        /* into the gap */
  CHECK(same(rd_readlog_find(&v, OBB, 300, 10), OBB, 300, 10));
  free(file);
}

/* A full recorder refuses more reads (they are just not prefetched next time); empty and
 * negative-offset reads are ignored. */
static void test_limits(void) {
  rd_readlog_range few[2];
  uint8_t small[100];
  rd_readlog r;
  rd_readlog_init(&r, few, 2, small, sizeof small);
  uint8_t buf[80] = { 0 };
  CHECK(rd_readlog_add(&r, APK, 0, buf, 60));
  CHECK(!rd_readlog_add(&r, APK, 100, buf, 50));           /* data full */
  CHECK(rd_readlog_add(&r, APK, 100, buf, 40));
  CHECK(!rd_readlog_add(&r, APK, 200, buf, 1));            /* ranges full */
  CHECK(rd_readlog_add(&r, APK, 300, buf, 0) && rd_readlog_add(&r, APK, -1, buf, 4));
  CHECK(r.n == 2 && r.used == 100);
  CHECK(rd_readlog_serialize(&r, "s", small, 10) == 0);    /* output too small */
}

/* A log is used only for the archives it was made from, and a damaged or foreign file is
 * refused instead of serving wrong bytes. */
static void test_parse_rejects(void) {
  rd_readlog r;
  rd_readlog_init(&r, g_ranges, 64, g_data, sizeof g_data);
  add(&r, OBB, 100, 216);
  char stamp[RD_READLOG_STAMP_MAX], other[RD_READLOG_STAMP_MAX];
  CHECK(rd_readlog_stamp(stamp, sizeof stamp, 53545581, 1700000000, 230223716, 1616800000) > 0);
  rd_readlog_stamp(other, sizeof other, 53545581, 1700000001, 230223716, 1616800000);
  CHECK(strcmp(stamp, other) != 0);
  CHECK(rd_readlog_stamp(other, 8, 1, 2, 3, 4) == -1);
  size_t len;
  uint8_t *file = saved(&r, stamp, &len);
  rd_readlog_view v;
  CHECK(rd_readlog_parse(&v, file, len, stamp));
  CHECK(!rd_readlog_parse(&v, file, len, "base.apk 1 2; obb 3 4"));   /* other archives */
  CHECK(!rd_readlog_parse(&v, file, len - 1, stamp));      /* cut short */
  CHECK(!rd_readlog_parse(&v, file, 8, stamp));
  CHECK(!rd_readlog_parse(&v, NULL, 0, stamp));
  file[0] = 'X';
  CHECK(!rd_readlog_parse(&v, file, len, stamp));          /* not a log */
  file[0] = 'R';
  file[4] = 9;
  CHECK(!rd_readlog_parse(&v, file, len, stamp));          /* other version */
  file[4] = 1;
  const size_t head = len - 216 - 24;                      /* the entry: data offset beyond */
  file[head + 20] = 200;
  CHECK(!rd_readlog_parse(&v, file, len, stamp));
  free(file);
}

/* An empty recording makes a valid log that serves nothing. */
static void test_empty(void) {
  rd_readlog r;
  rd_readlog_init(&r, g_ranges, 64, g_data, sizeof g_data);
  size_t len;
  uint8_t *file = saved(&r, "s", &len);
  rd_readlog_view v;
  CHECK(rd_readlog_parse(&v, file, len, "s") && v.n == 0);
  CHECK(rd_readlog_find(&v, OBB, 0, 1) == NULL);
  free(file);
}

int main(void) {
  RUN(test_round_trip);
  RUN(test_merge);
  RUN(test_limits);
  RUN(test_parse_rejects);
  RUN(test_empty);
  return g_failures != 0;
}
