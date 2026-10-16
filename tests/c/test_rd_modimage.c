/* sources: source/rd_modimage.c */
#include <string.h>
#include "check.h"
#include "rd_modimage.h"

static uint32_t g_mem[256];   /* fake loaded module: code at +0x40, rodata string at +0x200 */

static void setup(so_module *m) {
  memset(m, 0, sizeof *m);
  memset(g_mem, 0, sizeof g_mem);
  m->load_virtbase = g_mem;
  m->load_size = sizeof g_mem;
  m->phnum = 3;
  m->phdr[0] = (Elf64_Phdr){ .p_type = PT_PHDR };
  m->phdr[1] = (Elf64_Phdr){ .p_type = PT_LOAD, .p_flags = PF_R | PF_X, .p_vaddr = 0x40, .p_filesz = 0x100 };
  m->phdr[2] = (Elf64_Phdr){ .p_type = PT_LOAD, .p_flags = PF_R | PF_W, .p_vaddr = 0x200, .p_filesz = 0x100 };
  g_mem[0x40 / 4] = 0xd503201f;
  strcpy((char *)g_mem + 0x200, "Choreographer available");
}

static void test_exec_segment(void) {
  so_module m; rd_image im; setup(&m);
  CHECK(rd_image_from_module(&m, &im) == 0);
  CHECK(im.vaddr == 0x40 && im.nwords == 0x40 && im.code[0] == 0xd503201f);
  CHECK(strcmp(im.cstr(im.ctx, 0x200), "Choreographer available") == 0);
  CHECK(im.cstr(im.ctx, sizeof g_mem) == NULL);
}

static void test_bad_modules(void) {
  so_module m; rd_image im; setup(&m);
  m.phdr[1].p_flags = PF_R;                      /* no executable segment */
  CHECK(rd_image_from_module(&m, &im) == -1);
  setup(&m); m.phdr[1].p_filesz = 0x10000;       /* segment past the mapping */
  CHECK(rd_image_from_module(&m, &im) == -1);
  setup(&m); m.load_virtbase = NULL;
  CHECK(rd_image_from_module(&m, &im) == -1);
  CHECK(rd_image_from_module(NULL, &im) == -1);
}

int main(void) {
  RUN(test_exec_segment);
  RUN(test_bad_modules);
  return g_failures != 0;
}
