/* (c) TeeDefenceArchive - 2026 */

#include "account_crypto.h"

#include <string.h>

#include <base/hash.h>

static const char ACCOUNT_PASSWORD_PREFIX[] = "$sha256$";

bool AccountPasswordIsHashed(const char *pStored)
{
	return pStored && str_startswith(pStored, ACCOUNT_PASSWORD_PREFIX);
}

bool AccountPasswordHash(const char *pPlain, char *pOut, int OutLen)
{
	if(!pPlain || !pOut || OutLen < 96)
		return false;

	unsigned char aSalt[8];
	secure_random_fill(aSalt, sizeof(aSalt));

	char aSaltHex[17];
	for(int i = 0; i < 8; i++)
		str_format(aSaltHex + i * 2, 3, "%02x", aSalt[i]);

	char aBuf[256];
	str_copy(aBuf, aSaltHex, sizeof(aBuf));
	str_append(aBuf, pPlain, sizeof(aBuf));

	SHA256_DIGEST Digest = sha256(aBuf, str_length(aBuf));
	char aHashHex[SHA256_MAXSTRSIZE];
	sha256_str(Digest, aHashHex, sizeof(aHashHex));

	str_format(pOut, OutLen, "%s%s$%s", ACCOUNT_PASSWORD_PREFIX, aSaltHex, aHashHex);
	return true;
}

bool AccountPasswordVerify(const char *pPlain, const char *pStored, char *pUpgradeHash, int UpgradeLen)
{
	if(!pPlain || !pStored)
		return false;

	if(!AccountPasswordIsHashed(pStored))
	{
		if(str_comp(pPlain, pStored) != 0)
			return false;
		if(pUpgradeHash && UpgradeLen > 0)
			AccountPasswordHash(pPlain, pUpgradeHash, UpgradeLen);
		return true;
	}

	const char *pBody = pStored + str_length(ACCOUNT_PASSWORD_PREFIX);
	const char *pSep = strchr(pBody, '$');
	if(!pSep || pSep - pBody != 16)
		return false;

	char aSaltHex[17];
	mem_copy(aSaltHex, pBody, 16);
	aSaltHex[16] = 0;
	const char *pHashHex = pSep + 1;
	if(str_length(pHashHex) != 64)
		return false;

	char aBuf[256];
	str_copy(aBuf, aSaltHex, sizeof(aBuf));
	str_append(aBuf, pPlain, sizeof(aBuf));

	SHA256_DIGEST Digest = sha256(aBuf, str_length(aBuf));
	char aCalcHex[SHA256_MAXSTRSIZE];
	sha256_str(Digest, aCalcHex, sizeof(aCalcHex));
	return str_comp(aCalcHex, pHashHex) == 0;
}
