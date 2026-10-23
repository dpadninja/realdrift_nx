/* rd_slvolume.h -- the OpenSL ES volume interface's numbers (opensles.c, opensles_mixer.c).
 *
 * SetVolumeLevel gives a level in millibels, SetMute a flag. The mixer scales a player by
 * rd_slvolume_out(gain, muted): the level is kept while muted, so clearing mute brings it
 * back (zeroing the gain on mute, as the forked shim did, silenced a player for good after
 * mute -> unmute). Pure logic, host-tested (tests/c/test_rd_slvolume.c). MIT (see LICENSE). */
#ifndef RD_SLVOLUME_H
#define RD_SLVOLUME_H

#include <stdint.h>

#define RD_SLVOLUME_MIN_MB (-9600)      /* OpenSL's silence */

/* Linear gain of a level in millibels: 0 mB (and anything above, which callers pass as
 * garbage) = 1.0, RD_SLVOLUME_MIN_MB and below = 0; 100 mB = 1 dB. */
float rd_slvolume_gain(int32_t millibel);

/* The factor the mixer applies: 0 while muted, else the player's gain. */
float rd_slvolume_out(float gain, int muted);

#endif
