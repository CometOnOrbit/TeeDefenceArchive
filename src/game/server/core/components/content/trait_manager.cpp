#include <engine/shared/jsonparser.h>

#include <game/commands.h>
#include <game/server/account.h>
#include <game/server/core/components/content/trait_manager.h>
#include <game/server/core/components/vote/vote_menu_manager.h>
#include <game/server/core/components/vote/vote_menu_types.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

CTraitManager::CTraitManager()
{
	m_NumTraits = 0;
	mem_zero(m_aaPlayerTrait, sizeof(m_aaPlayerTrait));
	mem_zero(m_aTraitLocked, sizeof(m_aTraitLocked));
}

void CTraitManager::LoadTraits()
{
	m_NumTraits = 0;
	if(!Storage())
		return;

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseFile("server_content/traits.json", Storage());
	if(!pRoot)
	{
		dbg_msg("content", "traits.json: %s", Parser.Error());
		return;
	}

	const json_value &Arr = (*pRoot)["traits"];
	if(Arr.type != json_array)
		return;

	for(unsigned i = 0; i < Arr.u.array.length && m_NumTraits < MAX_CONTENT_TRAITS; i++)
	{
		const json_value &T = Arr[(int)i];
		if(T.type != json_object || T["id"].type != json_string)
			continue;
		STraitDef &Def = m_aTraits[m_NumTraits++];
		mem_zero(&Def, sizeof(Def));
		str_copy(Def.m_aId, T["id"].u.string.ptr, sizeof(Def.m_aId));
		const json_value &P = T["params"];
		if(P.type == json_object)
		{
			if(P["damage_mul"].type == json_double)
				Def.m_DamageMul = (float)P["damage_mul"].u.dbl;
			else if(P["damage_mul"].type == json_integer)
				Def.m_DamageMul = (float)P["damage_mul"].u.integer;
			if(P["reload_mul"].type == json_double)
				Def.m_ReloadMul = (float)P["reload_mul"].u.dbl;
			else if(P["reload_mul"].type == json_integer)
				Def.m_ReloadMul = (float)P["reload_mul"].u.integer;
			if(P["mining_luck_bonus"].type == json_integer)
				Def.m_MiningLuckBonus = (int)P["mining_luck_bonus"].u.integer;
			if(P["mine_cd_bonus"].type == json_integer)
				Def.m_MineCdBonus = (int)P["mine_cd_bonus"].u.integer;
			if(P["max_health_bonus"].type == json_integer)
				Def.m_MaxHealthBonus = (int)P["max_health_bonus"].u.integer;
			if(P["shield_bonus"].type == json_integer)
				Def.m_ShieldBonus = (int)P["shield_bonus"].u.integer;
			if(P["skill_cd_mul"].type == json_double)
				Def.m_SkillCdMul = (float)P["skill_cd_mul"].u.dbl;
			else if(P["skill_cd_mul"].type == json_integer)
				Def.m_SkillCdMul = (float)P["skill_cd_mul"].u.integer;
			if(P["lifesteal_bonus"].type == json_integer)
				Def.m_LifestealBonus = (int)P["lifesteal_bonus"].u.integer;
		}
		if(Def.m_DamageMul <= 0.f)
			Def.m_DamageMul = 1.f;
		if(Def.m_ReloadMul <= 0.f)
			Def.m_ReloadMul = 1.f;
		if(Def.m_SkillCdMul <= 0.f)
			Def.m_SkillCdMul = 1.f;
	}
}

void CTraitManager::OnInitWorld(const char *pWhereLocalWorld)
{
	(void)pWhereLocalWorld;
	LoadTraits();
}

const STraitDef *CTraitManager::FindTrait(const char *pId) const
{
	if(!pId || !pId[0])
		return nullptr;
	for(int i = 0; i < m_NumTraits; i++)
	{
		if(str_comp(m_aTraits[i].m_aId, pId) == 0)
			return &m_aTraits[i];
	}
	return nullptr;
}

void CTraitManager::AssignTrait(CPlayer *pPlayer)
{
	if(!pPlayer || pPlayer->IsDummy())
		return;
	const int CID = pPlayer->GetCID();
	if(CID < 0 || CID >= MAX_CLIENTS)
		return;

	if(m_NumTraits <= 0)
	{
		m_aaPlayerTrait[CID][0] = 0;
		return;
	}

	const int Pick = CID % m_NumTraits;
	str_copy(m_aaPlayerTrait[CID], m_aTraits[Pick].m_aId, sizeof(m_aaPlayerTrait[CID]));
}

