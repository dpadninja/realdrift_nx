/* sources: source/bionic/rd_archcache.c */
/* Host unit test of rd_archcache (archive_cache: the block cache of base.apk / the OBB).
 * MIT (see LICENSE). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "rd_archcache.h"

#define BLOCK 16
#define SECTOR 4
#define SIZE  100                 /* not a multiple of BLOCK: the last block is short */

/* A fake file: byte i is (i * 7 + key) & 0xff; counts the fill calls and can fail. */
typedef struct {
  uint64_t key;
  int64_t size;
  int fills;
  int fail;
  int64_t last_off;               /* the last fill's request */
  size_t last_n;
} fake_file;

static uint8_t byte_at(uint64_t key, int64_t i) { return (uint8_t)((i * 7 + (int64_t)key) & 0xff); }

static long fake_fill(void *ctx, int64_t off, void *buf, size_t n) {
  fake_file *f = ctx;
  f->fills++;
  f->last_off = off;
  f->last_n = n;
  if (f->fail) return -1;
  size_t k = 0;
  for (; k < n && off + (int64_t)k < f->size; k++) ((uint8_t *)buf)[k] = byte_at(f->key, off + (int64_t)k);
  return (long)k;
}

static void free_blocks(rd_archcache_block *b, int n) {
  for (int i = 0; i < n; i++) free(b[i].data);
}

/* 1 if buf holds bytes [off, off+n) of the fake file `key`. */
static int same(const uint8_t *buf, uint64_t key, int64_t off, size_t n) {
  for (size_t i = 0; i < n; i++) if (buf[i] != byte_at(key, off + (int64_t)i)) return 0;
  return 1;
}

/* Every (offset, length) pair, including reads across blocks and over the end, returns the
 * file's bytes and clamps at the end. */
static void test_reads_match_file(void) {
  rd_archcache_block blocks[3];
  rd_archcache c;
  rd_archcache_init(&c, blocks, 3, BLOCK, SECTOR);
  fake_file f = { .key = 5, .size = SIZE };
  uint8_t buf[SIZE + 10];
  for (int64_t off = 0; off <= SIZE; off++)
    for (size_t n = 0; n <= 40; n++) {
      const long r = rd_archcache_read(&c, f.key, f.size, off, buf, n, 1, fake_fill, &f);
      const long want = off + (int64_t)n > SIZE ? (long)(SIZE - off) : (long)n;
      CHECK(r == want);
      CHECK(same(buf, f.key, off, (size_t)r));
    }
  free_blocks(blocks, 3);
}

/* Small reads inside one block read it from the file once; the rest are hits. */
static void test_small_reads_hit(void) {
  rd_archcache_block blocks[4];
  rd_archcache c;
  rd_archcache_init(&c, blocks, 4, BLOCK, SECTOR);
  fake_file f = { .key = 1, .size = SIZE };
  uint8_t buf[4];
  for (int i = 0; i < 4; i++) CHECK(rd_archcache_read(&c, 1, SIZE, 20 + i * 2, buf, 2, 1, fake_fill, &f) == 2);
  CHECK(f.fills == 1 && c.misses == 1 && c.hits == 3);
  free_blocks(blocks, 4);
}

/* With the pool full, the least recently used block is the one replaced. */
static void test_lru_eviction(void) {
  rd_archcache_block blocks[2];
  rd_archcache c;
  rd_archcache_init(&c, blocks, 2, BLOCK, SECTOR);
  fake_file f = { .key = 2, .size = SIZE };
  uint8_t b;
  rd_archcache_read(&c, 2, SIZE, 0, &b, 1, 1, fake_fill, &f);        /* block 0 */
  rd_archcache_read(&c, 2, SIZE, BLOCK, &b, 1, 1, fake_fill, &f);    /* block 1 */
  rd_archcache_read(&c, 2, SIZE, 0, &b, 1, 1, fake_fill, &f);        /* block 0 again: newer */
  rd_archcache_read(&c, 2, SIZE, 2 * BLOCK, &b, 1, 1, fake_fill, &f);/* block 2 replaces 1 */
  CHECK(f.fills == 3);
  rd_archcache_read(&c, 2, SIZE, 0, &b, 1, 1, fake_fill, &f);        /* still cached */
  CHECK(f.fills == 3);
  rd_archcache_read(&c, 2, SIZE, BLOCK, &b, 1, 1, fake_fill, &f);    /* was evicted */
  CHECK(f.fills == 4);
  CHECK(b == byte_at(2, BLOCK));
  free_blocks(blocks, 2);
}

