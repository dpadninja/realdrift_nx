/* rd_jobs.h -- a small pool of worker threads on the CPU cores the engine leaves idle.
 *
 * Every engine thread runs on core 0 (newlib's pthread_create starts threads on the process's
 * default core, analysis/research/09), so cores 1 and 2 are free. rd_jobs_run splits one piece
 * of work into parts and runs them on a worker per free core plus the calling thread, which
 * returns when all parts are done. Workers run only our own pure code (no engine calls, no
 * bionic TLS). They start on the first rd_jobs_run; without free cores (e.g. run as an applet)
 * everything runs in the caller. Calls are serialised. MIT (see LICENSE). */
#ifndef RD_JOBS_H
#define RD_JOBS_H

#define RD_JOBS_MAX_WORKERS 2     /* cores 1 and 2 */

/* One part of a job: part `part` of `parts`. */
typedef void (*rd_job_fn)(void *ctx, int part, int parts);

/* Runs fn(ctx, i, parts) for every i in [0, parts) and returns when all have finished. Returns
 * the number of threads that worked on it (1 = the caller alone). Any thread. */
int rd_jobs_run(rd_job_fn fn, void *ctx, int parts);

#endif
