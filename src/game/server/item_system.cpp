/* (c) TeeDefenceArchive - 2026 */
#include <engine/shared/jsonparser.h>
#include <engine/storage.h>

#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "item_system.h"

CItemHelper::CItemHelper(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
	ResetStats();
}

void CItemHelper::ResetStats()
{
	mem_zero(m_aToolDmg, sizeof(m_aToolDmg));
	mem_zero(m_aMatHealth, sizeof(m_aMatHealth));
	mem_zero(m_aaItemName, sizeof(m_aaItemName));
	mem_zero(m_aaItemDesc, sizeof(m_aaItemDesc));
	mem_zero(m_aaItemNameKey, sizeof(m_aaItemNameKey));
	mem_zero(m_aItemType, sizeof(m_aItemType));
	mem_zero(m_aItemMaxStack, sizeof(m_aItemMaxStack));
	mem_zero(m_aFormula, sizeof(m_aFormula));
	mem_zero(m_aHasFormula, sizeof(m_aHasFormula));
	mem_zero(m_aProba, sizeof(m_aProba));
	mem_zero(m_aMaxPlace, sizeof(m_aMaxPlace));
	mem_zero(m_aaPlaceable, sizeof(m_aaPlaceable));
	mem_zero(m_aaItemEffects, sizeof(m_aaItemEffects));
	mem_zero(m_aNumItemEffects, sizeof(m_aNumItemEffects));

	for(int i = 0; i < NUM_ITEM; i++)
		m_aItemType[i] = ITYPE_MATERIAL;
}

void CItemHelper::ConsumeStatPass(const json_value &Entry, int ParentType)
{
	int Type = ParentType;
	if(Entry["type"].type == json_integer)
		Type = (int)Entry["type"].u.integer;
	if(Entry["id"].type != json_integer)
		return;
	int ID = (int)Entry["id"].u.integer;
	if(!CheckItemValid(ID))
		return;
	if(Entry["name"].type == json_string)
		str_copy(m_aaItemName[ID], Entry["name"].u.string.ptr, sizeof(m_aaItemName[ID]));
	if(Entry["desc"].type == json_string)
		str_copy(m_aaItemDesc[ID], Entry["desc"].u.string.ptr, sizeof(m_aaItemDesc[ID]));
	if(Entry["name_key"].type == json_string)
		str_copy(m_aaItemNameKey[ID], Entry["name_key"].u.string.ptr, sizeof(m_aaItemNameKey[ID]));
	m_aItemType[ID] = Type;
	if(Entry["max"].type == json_integer)
		m_aItemMaxStack[ID] = (int)Entry["max"].u.integer;
	if(Entry["health"].type == json_integer)
		m_aMatHealth[ID] = (int)Entry["health"].u.integer;
	if(Entry["damage"].type == json_integer)
		m_aToolDmg[ID].m_Damage = (int)Entry["damage"].u.integer;
	if(Entry["capacity"].type == json_integer)
		m_aToolDmg[ID].m_Capacity = (int)Entry["capacity"].u.integer;
	if(Entry["defense"].type == json_integer)
		m_aToolDmg[ID].m_Defense = (int)Entry["defense"].u.integer;
	if(Entry["proba"].type == json_integer)
		m_aProba[ID] = (int)Entry["proba"].u.integer;
	if(Entry["max_place"].type == json_integer)
		m_aMaxPlace[ID] = (int)Entry["max_place"].u.integer;
	if(Entry["placeable"].type == json_array)
	{
		for(int t = 0; t < NUM_ITYPE; t++)
			m_aaPlaceable[ID][t] = false;
		for(unsigned i = 0; i < Entry["placeable"].u.array.length; i++)
		{
			const json_value &PV = Entry["placeable"][(int)i];
			if(PV.type != json_integer)
				continue;
			const int T = (int)PV.u.integer;
			if(T >= 0 && T < NUM_ITYPE)
				m_aaPlaceable[ID][T] = true;
		}
	}
	if(Entry["effect"].type == json_string && m_aNumItemEffects[ID] < MAX_ITEM_EFFECT_KEYS)
	{
		str_copy(m_aaItemEffects[ID][m_aNumItemEffects[ID]++], Entry["effect"].u.string.ptr,
			sizeof(m_aaItemEffects[ID][0]));
	}
	const json_value &Effects = Entry["effects"];
	if(Effects.type == json_array)
	{
		for(unsigned e = 0; e < Effects.u.array.length && m_aNumItemEffects[ID] < MAX_ITEM_EFFECT_KEYS; e++)
		{
			const json_value &EV = Effects[(int)e];
			if(EV.type != json_string)
				continue;
			str_copy(m_aaItemEffects[ID][m_aNumItemEffects[ID]++], EV.u.string.ptr, sizeof(m_aaItemEffects[ID][0]));
		}
	}
}

