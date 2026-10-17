/* unity_imports.h -- supplementary import table (see unity_imports.c)
 * Forked from badpiggies_nx by ChanseyIsTheBest (https://github.com/ChanseyIsTheBest/badpiggies_nx,
 * commit 7726be3), from the so-loader lineage; changed for realdrift_nx. MIT (see LICENSE). */
#ifndef UNITY_IMPORTS_H
#define UNITY_IMPORTS_H
#include "imports.h"
extern DynLibFunction unity_dynlib_functions[];
extern int unity_dynlib_numfunctions;

/* NULL-safe strncasecmp (a NULL string sorts first); also serves imports.c's table. */
int z_strncasecmp(const char *a, const char *b, unsigned long n);
#endif