void CTraitManager::OnPlayerLogin(CPlayer *pPlayer)
{
	AssignTrait(pPlayer);
}

void CTraitManager::OnClientReset(int ClientID)
{
	if(ClientID >= 0 && ClientID < MAX_CLIENTS)
	{
		m_aaPlayerTrait[ClientID][0] = 0;
		m_aTraitLocked[ClientID] = false;
	}
}

bool CTraitManager::SelectTrait(CPlayer *pPlayer, const char *pTraitId)
{
	if(!pPlayer || !GS() || !pTraitId || !pTraitId[0])
		return false;
	const STraitDef *pDef = FindTrait(pTraitId);
	if(!pDef)
		return false;

	const int CID = pPlayer->GetCID();
	if(CID < 0 || CID >= MAX_CLIENTS)
		return false;

	if(m_aaPlayerTrait[CID][0] && str_comp(m_aaPlayerTrait[CID], pTraitId) == 0)
		return true;

	if(m_aTraitLocked[CID])
	{
		if(pPlayer->m_AccData.m_aItems[ITEM_ZOMBIEHEART].m_Num < 100)
		{
			GS()->SendChatLoc(CID, "trait.change.need_heart", u8"更换特质需要 100 个僵尸之心。");
			return false;
		}
		pPlayer->m_AccData.m_aItems[ITEM_ZOMBIEHEART].m_Num -= 100;
		if(GS()->Accounts() && GS()->Accounts()->IsEnabled() && pPlayer->GetAccountId() >= 0)
			GS()->Accounts()->RequestSaveItems(CID);
	}

	str_copy(m_aaPlayerTrait[CID], pDef->m_aId, sizeof(m_aaPlayerTrait[CID]));
	m_aTraitLocked[CID] = true;

	char aKey[48];
	str_format(aKey, sizeof(aKey), "trait.%s", pDef->m_aId);
	GS()->SendChatLocF(CID, "trait.selected", u8"已选择特质：%s", GS()->Loc(CID, aKey, pDef->m_aId));
	return true;
}

void CTraitManager::BuildTraitVotePage(int ClientID)
{
	if(!GS() || !Core() || !Core()->VoteMenuManager())
		return;

	CVoteMenuManager *pVote = Core()->VoteMenuManager();
	CPlayer *pP = GS()->m_apPlayers[ClientID];
	const char *pCurrent = GetPlayerTrait(ClientID);

	pVote->SetVoteBuildClientID(ClientID);
	pVote->AddVote_TextLine(GS()->Loc(ClientID, "trait.menu.title", u8"☪ 特质（职业倾向）"));
	pVote->AddVote_TextLine(GS()->Loc(ClientID, "menu.sep.short", "---"));

	for(int i = 0; i < m_NumTraits; i++)
	{
		const STraitDef &Def = m_aTraits[i];
		char aKey[48];
		str_format(aKey, sizeof(aKey), "trait.%s", Def.m_aId);
		const char *pName = GS()->Loc(ClientID, aKey, Def.m_aId);
		char aLine[VOTE_DESC_LENGTH];
		if(pCurrent && str_comp(pCurrent, Def.m_aId) == 0)
			str_format(aLine, sizeof(aLine), GS()->Loc(ClientID, "trait.entry.active", u8"✓ %s"), pName);
		else
			str_format(aLine, sizeof(aLine), GS()->Loc(ClientID, "trait.entry", u8"▹ %s"), pName);
		char aCmd[48];
		str_format(aCmd, sizeof(aCmd), "ccv_traitselect %d", i);
		pVote->AddVote(aLine, aCmd, ClientID);
	}

	if(pCurrent)
	{
		char aDescKey[56];
		str_format(aDescKey, sizeof(aDescKey), "trait.%s.desc", pCurrent);
		pVote->AddVote_TextLine(GS()->Loc(ClientID, "menu.sep.short", "---"));
		pVote->AddVote_TextLine(GS()->Loc(ClientID, aDescKey, pCurrent));
	}
	pVote->AddVote_TextLine(GS()->Loc(ClientID, "trait.change.hint", u8"首次选择免费；更换需 100 僵尸之心"));
	pVote->AddVote_TextLine(GS()->Loc(ClientID, "menu.sep.long", "---------------------"));
	pVote->AddVote_Back();
	(void)pP;
}