void CItemHelper::ConsumeFormulaPass(const json_value &Entry)
{
	if(Entry["id"].type != json_integer)
		return;
	int ID = (int)Entry["id"].u.integer;
	if(!CheckItemValid(ID))
		return;
	const json_value &Form = Entry["formula"];
	if(Form.type != json_object)
		return;
	mem_zero(m_aFormula[ID], sizeof(m_aFormula[ID]));
	m_aHasFormula[ID] = true;
	for(unsigned i = 0; i < Form.u.object.length; ++i)
	{
		const char *pKey = Form.u.object.values[i].name;
		const json_value *pVal = Form.u.object.values[i].value;
		if(!pVal || pVal->type != json_integer)
			continue;
		int MatId = -1;
		if(pKey[0] >= '0' && pKey[0] <= '9')
		{
			MatId = str_toint(pKey);
			if(!CheckItemValid(MatId))
				MatId = -1;
		}
		if(MatId < 0)
		{
			dbg_msg("items", "formula for id=%d: material key '%s' must be a TD item id (integer string)", ID, pKey);
			continue;
		}
		m_aFormula[ID][MatId] = (int)pVal->u.integer;
	}
}

void CItemHelper::LoadItemFilePass(int Pass, const json_value *pRoot)
{
	if(!pRoot)
		return;
	const json_value &rItem = (*pRoot)["item"];
	if(rItem.type == json_none)
		return;
	int ParentType = ITYPE_MATERIAL;
	if(rItem["type"].type == json_integer)
		ParentType = (int)rItem["type"].u.integer;
	const json_value &Mult = rItem["multiple"];
	if(Mult.type == json_array)
	{
		for(unsigned i = 0; i < Mult.u.array.length; i++)
		{
			if(Pass == 1)
				ConsumeStatPass(Mult[(int)i], ParentType);
			else
				ConsumeFormulaPass(Mult[(int)i]);
		}
	}
	else if(rItem["id"].type == json_integer)
	{
		if(Pass == 1)
			ConsumeStatPass(rItem, ParentType);
		else
			ConsumeFormulaPass(rItem);
	}
}

void CItemHelper::LoadDefinitions(IStorage *pStorage)
{
	if(!pStorage)
		return;

	enum
	{
		MAX_ITEM_JSON_FILES = 128,
	};

	char aaFiles[MAX_ITEM_JSON_FILES][256];
	int nFiles = 0;

	CJsonParser IdxParser;
	json_value *pIdx = IdxParser.ParseFile("server_content/td/items/index.json", pStorage);
	if(!pIdx)
	{
		dbg_msg("items", "server_content/td/items/index.json: %s", IdxParser.Error());
		return;
	}

	const json_value &Arr = (*pIdx)["item indices"];
	if(Arr.type != json_array)
	{
		dbg_msg("items", "server_content/td/items/index.json: missing 'item indices' array");
		return;
	}

	for(unsigned i = 0; i < Arr.u.array.length && nFiles < MAX_ITEM_JSON_FILES; i++)
	{
		const json_value &V = Arr[(int)i];
		if(V.type != json_string)
			continue;
		str_copy(aaFiles[nFiles++], V.u.string.ptr, sizeof(aaFiles[0]));
	}

	ResetStats();

	for(int Pass = 1; Pass <= 2; Pass++)
	{
		for(int fi = 0; fi < nFiles; fi++)
		{
			CJsonParser P;
			json_value *pRoot = P.ParseFile(aaFiles[fi], pStorage);
			if(!pRoot)
			{
				dbg_msg("items", "skip %s: %s", aaFiles[fi], P.Error());
				continue;
			}
			LoadItemFilePass(Pass, pRoot);
		}
	}

	dbg_msg("items", "loaded %d item definition files (JSON)", nFiles);
}