/* Two files at the same offsets do not share blocks. */
static void test_files_apart(void) {
  rd_archcache_block blocks[4];
  rd_archcache c;
  rd_archcache_init(&c, blocks, 4, BLOCK, SECTOR);
  fake_file a = { .key = 3, .size = SIZE }, z = { .key = 9, .size = SIZE };
  uint8_t ba[8], bz[8];
  rd_archcache_read(&c, a.key, SIZE, 2, ba, 8, 1, fake_fill, &a);
  rd_archcache_read(&c, z.key, SIZE, 2, bz, 8, 1, fake_fill, &z);
  CHECK(same(ba, a.key, 2, 8) && same(bz, z.key, 2, 8));
  CHECK(a.fills == 1 && z.fills == 1);
  free_blocks(blocks, 4);
}

/* A failing file read returns -1 and caches nothing; past the end and a negative offset are
 * 0 and -1 without reading. */
static void test_errors(void) {
  rd_archcache_block blocks[2];
  rd_archcache c;
  rd_archcache_init(&c, blocks, 2, BLOCK, SECTOR);
  fake_file f = { .key = 4, .size = SIZE, .fail = 1 };
  uint8_t buf[8];
  CHECK(rd_archcache_read(&c, 4, SIZE, 0, buf, 8, 1, fake_fill, &f) == -1);
  f.fail = 0;
  CHECK(rd_archcache_read(&c, 4, SIZE, 0, buf, 8, 1, fake_fill, &f) == 8 && same(buf, 4, 0, 8));
  CHECK(f.fills == 2);
  CHECK(rd_archcache_read(&c, 4, SIZE, SIZE, buf, 8, 1, fake_fill, &f) == 0);
  CHECK(rd_archcache_read(&c, 4, SIZE, -1, buf, 8, 1, fake_fill, &f) == -1);
  CHECK(f.fills == 2);
  free_blocks(blocks, 2);
}

/* A file shorter than the size it was opened with stops at its real end. */
static void test_file_shrank(void) {
  rd_archcache_block blocks[2];
  rd_archcache c;
  rd_archcache_init(&c, blocks, 2, BLOCK, SECTOR);
  fake_file f = { .key = 6, .size = 20 };
  uint8_t buf[40];
  CHECK(rd_archcache_read(&c, 6, 40, 0, buf, 40, 1, fake_fill, &f) == 20);
  CHECK(same(buf, 6, 0, 20));
  free_blocks(blocks, 2);
}

/* Without `ahead` only the sectors a read needs come from the file, and a later read fills
 * just its own missing sectors between valid ones; with it, the rest of the block comes too. */
