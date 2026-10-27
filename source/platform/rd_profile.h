/* rd_profile.h -- the Switch user who runs the game: their profile nickname.
 *
 * The account service (acc:u0) names the user the game was started for (PreselectedUser,
 * when launched as an application) or, from the homebrew menu, the last user who opened an
 * application. Read once at boot; no network. MIT (see LICENSE). */
#ifndef RD_PROFILE_H
#define RD_PROFILE_H

#include <stddef.h>

#define RD_PROFILE_NICK_MAX 32          /* AccountProfileBase.nickname: UTF-8, not terminated */

/* Copies the user's nickname into out[0..n) (raw bytes, terminated, at most
 * RD_PROFILE_NICK_MAX of them). Returns 1, or 0 when no user is known (out = ""). */
int rd_profile_nickname(char *out, size_t n);

#endif
