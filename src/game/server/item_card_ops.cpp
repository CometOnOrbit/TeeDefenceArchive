/* (c) TeeDefenceArchive - 2026 */
#include <base/system.h>

#include <engine/shared/jsonparser.h>

#include <game/server/gamecontext.h>
#include <game/server/item_card_ops.h>
#include <game/server/item_system.h>
#include <game/server/player.h>

namespace
{
struct SSlot
{
	int m_Id;
	int m_Num;
};

static bool IsTypeKey(const char *pType)
{
	return str_comp(pType, "Cards") == 0 || str_comp(pType, "Parts") == 0;
}

static int SumSlotCapacity(CItemHelper *pH, const SSlot *pS, int n)
{
	int Sum = 0;
	for(int i = 0; i < n; i++)
		Sum += pH->GetMaxCapacity(pS[i].m_Id) * pS[i].m_Num;
	return Sum;
}

static void LoadSlots(const json_value *pRoot, const char *pKey, SSlot *pOut, int *pN, int MaxN)
{
	*pN = 0;
	if(!pRoot || (*pRoot)["Extra"].type != json_object)
		return;
	const json_value &Arr = (*pRoot)["Extra"][pKey];
	if(Arr.type != json_array)
		return;
	for(unsigned i = 0; i < Arr.u.array.length && *pN < MaxN; i++)
	{
		const json_value &El = Arr[(int)i];
		if(El.type != json_object || El["id"].type != json_integer || El["num"].type != json_integer)
			continue;
		pOut[*pN].m_Id = (int)El["id"].u.integer;
		pOut[*pN].m_Num = (int)El["num"].u.integer;
		(*pN)++;
	}
}

static bool BuildExtra(char *pBuf, int BufSize, const SSlot *pCards, int nC, const SSlot *pParts, int nP)
{
	char aCards[320] = "";
	aCards[0] = 0;
	for(int i = 0; i < nC; i++)
	{
		if(i)
			str_append(aCards, ",", sizeof(aCards));
		char aSeg[72];
		str_format(aSeg, sizeof(aSeg), "{\"id\":%d,\"num\":%d}", pCards[i].m_Id, pCards[i].m_Num);
		str_append(aCards, aSeg, sizeof(aCards));
	}
	char aParts[320] = "";
	aParts[0] = 0;
	for(int i = 0; i < nP; i++)
	{
		if(i)
			str_append(aParts, ",", sizeof(aParts));
		char aSeg[72];
		str_format(aSeg, sizeof(aSeg), "{\"id\":%d,\"num\":%d}", pParts[i].m_Id, pParts[i].m_Num);
		str_append(aParts, aSeg, sizeof(aParts));
	}
	str_format(pBuf, BufSize, "{\"Extra\":{\"Cards\":[%s],\"Parts\":[%s]}}", aCards, aParts);
	return true;
}
} // namespace