static void test_sectors(void) {
  rd_archcache_block blocks[2];
  rd_archcache c;
  rd_archcache_init(&c, blocks, 2, BLOCK, SECTOR);
  fake_file f = { .key = 7, .size = SIZE };
  uint8_t buf[BLOCK];
  CHECK(rd_archcache_read(&c, 7, SIZE, 5, buf, 1, 0, fake_fill, &f) == 1);
  CHECK(f.last_off == 4 && f.last_n == SECTOR);                     /* sector 1 */
  CHECK(rd_archcache_read(&c, 7, SIZE, 13, buf, 2, 0, fake_fill, &f) == 2);
  CHECK(f.last_off == 12 && f.last_n == SECTOR);                    /* sector 3 */
  CHECK(rd_archcache_read(&c, 7, SIZE, 4, buf, 12, 0, fake_fill, &f) == 12 && same(buf, 7, 4, 12));
  CHECK(f.fills == 3 && f.last_off == 8 && f.last_n == SECTOR);     /* only sector 2 */
  CHECK(rd_archcache_read(&c, 7, SIZE, 6, buf, 9, 0, fake_fill, &f) == 9);
  CHECK(f.fills == 3);                                              /* all valid: a hit */
  CHECK(rd_archcache_read(&c, 7, SIZE, 0, buf, 1, 1, fake_fill, &f) == 1);
  CHECK(f.last_off == 0 && f.last_n == BLOCK);                      /* ahead: to the end */
  CHECK(rd_archcache_read(&c, 7, SIZE, BLOCK + 5, buf, 1, 1, fake_fill, &f) == 1);
  CHECK(f.last_off == BLOCK + 4 && f.last_n == BLOCK - 4);
  CHECK(rd_archcache_read(&c, 7, SIZE, 97, buf, 3, 1, fake_fill, &f) == 3);
  CHECK(f.last_off == 96 && f.last_n == 4 && same(buf, 7, 97, 3));  /* the file's last block */
  free_blocks(blocks, 2);
}

/* Archives are recognised by extension in any case; anything else is not one. */
static void test_is_archive(void) {
  CHECK(rd_archcache_is_archive("/switch/realdrift_nx/base.apk"));
  CHECK(rd_archcache_is_archive("/switch/realdrift_nx/main.76.com.realdrift.sipon.OBB"));
  CHECK(!rd_archcache_is_archive("/switch/realdrift_nx/prefs.kv"));
  CHECK(!rd_archcache_is_archive("/switch/realdrift_nx/obb"));
  CHECK(!rd_archcache_is_archive("/switch/x.apk.bak"));
  CHECK(!rd_archcache_is_archive(NULL));
}

/* lseek from each origin moves the position and returns it; a negative result or an unknown
 * origin fails and leaves the position where it was. */
static void test_seek(void) {
  rd_archfd fds[8];
  rd_archfiles t;
  rd_archfiles_init(&t, fds, 8);
  CHECK(rd_archfiles_attach(&t, 3, 42, 1000));
  CHECK(rd_archfiles_seek(&t, 3, 100, SEEK_SET) == 100);
  CHECK(rd_archfiles_seek(&t, 3, 50, SEEK_CUR) == 150);
  CHECK(rd_archfiles_seek(&t, 3, -10, SEEK_END) == 990);
  CHECK(rd_archfiles_seek(&t, 3, 0, SEEK_CUR) == 990);
  CHECK(rd_archfiles_seek(&t, 3, -1000, SEEK_CUR) == -1 && rd_archfiles_pos(&t, 3) == 990);
  CHECK(rd_archfiles_seek(&t, 3, 0, 7) == -1 && rd_archfiles_pos(&t, 3) == 990);
  CHECK(rd_archfiles_seek(&t, 3, 2000, SEEK_SET) == 2000);   /* past the end is allowed */
  CHECK(rd_archfiles_seek(&t, 4, 0, SEEK_SET) == -1);        /* not an archive fd */
}

/* A read advances its fd's position only; each fd of the same file keeps its own. */
static void test_positions(void) {
  rd_archfd fds[8];
  rd_archfiles t;
  rd_archfiles_init(&t, fds, 8);
  rd_archfiles_attach(&t, 1, 42, 1000);
  rd_archfiles_attach(&t, 2, 42, 1000);
  rd_archfiles_advance(&t, 1, 300);
  rd_archfiles_advance(&t, 1, 0);
  rd_archfiles_advance(&t, 1, -5);                           /* a failed read: no move */
  CHECK(rd_archfiles_pos(&t, 1) == 300 && rd_archfiles_pos(&t, 2) == 0);
  CHECK(rd_archfiles_file(&t, 1) == rd_archfiles_file(&t, 2));
  CHECK(rd_archfiles_file(&t, 1)->key == 42 && rd_archfiles_file(&t, 1)->size == 1000);
  rd_archfiles_advance(&t, 5, 10);                           /* not ours: ignored */
  CHECK(rd_archfiles_pos(&t, 5) == 0);
}

