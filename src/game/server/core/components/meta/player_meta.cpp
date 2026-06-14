#include "player_meta.h"

#include <base/math.h>
#include <base/system.h>

#include <stdio.h>
#include <string.h>

void PlayerMeta_Reset(SPlayerMetaData *pMeta)
{
	if(!pMeta)
		return;
	mem_zero(pMeta, sizeof(*pMeta));
}

static void CopyToday(char *pOut, int OutSize)
{
	char aStamp[64];
	str_timestamp(aStamp, sizeof(aStamp));
	if(str_length(aStamp) >= 10)
		str_copy(pOut, aStamp, minimum(OutSize, 11));
	else
		str_copy(pOut, "1970-01-01", OutSize);
}

void PlayerMeta_EnsureDay(SPlayerMetaData *pMeta)
{
	if(!pMeta)
		return;
	char aToday[16];
	CopyToday(aToday, sizeof(aToday));
	if(pMeta->m_aDay[0] && str_comp(pMeta->m_aDay, aToday) == 0)
		return;
	str_copy(pMeta->m_aDay, aToday, sizeof(pMeta->m_aDay));
	mem_zero(pMeta->m_aDutyProgress, sizeof(pMeta->m_aDutyProgress));
	mem_zero(pMeta->m_aDutyClaimed, sizeof(pMeta->m_aDutyClaimed));
}

bool PlayerMeta_Parse(const char *pJson, SPlayerMetaData *pMeta)
{
	PlayerMeta_Reset(pMeta);
	if(!pJson || !pJson[0])
	{
		PlayerMeta_EnsureDay(pMeta);
		return true;
	}

	const char *pDay = strstr(pJson, "\"day\":\"");
	if(pDay)
		sscanf(pDay, "\"day\":\"%15[^\"]\"", pMeta->m_aDay);

	for(int i = 0; i < MAX_META_DUTIES; i++)
	{
		char aKey[24];
		str_format(aKey, sizeof(aKey), "\"d%d\":", i);
		const char *pProg = strstr(pJson, aKey);
		if(pProg)
			pMeta->m_aDutyProgress[i] = str_toint(pProg + str_length(aKey));

		str_format(aKey, sizeof(aKey), "\"dc%d\":", i);
		const char *pClaim = strstr(pJson, aKey);
		if(pClaim)
			pMeta->m_aDutyClaimed[i] = str_toint(pClaim + str_length(aKey)) != 0;
	}

	for(int i = 0; i < MAX_META_ACHIEVEMENTS; i++)
	{
		char aKey[24];
		str_format(aKey, sizeof(aKey), "\"ap%d\":", i);
		const char *pProg = strstr(pJson, aKey);
		if(pProg)
			pMeta->m_aAchProgress[i] = str_toint(pProg + str_length(aKey));

		char aPat[16];
		str_format(aPat, sizeof(aPat), "\"a%d\":1", i);
		if(strstr(pJson, aPat))
			pMeta->m_aaAchievements[i] = true;
	}

	{
		const char *pTrait = strstr(pJson, "\"trait\":\"");
		if(pTrait)
			sscanf(pTrait, "\"trait\":\"%31[^\"]\"", pMeta->m_aTraitId);
		const char *pLocked = strstr(pJson, "\"trait_locked\":1");
		if(pLocked)
			pMeta->m_TraitLocked = true;
	}

	PlayerMeta_EnsureDay(pMeta);
	return true;
}

void PlayerMeta_Serialize(const SPlayerMetaData *pMeta, char *pOut, int OutSize)
{
	if(!pOut || OutSize <= 0)
		return;
	if(!pMeta)
	{
		pOut[0] = 0;
		return;
	}

	str_format(pOut, OutSize, "{\"day\":\"%s\"", pMeta->m_aDay);
	int Len = str_length(pOut);
	for(int i = 0; i < MAX_META_DUTIES && Len < OutSize - 4; i++)
	{
		if(pMeta->m_aDutyProgress[i] != 0)
		{
			str_format(pOut + Len, OutSize - Len, ",\"d%d\":%d", i, pMeta->m_aDutyProgress[i]);
			Len = str_length(pOut);
		}
		if(pMeta->m_aDutyClaimed[i])
		{
			str_format(pOut + Len, OutSize - Len, ",\"dc%d\":1", i);
			Len = str_length(pOut);
		}
	}
	for(int i = 0; i < MAX_META_ACHIEVEMENTS && Len < OutSize - 4; i++)
	{
		if(pMeta->m_aAchProgress[i] != 0)
		{
			str_format(pOut + Len, OutSize - Len, ",\"ap%d\":%d", i, pMeta->m_aAchProgress[i]);
			Len = str_length(pOut);
		}
		if(pMeta->m_aaAchievements[i])
		{
			str_format(pOut + Len, OutSize - Len, ",\"a%d\":1", i);
			Len = str_length(pOut);
		}
	}
	if(pMeta->m_aTraitId[0] && Len < OutSize - 48)
	{
		str_format(pOut + Len, OutSize - Len, ",\"trait\":\"%s\"", pMeta->m_aTraitId);
		Len = str_length(pOut);
	}
	if(pMeta->m_TraitLocked && Len < OutSize - 24)
	{
		str_format(pOut + Len, OutSize - Len, ",\"trait_locked\":1");
		Len = str_length(pOut);
	}
	if(Len < OutSize - 2)
		str_append(pOut, "}", OutSize);
}