int CItemHelper::FindItemByName(const char *pName) const
{
	if(!pName || !pName[0])
		return -1;
	for(int i = 0; i < NUM_ITEM; i++)
	{
		if(m_aaItemName[i][0] && str_comp(m_aaItemName[i], pName) == 0)
			return i;
	}
	return -1;
}

int CItemHelper::GetFormulaNeed(int CraftId, int MatId) const
{
	if(!CheckItemValid(CraftId) || !CheckItemValid(MatId))
		return 0;
	return m_aFormula[CraftId][MatId];
}

bool CItemHelper::ItemExtraBlocksCraftConsume(const char *pExtraJson) const
{
	if(!pExtraJson || !pExtraJson[0])
		return false;

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseString(pExtraJson, "item_extra");
	if(!pRoot)
		return false;

	const json_value &Ex = (*pRoot)["Extra"];
	if(Ex.type != json_object)
		return false;
	const json_value &Cards = Ex["Cards"];
	const json_value &Parts = Ex["Parts"];
	if(Cards.type == json_array && Cards.u.array.length > 0)
		return true;
	if(Parts.type == json_array && Parts.u.array.length > 0)
		return true;
	return false;
}

int CItemHelper::GetDmg(int ID) const
{
	if(!CheckItemValid(ID))
		return 0;
	return m_aToolDmg[ID].m_Damage;
}

int CItemHelper::GetDefense(int ID) const
{
	if(!CheckItemValid(ID))
		return 0;
	return m_aToolDmg[ID].m_Defense;
}

int CItemHelper::GetMaxHealth(int MatID) const
{
	if(!CheckItemValid(MatID))
		return 0;
	return m_aMatHealth[MatID];
}

int CItemHelper::GetMax(int ID) const
{
	if(!CheckItemValid(ID))
		return 0;
	return m_aItemMaxStack[ID];
}

int CItemHelper::GetMaxCapacity(int ID) const
{
	if(!CheckItemValid(ID))
		return 0;
	return m_aToolDmg[ID].m_Capacity;
}

int CItemHelper::GetProba(int ID) const
{
	if(!CheckItemValid(ID))
		return 0;
	return m_aProba[ID];
}

int CItemHelper::GetMaxPlace(int ID) const
{
	if(!CheckItemValid(ID))
		return 0;
	return m_aMaxPlace[ID] == 0 ? 999 : m_aMaxPlace[ID];
}

bool CItemHelper::IsPlaceableOnItemType(int CardOrPartId, int HostItemType) const
{
	if(!CheckItemValid(CardOrPartId) || HostItemType < 0 || HostItemType >= NUM_ITYPE)
		return false;
	return m_aaPlaceable[CardOrPartId][HostItemType];
}

bool CItemHelper::IsPartItem(int ID) const
{
	return ID >= ITEM_PART_BARREL && ID <= ITEM_PART_STABILIZER;
}

int CItemHelper::GetType(int ID) const
{
	if(!CheckItemValid(ID))
		return ITYPE_MATERIAL;
	return m_aItemType[ID];
}