bool ItemCardOps_Place(CGameContext *pGame, CPlayer *pP, int HostItemId, const char *pType, int CardId)
{
	if(!pGame || !pP || !pType || !IsTypeKey(pType))
		return false;
	CItemHelper *pH = pGame->ItemHelper();
	if(!pH->CheckItemValid(HostItemId) || !pH->CheckItemValid(CardId))
		return false;
	const int HostType = pH->GetType(HostItemId);
	if(!pH->IsPlaceableOnItemType(CardId, HostType))
		return false;

	SAccSyncData &Acc = pP->m_AccData;
	if(Acc.m_aItems[HostItemId].m_Num < 1 || Acc.m_aItems[CardId].m_Num < 1)
		return false;

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseString(Acc.m_aItems[HostItemId].m_aExtra, "place_card");

	SSlot aCards[24], aParts[24];
	int nC = 0, nP = 0;
	if(pRoot)
	{
		LoadSlots(pRoot, "Cards", aCards, &nC, 24);
		LoadSlots(pRoot, "Parts", aParts, &nP, 24);
	}

	SSlot *pTarget = str_comp(pType, "Cards") == 0 ? aCards : aParts;
	int *pnT = str_comp(pType, "Cards") == 0 ? &nC : &nP;
	const int OtherN = str_comp(pType, "Cards") == 0 ? nP : nC;
	const SSlot *pOther = str_comp(pType, "Cards") == 0 ? aParts : aCards;

	const int HostCap = pH->GetMaxCapacity(HostItemId);
	const int NewPieceCap = pH->GetMaxCapacity(CardId);

	int Exist = -1;
	for(int i = 0; i < *pnT; i++)
	{
		if(pTarget[i].m_Id == CardId)
		{
			Exist = i;
			break;
		}
	}

	const int OldSumTarget = SumSlotCapacity(pH, pTarget, *pnT);
	const int OldOther = SumSlotCapacity(pH, pOther, OtherN);
	const int NewSumTarget = OldSumTarget + NewPieceCap;
	if(NewSumTarget + OldOther > HostCap)
		return false;

	if(Exist >= 0)
	{
		const int MaxP = pH->GetMaxPlace(CardId);
		if(pTarget[Exist].m_Num >= MaxP)
			return false;
		pTarget[Exist].m_Num++;
	}
	else
	{
		if(*pnT >= 24)
			return false;
		pTarget[*pnT].m_Id = CardId;
		pTarget[*pnT].m_Num = 1;
		(*pnT)++;
	}

	char aNew[640];
	if(!BuildExtra(aNew, sizeof(aNew), aCards, nC, aParts, nP))
		return false;

	str_copy(Acc.m_aItems[HostItemId].m_aExtra, aNew, sizeof(Acc.m_aItems[HostItemId].m_aExtra));
	Acc.m_aItems[HostItemId].m_Capacity = NewSumTarget + OldOther;
	Acc.m_aItems[CardId].m_Num--;
	return true;
}

bool ItemCardOps_Separate(CGameContext *pGame, CPlayer *pP, int HostItemId, const char *pType, int CardId)
{
	if(!pGame || !pP || !pType || !IsTypeKey(pType))
		return false;
	CItemHelper *pH = pGame->ItemHelper();
	if(!pH->CheckItemValid(HostItemId) || !pH->CheckItemValid(CardId))
		return false;

	SAccSyncData &Acc = pP->m_AccData;
	if(Acc.m_aItems[HostItemId].m_Num < 1)
		return false;

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseString(Acc.m_aItems[HostItemId].m_aExtra, "sep_card");
	if(!pRoot)
		return false;

	SSlot aCards[24], aParts[24];
	int nC = 0, nP = 0;
	LoadSlots(pRoot, "Cards", aCards, &nC, 24);
	LoadSlots(pRoot, "Parts", aParts, &nP, 24);

	SSlot *pTarget = str_comp(pType, "Cards") == 0 ? aCards : aParts;
	int *pnT = str_comp(pType, "Cards") == 0 ? &nC : &nP;

	int Idx = -1;
	for(int i = 0; i < *pnT; i++)
	{
		if(pTarget[i].m_Id == CardId)
		{
			Idx = i;
			break;
		}
	}
	if(Idx < 0)
		return false;

	pTarget[Idx].m_Num--;
	if(pTarget[Idx].m_Num <= 0)
	{
		for(int j = Idx; j < *pnT - 1; j++)
			pTarget[j] = pTarget[j + 1];
		(*pnT)--;
	}

	char aNew[640];
	if(!BuildExtra(aNew, sizeof(aNew), aCards, nC, aParts, nP))
		return false;

	str_copy(Acc.m_aItems[HostItemId].m_aExtra, aNew, sizeof(Acc.m_aItems[HostItemId].m_aExtra));
	Acc.m_aItems[HostItemId].m_Capacity = SumSlotCapacity(pH, aCards, nC) + SumSlotCapacity(pH, aParts, nP);
	Acc.m_aItems[CardId].m_Num++;
	return true;
}