/* After close the fd number is plain again; reopened on another archive it starts at 0 on
 * that file, and an archive opened again with a new size takes it. */
static void test_detach_reuse(void) {
  rd_archfd fds[8];
  rd_archfiles t;
  rd_archfiles_init(&t, fds, 8);
  rd_archfiles_attach(&t, 6, 42, 1000);
  rd_archfiles_advance(&t, 6, 77);
  rd_archfiles_detach(&t, 6);
  CHECK(rd_archfiles_file(&t, 6) == NULL && rd_archfiles_pos(&t, 6) == 0);
  rd_archfiles_attach(&t, 6, 43, 500);
  CHECK(rd_archfiles_file(&t, 6)->key == 43 && rd_archfiles_pos(&t, 6) == 0);
  rd_archfiles_attach(&t, 7, 42, 1200);
  CHECK(rd_archfiles_file(&t, 7)->size == 1200 && t.nfiles == 2);
  rd_archfiles_detach(&t, -1);                               /* out of range: no-op */
  rd_archfiles_detach(&t, 8);
}

/* With every file slot taken a new archive is not attached (its fd reads the card directly),
 * fds out of range never are, and known files still attach. */
static void test_table_full(void) {
  rd_archfd fds[16];
  rd_archfiles t;
  rd_archfiles_init(&t, fds, 16);
  for (int i = 0; i < RD_ARCHFILES_FILES; i++) CHECK(rd_archfiles_attach(&t, i, 100 + (uint64_t)i, 10));
  CHECK(!rd_archfiles_attach(&t, 12, 999, 10) && rd_archfiles_file(&t, 12) == NULL);
  CHECK(rd_archfiles_attach(&t, 13, 100, 10));
  CHECK(!rd_archfiles_attach(&t, 16, 100, 10) && !rd_archfiles_attach(&t, -1, 100, 10));
  CHECK(rd_archfiles_file(&t, -1) == NULL && rd_archfiles_file(&t, 16) == NULL);
}

/* Only a read right after the previous one, with no seek between, counts as sequential; the
 * first read and one after any seek do not, and reopening starts over. */
static void test_sequential(void) {
  rd_archfd fds[4];
  rd_archfiles t;
  rd_archfiles_init(&t, fds, 4);
  rd_archfiles_attach(&t, 1, 42, 1000);
  CHECK(!rd_archfiles_sequential(&t, 1));                    /* first read */
  rd_archfiles_advance(&t, 1, 100);
  CHECK(rd_archfiles_sequential(&t, 1));
  rd_archfiles_seek(&t, 1, 500, SEEK_SET);
  CHECK(!rd_archfiles_sequential(&t, 1));                    /* jumped */
  rd_archfiles_advance(&t, 1, 10);
  CHECK(rd_archfiles_sequential(&t, 1));
  rd_archfiles_seek(&t, 1, 0, SEEK_CUR);                     /* a tell(): still sequential */
  CHECK(rd_archfiles_sequential(&t, 1));
  rd_archfiles_attach(&t, 1, 42, 1000);
  CHECK(!rd_archfiles_sequential(&t, 1));
  CHECK(!rd_archfiles_sequential(&t, 2));                    /* not an archive fd */
}

int main(void) {
  RUN(test_reads_match_file);
  RUN(test_small_reads_hit);
  RUN(test_lru_eviction);
  RUN(test_files_apart);
  RUN(test_errors);
  RUN(test_file_shrank);
  RUN(test_sectors);
  RUN(test_is_archive);
  RUN(test_seek);
  RUN(test_positions);
  RUN(test_detach_reuse);
  RUN(test_table_full);
  RUN(test_sequential);
  return g_failures != 0;
}
