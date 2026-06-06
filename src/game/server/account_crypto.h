/* (c) TeeDefenceArchive - 2026 */
#ifndef GAME_SERVER_ACCOUNT_CRYPTO_H
#define GAME_SERVER_ACCOUNT_CRYPTO_H

#include <base/system.h>

/** Stored format: $sha256$<16 hex salt>$<64 hex digest> */
bool AccountPasswordIsHashed(const char *pStored);
bool AccountPasswordHash(const char *pPlain, char *pOut, int OutLen);
/** On legacy plaintext match, writes upgrade hash to pUpgradeHash when provided. */
bool AccountPasswordVerify(const char *pPlain, const char *pStored, char *pUpgradeHash, int UpgradeLen);

#endif
