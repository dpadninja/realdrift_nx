/* sources: source/bionic/rd_zipview.c source/android/rd_zip.c */
/* Host unit test of rd_zipview: the archive test_c_units.py writes (argv[1]/test.zip) gets one
 * entry replaced and one added; the view is written out and read back with rd_zip, the reader
 * the port's AssetManager uses. MIT (see LICENSE). */
/* libs: -lz */
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "check.h"
#include "rd_zip.h"
#include "rd_zipfmt.h"
#include "rd_zipview.h"

static uint8_t *g_orig;
static long g_orig_size;
static const char *SRC[] = { "the replaced boot.config\n", "a brand new file", "dup" };

/* Read callback: the original from memory, mod files from SRC. */
static long read_cb(void *ctx, int src, uint64_t off, void *buf, size_t n) {
  (void)ctx;
  const uint8_t *data = src < 0 ? g_orig : (const uint8_t *)SRC[src];
  const uint64_t size = src < 0 ? (uint64_t)g_orig_size : strlen(SRC[src]);
  if (off >= size) return 0;
  const size_t take = n < size - off ? n : (size_t)(size - off);
  memcpy(buf, data + off, take);
  return (long)take;
}

static rd_zipview_file file(const char *name, int src) {
  const uint32_t crc = (uint32_t)crc32(0, (const Bytef *)SRC[src], (uInt)strlen(SRC[src]));
  return (rd_zipview_file){ name, strlen(SRC[src]), crc, src };
}

/* The whole view as a FILE, through rd_zipview_read in odd-sized chunks (Unity reads archives
 * in small pieces at arbitrary offsets). */
static FILE *materialize(const rd_zipview *v) {
  FILE *f = tmpfile();
  uint8_t buf[37];
  for (uint64_t off = 0; off < v->size;) {
    const long r = rd_zipview_read(v, off, buf, sizeof buf, read_cb, NULL);
    if (r <= 0) break;
    fwrite(buf, 1, (size_t)r, f);
    off += (uint64_t)r;
  }
  rewind(f);
  return f;
}

/* Reads entry `name` of f as a string (NULL if missing). */
static char *entry(FILE *f, const char *name, rd_zip_entry *e) {
  return rd_zip_find(f, name, e) == 0 ? rd_zip_read(f, e) : NULL;
}

/* A replaced entry reads as the mod file, an added one exists, and untouched entries -- stored
 * and deflated -- read exactly as before: the view is a valid ZIP for the port's own reader. */
static void test_replace_and_add(void) {
  const rd_zipview_file files[] = { file("dir/boot.config", 0), file("mods/new.txt", 1) };
  rd_zipview v;
  CHECK(rd_zipview_build(&v, (uint64_t)g_orig_size, files, 2, read_cb, NULL) == 0);
  CHECK(v.replaced == 1 && v.added == 1);
  FILE *f = materialize(&v);
  rd_zip_entry e;
  char *s = entry(f, "dir/boot.config", &e);
  CHECK(s && !strcmp(s, SRC[0]) && e.method == ZIP_STORED);
  CHECK(e.data_offset % RD_ZIPVIEW_ALIGN == 0);              /* aligned like zipalign */
  free(s);
  s = entry(f, "mods/new.txt", &e);
  CHECK(s && !strcmp(s, SRC[1]));
  free(s);
  s = entry(f, "stored.txt", &e);
  CHECK(s && !strcmp(s, "hello stored"));
  free(s);
  s = entry(f, "deflated.txt", &e);
  CHECK(s && strlen(s) == 1500);
  free(s);
  fclose(f);
  rd_zipview_free(&v);
}

/* The central directory record of a replaced entry carries the new CRC and size, so a reader
 * that checks them (Unity may) agrees with the data. */