void CItemHelper::FormatItemLocKey(int ID, char *pBuf, int BufSize) const
{
	if(!pBuf || BufSize <= 0)
		return;
	str_format(pBuf, BufSize, "item.id.%d", ID);
}

const char *CItemHelper::GetItemName(int ID, bool IncludeZero) const
{
	if(!IncludeZero && !ID)
		return "Empty";
	if(!CheckItemValid(ID))
		return "Hand";
	if(m_aaItemName[ID][0])
		return m_aaItemName[ID];
	return "Item";
}

const char *CItemHelper::GetItemDesc(int ID) const
{
	if(!CheckItemValid(ID) || !m_aaItemDesc[ID][0])
		return "";
	return m_aaItemDesc[ID];
}

namespace
{
int ExtraSlotNumFromRoot(const json_value *pRoot, const char *pArrayName, int ItemId)
{
	if(!pRoot || !pArrayName || ItemId < 0)
		return 0;

	const json_value &Ex = (*pRoot)["Extra"];
	if(Ex.type != json_object)
		return 0;
	const json_value &Arr = Ex[pArrayName];
	if(Arr.type != json_array)
		return 0;

	for(unsigned i = 0; i < Arr.u.array.length; i++)
	{
		const json_value &El = Arr[(int)i];
		if(El.type != json_object)
			continue;
		if(El["id"].type != json_integer || (int)El["id"].u.integer != ItemId)
			continue;
		if(El["num"].type == json_integer)
			return (int)El["num"].u.integer;
		return 1;
	}
	return 0;
}
} // namespace

int CItemHelper::GetExtraSlotNum(const char *pExtraJson, const char *pArrayName, int ItemId) const
{
	if(!pExtraJson || !pExtraJson[0] || !pArrayName || ItemId < 0)
		return 0;

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseString(pExtraJson, "item_extra_slot");
	if(!pRoot)
		return 0;

	return ExtraSlotNumFromRoot(pRoot, pArrayName, ItemId);
}

int CItemHelper::GetCard(const char *pExtraJson, int CardID) const
{
	return GetExtraSlotNum(pExtraJson, "Cards", CardID);
}

int CItemHelper::GetPart(const char *pExtraJson, int PartItemId) const
{
	return GetExtraSlotNum(pExtraJson, "Parts", PartItemId);
}

int CItemHelper::GetCapacityFromExtra(const char *pExtraJson) const
{
	if(!pExtraJson || !pExtraJson[0])
		return 0;

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseString(pExtraJson, "item_extra_cap");
	if(!pRoot)
		return 0;

	const json_value &Ex = (*pRoot)["Extra"];
	if(Ex.type != json_object)
		return 0;

	int Sum = 0;
	// Sum capacity of both Cards and Parts
	for(int s = 0; s < 2; s++)
	{
		const char *apKeys[2] = {"Cards", "Parts"};
		const json_value &Arr = Ex[apKeys[s]];
		if(Arr.type != json_array)
			continue;
		for(unsigned i = 0; i < Arr.u.array.length; i++)
		{
			const json_value &El = Arr[(int)i];
			if(El.type != json_object)
				continue;
			if(El["id"].type != json_integer || El["num"].type != json_integer)
				continue;
			const int Id = (int)El["id"].u.integer;
			const int Num = (int)El["num"].u.integer;
			Sum += GetMaxCapacity(Id) * Num;
		}
	}
	return Sum;
}

int CItemHelper::GetNumItemEffects(int ItemId) const
{
	if(!CheckItemValid(ItemId))
		return 0;
	return m_aNumItemEffects[ItemId];
}

const char *CItemHelper::GetItemEffectKey(int ItemId, int EffectIdx) const
{
	if(!CheckItemValid(ItemId) || EffectIdx < 0 || EffectIdx >= m_aNumItemEffects[ItemId])
		return nullptr;
	return m_aaItemEffects[ItemId][EffectIdx];
}

