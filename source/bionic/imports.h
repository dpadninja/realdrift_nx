/* imports.h -- .so import resolution
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 * Forked from badpiggies_nx by ChanseyIsTheBest (https://github.com/ChanseyIsTheBest/badpiggies_nx,
 * commit 7726be3), from the so-loader lineage; changed for realdrift_nx. MIT (see LICENSE).
 */

#ifndef __IMPORTS_H__
#define __IMPORTS_H__

#include <stdio.h>
#include <stdlib.h>
#include "so_util.h"

/* The engine's stderr (&fake_sF[2], libc_shim.h) and the main import table. */
extern FILE *stderr_fake;
extern DynLibFunction dynlib_functions[];
extern size_t dynlib_numfunctions;
/* Our implementation of `name` from the import tables, or 0 (dlsym's fallback). */
uintptr_t dynlib_find_export(const char *name);


// relocate `mod` and resolve its imports against dynlib_functions[] (and the
// other already-loaded modules). Used for libmain, libunity and libil2cpp.
void nx_resolve_imports(so_module *mod);

#endif