static void ComVoteTraitSelect(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = static_cast<CCommandManager::SCommandContext *>(pUser);
	CGameContext *pGame = static_cast<CGameContext *>(pCtx->m_pContext);
	if(!pGame || !pGame->Core() || !pGame->Core()->TraitManager())
		return;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP)
		return;
	const int Idx = pResult->GetInteger(0);
	const char *pId = pGame->Core()->TraitManager()->GetTraitIdByIndex(Idx);
	if(!pId)
		return;
	if(pGame->Core()->TraitManager()->SelectTrait(pP, pId) && pGame->Core()->VoteMenuManager())
		pGame->Core()->VoteMenuManager()->ClearVotes(pCtx->m_ClientID);
}

void CTraitManager::RegisterVoteCommands(CCommandManager *pMgr)
{
	if(!pMgr || !GS())
		return;
	pMgr->AddCommand("traitselect", "", "i", ComVoteTraitSelect, GS());
}

void CTraitManager::OnCharacterSpawn(CPlayer *pPlayer)
{
	if(!pPlayer || pPlayer->IsDummy())
		return;
	if(m_aaPlayerTrait[pPlayer->GetCID()][0] == 0)
		AssignTrait(pPlayer);

	int ExtraHp = 0;
	int ShieldBonus = 0;
	GetSpawnBonuses(pPlayer, ExtraHp, ShieldBonus);
	if(ExtraHp > 0 && pPlayer->GetCharacter())
		pPlayer->GetCharacter()->IncreaseHealth(ExtraHp);
	(void)ShieldBonus;
}

const char *CTraitManager::GetPlayerTrait(int ClientID) const
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !m_aaPlayerTrait[ClientID][0])
		return nullptr;
	return m_aaPlayerTrait[ClientID];
}

const char *CTraitManager::GetTraitIdByIndex(int Index) const
{
	if(Index < 0 || Index >= m_NumTraits)
		return nullptr;
	return m_aTraits[Index].m_aId;
}

void CTraitManager::GetCombatModifiers(CPlayer *pPlayer, float &DamageMul, float &ReloadMul) const
{
	DamageMul = 1.f;
	ReloadMul = 1.f;
	if(!pPlayer)
		return;
	const STraitDef *pDef = FindTrait(GetPlayerTrait(pPlayer->GetCID()));
	if(!pDef)
		return;
	DamageMul = pDef->m_DamageMul;
	ReloadMul = pDef->m_ReloadMul;
}

void CTraitManager::GetSpawnBonuses(CPlayer *pPlayer, int &ExtraHealth, int &ShieldBonus) const
{
	ExtraHealth = 0;
	ShieldBonus = 0;
	if(!pPlayer)
		return;
	const STraitDef *pDef = FindTrait(GetPlayerTrait(pPlayer->GetCID()));
	if(!pDef)
		return;
	ExtraHealth = pDef->m_MaxHealthBonus;
	ShieldBonus = pDef->m_ShieldBonus;
}

int CTraitManager::GetMiningLuckBonus(CPlayer *pPlayer) const
{
	const STraitDef *pDef = FindTrait(GetPlayerTrait(pPlayer ? pPlayer->GetCID() : -1));
	return pDef ? pDef->m_MiningLuckBonus : 0;
}

int CTraitManager::GetMineCdBonus(CPlayer *pPlayer) const
{
	const STraitDef *pDef = FindTrait(GetPlayerTrait(pPlayer ? pPlayer->GetCID() : -1));
	return pDef ? pDef->m_MineCdBonus : 0;
}

float CTraitManager::GetSkillCdMul(CPlayer *pPlayer) const
{
	const STraitDef *pDef = FindTrait(GetPlayerTrait(pPlayer ? pPlayer->GetCID() : -1));
	return pDef ? pDef->m_SkillCdMul : 1.f;
}

int CTraitManager::GetLifestealBonus(CPlayer *pPlayer) const
{
	const STraitDef *pDef = FindTrait(GetPlayerTrait(pPlayer ? pPlayer->GetCID() : -1));
	return pDef ? pDef->m_LifestealBonus : 0;
}
