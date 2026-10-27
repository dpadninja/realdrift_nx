/* rd_fsdeny.h -- files the port refuses to write (config.txt offline_ui).
 *
 * Unity Analytics queues its events on disk under <game folder>/Unity/<project id>/Analytics/
 * (ArchivedEvents/<session>/{s,g,c,e,p} and a values file, a few per launch) and sends them
 * when it is online. The port is never online, so the queue only grows on the SD card. Its
 * switches (Analytics.enabled, deviceStatsEnabled) are not among the icalls this build
 * registers, so the files are refused instead: open / fopen for writing, mkdir and rename into
 * that folder fail with EACCES, and Unity gives the event up (analysis/research/32-offline-ui.md).
 * Pure logic, host-tested (tests/c/test_rd_fsdeny.c). MIT (see LICENSE). */
#ifndef RD_FSDENY_H
#define RD_FSDENY_H

#define RD_UNITY_DATA_DIR      "Unity"       /* Unity's per-project folder in the game folder */
#define RD_ANALYTICS_DIR       "Analytics"   /* below RD_UNITY_DATA_DIR/<project id>/ */

/* Turns the refusal on (1) or off (0, the default). Called once at boot, before the engine. */
void rd_fsdeny_set_analytics(int on);

/* 1 if writing at `path` (a file or a directory to create) is refused: the refusal is on and
 * path is the analytics folder or below it, with or without the "sdmc:" prefix. */
int rd_fsdeny_write(const char *path);

#endif