static void test_crc_in_directory(void) {
  const rd_zipview_file files[] = { file("stored.txt", 0) };
  rd_zipview v;
  CHECK(rd_zipview_build(&v, (uint64_t)g_orig_size, files, 1, read_cb, NULL) == 0);
  const rd_zipview_seg *dir = &v.segs[v.nsegs - 1];
  const uint8_t *p = v.mem + dir->off;
  int seen = 0;
  for (; zip_le32(p) == ZIP_CDH_SIG;
       p += ZIP_CDH_SIZE + zip_le16(p + ZIP_CDH_NAME_LEN) + zip_le16(p + ZIP_CDH_EXTRA_LEN) +
            zip_le16(p + ZIP_CDH_COMMENT_LEN)) {
    if (zip_le16(p + ZIP_CDH_NAME_LEN) == 10 && !memcmp(p + ZIP_CDH_SIZE, "stored.txt", 10)) {
      seen++;
      CHECK(zip_le32(p + ZIP_CDH_CRC) == files[0].crc);
      CHECK(zip_le32(p + ZIP_CDH_SIZE_UNCOMP) == files[0].size);
    }
  }
  CHECK(seen == 1);                                          /* replaced, not duplicated */
  CHECK(zip_le32(p) == ZIP_EOCD_SIG && zip_le16(p + ZIP_EOCD_ENTRIES) == 3);
  rd_zipview_free(&v);
}

/* A name given twice is put in once (the first mod wins), and empty or nameless entries are
 * ignored instead of producing a broken directory. */
static void test_duplicates_and_empty_names(void) {
  const rd_zipview_file files[] = { file("x.txt", 1), file("x.txt", 2), file("", 2) };
  rd_zipview v;
  CHECK(rd_zipview_build(&v, (uint64_t)g_orig_size, files, 3, read_cb, NULL) == 0);
  CHECK(v.added == 1 && v.replaced == 0);
  FILE *f = materialize(&v);
  rd_zip_entry e;
  char *s = entry(f, "x.txt", &e);
  CHECK(s && !strcmp(s, SRC[1]));
  free(s);
  fclose(f);
  rd_zipview_free(&v);
}

/* No files: the view still reads as the same archive (offsets unchanged up to the directory). */
static void test_no_files(void) {
  rd_zipview v;
  CHECK(rd_zipview_build(&v, (uint64_t)g_orig_size, NULL, 0, read_cb, NULL) == 0);
  uint8_t head[64];
  CHECK(rd_zipview_read(&v, 0, head, sizeof head, read_cb, NULL) == (long)sizeof head);
  CHECK(!memcmp(head, g_orig, sizeof head));
  CHECK(rd_zipview_read(&v, v.size, head, 1, read_cb, NULL) == 0);   /* end of view */
  rd_zipview_free(&v);
}

static uint8_t g_fake[64];
/* Read callback over g_fake, a buffer that is not a usable ZIP. */
static long fake_cb(void *ctx, int src, uint64_t off, void *buf, size_t n) {
  (void)ctx; (void)src;
  if (off >= sizeof g_fake) return 0;
  const size_t take = n < sizeof g_fake - off ? n : (size_t)(sizeof g_fake - off);
  memcpy(buf, g_fake + off, take);
  return (long)take;
}

/* Something that is not a ZIP is -1; a ZIP64 end record (entry count 0xFFFF) is -2: the view
 * cannot represent it, so the mod layer keeps the original archive. */
static void test_bad_archives(void) {
  rd_zipview v;
  memset(g_fake, 'x', sizeof g_fake);
  CHECK(rd_zipview_build(&v, sizeof g_fake, NULL, 0, fake_cb, NULL) == -1);
  CHECK(rd_zipview_build(&v, 10, NULL, 0, fake_cb, NULL) == -1);
  memset(g_fake, 0, sizeof g_fake);
  uint8_t *eo = g_fake + sizeof g_fake - ZIP_EOCD_SIZE;
  zip_put32(eo, ZIP_EOCD_SIG);
  zip_put16(eo + ZIP_EOCD_ENTRIES, ZIP_U16_MAX);
  CHECK(rd_zipview_build(&v, sizeof g_fake, NULL, 0, fake_cb, NULL) == -2);
}

int main(int argc, char **argv) {
  char path[512];
  snprintf(path, sizeof path, "%s/test.zip", argc > 1 ? argv[1] : ".");
  FILE *f = fopen(path, "rb");
  CHECK(f != NULL);
  if (!f) return 1;
  fseek(f, 0, SEEK_END);
  g_orig_size = ftell(f);
  rewind(f);
  g_orig = malloc((size_t)g_orig_size);
  CHECK(fread(g_orig, 1, (size_t)g_orig_size, f) == (size_t)g_orig_size);
  fclose(f);
  RUN(test_replace_and_add);
  RUN(test_crc_in_directory);
  RUN(test_duplicates_and_empty_names);
  RUN(test_no_files);
  RUN(test_bad_archives);
  free(g_orig);
  return g_failures != 0;
}