int CItemHelper::GetEffectStacksFromExtra(const char *pExtraJson, int ItemId, const char *pEffectKey) const
{
	if(!pEffectKey || !pEffectKey[0] || !CheckItemValid(ItemId))
		return 0;

	bool Matches = false;
	for(int e = 0; e < m_aNumItemEffects[ItemId]; e++)
	{
		if(str_comp(m_aaItemEffects[ItemId][e], pEffectKey) == 0)
		{
			Matches = true;
			break;
		}
	}
	if(!Matches)
		return 0;

	if(!pExtraJson || !pExtraJson[0])
		return 0;

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseString(pExtraJson, "item_effect_stacks");
	if(!pRoot)
		return 0;

	return ExtraSlotNumFromRoot(pRoot, "Cards", ItemId) + ExtraSlotNumFromRoot(pRoot, "Parts", ItemId);
}

int CItemHelper::QueryEffectStacksFromExtra(const char *pExtraJson, const char *pEffectKey, int LegacyItemId) const
{
	if(!pEffectKey || !pEffectKey[0] || !pExtraJson || !pExtraJson[0])
		return 0;

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseString(pExtraJson, "item_query_effect");
	if(!pRoot)
		return 0;

	const json_value &Ex = (*pRoot)["Extra"];
	if(Ex.type != json_object)
		return 0;

	int Stacks = 0;
	static const char *const s_apSlotKeys[2] = {"Cards", "Parts"};
	for(int s = 0; s < 2; s++)
	{
		const json_value &Arr = Ex[s_apSlotKeys[s]];
		if(Arr.type != json_array)
			continue;
		for(unsigned i = 0; i < Arr.u.array.length; i++)
		{
			const json_value &El = Arr[(int)i];
			if(El.type != json_object || El["id"].type != json_integer)
				continue;
			const int Id = (int)El["id"].u.integer;
			if(!CheckItemValid(Id))
				continue;
			bool Matches = false;
			for(int e = 0; e < m_aNumItemEffects[Id]; e++)
			{
				if(str_comp(m_aaItemEffects[Id][e], pEffectKey) == 0)
				{
					Matches = true;
					break;
				}
			}
			if(!Matches)
				continue;
			const int Num = El["num"].type == json_integer ? (int)El["num"].u.integer : 1;
			Stacks += Num;
		}
	}

	if(LegacyItemId >= 0 && CheckItemValid(LegacyItemId) && m_aNumItemEffects[LegacyItemId] == 0)
		Stacks += ExtraSlotNumFromRoot(pRoot, "Cards", LegacyItemId) + ExtraSlotNumFromRoot(pRoot, "Parts", LegacyItemId);

	return Stacks;
}

int CItemHelper::SumArmorEffectStacks(CPlayer *pP, int CardItemId, const char *pEffectKey) const
{
	if(!pP || !pEffectKey || !pEffectKey[0] || !CheckItemValid(CardItemId))
		return 0;

	const int ArmorTypes[] = {ITYPE_HELMET, ITYPE_CHEST, ITYPE_LEGS};
	int Total = 0;
	for(int a = 0; a < 3; a++)
	{
		const int ArmorId = pP->GetHolding(ArmorTypes[a]);
		if(ArmorId <= 0)
			continue;
		Total += GetEffectStacksFromExtra(pP->GetExtraForItem(ArmorId), CardItemId, pEffectKey);
	}
	return Total;
}

int CItemHelper::CountArmorWithCard(CPlayer *pP, int CardItemId) const
{
	if(!pP || !CheckItemValid(CardItemId))
		return 0;

	const int ArmorTypes[] = {ITYPE_HELMET, ITYPE_CHEST, ITYPE_LEGS};
	int Count = 0;
	for(int a = 0; a < 3; a++)
	{
		const int ArmorId = pP->GetHolding(ArmorTypes[a]);
		if(ArmorId <= 0)
			continue;
		if(GetCard(pP->GetExtraForItem(ArmorId), CardItemId) > 0)
			Count++;
	}
	return Count;
}
