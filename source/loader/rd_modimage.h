/* rd_modimage.h -- rd_image (rd_patchsites.h) over a module loaded by so_util.
 * Host-testable (tests/c/test_rd_modimage.c).
 * MIT (see LICENSE). */
#ifndef RD_MODIMAGE_H
#define RD_MODIMAGE_H

#include "rd_patchsites.h"
#include "so_util.h"

/* Fills *im with the module's executable PT_LOAD as mapped at load_virtbase (link-time
 * addresses in rd_image are relative to the module base). Returns 0, or -1 if none. */
int rd_image_from_module(const so_module *m, rd_image *im);

#endif
