/* rd_profile.c -- see rd_profile.h. MIT (see LICENSE). */
#include <string.h>
#include <switch.h>
#include "rd_profile.h"
#include "util.h"

int rd_profile_nickname(char *out, size_t n) {
  if (!out || !n) return 0;
  out[0] = '\0';
  Result rc = accountInitialize(AccountServiceType_Application);
  if (R_FAILED(rc)) {
    debugPrintf("[profile] account service: 0x%x\n", rc);
    return 0;
  }
  AccountUid uid = {0};
  rc = accountGetPreselectedUser(&uid);
  if (R_FAILED(rc) || !accountUidIsValid(&uid)) rc = accountGetLastOpenedUser(&uid);
  AccountProfile profile;
  AccountProfileBase base;
  int ok = 0;
  if (R_SUCCEEDED(rc) && accountUidIsValid(&uid) &&
      R_SUCCEEDED(accountGetProfile(&profile, uid))) {
    if (R_SUCCEEDED(accountProfileGet(&profile, NULL, &base))) {
      size_t len = strnlen(base.nickname, sizeof base.nickname);
      if (len > RD_PROFILE_NICK_MAX) len = RD_PROFILE_NICK_MAX;
      if (len >= n) len = n - 1;
      memcpy(out, base.nickname, len);
      out[len] = '\0';
      ok = 1;
    }
    accountProfileClose(&profile);
  }
  accountExit();
  if (!ok) debugPrintf("[profile] no user (0x%x)\n", rc);
  return ok;
}
