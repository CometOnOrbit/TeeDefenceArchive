#include <engine/shared/jsonparser.h>

#include <game/commands.h>
#include <game/server/account.h>
#include <game/server/core/components/content/status_manager.h>
#include <game/server/core/components/content/trait_manager.h>
#include <game/server/core/components/skills/skill_manager.h>
#include <game/server/core/components/vote/vote_menu_manager.h>
#include <game/server/core/components/vote/vote_menu_types.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/entities/character.h>
#include <game/server/entities/turret.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/item_system.h>
#include <game/server/player.h>
#include <generated/server_data.h>

CSkillManager::CSkillManager()
{
	m_NumSkills = 0;
	mem_zero(m_aSkills, sizeof(m_aSkills));
	mem_zero(m_aaInstances, sizeof(m_aaInstances));
}

void CSkillManager::LoadSkills()
{
	m_NumSkills = 0;
	if(!Storage())
		return;

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseFile("server_content/skills.json", Storage());
	if(!pRoot)
	{
		dbg_msg("skills", "skills.json: %s", Parser.Error());
		return;
	}

	const json_value &Arr = (*pRoot)["skills"];
	if(Arr.type != json_array)
		return;

	for(unsigned i = 0; i < Arr.u.array.length && m_NumSkills < MAX_SKILLS; i++)
	{
		const json_value &S = Arr[(int)i];
		if(S.type != json_object)
			continue;
		int Id = -1;
		if(S["id"].type == json_integer)
			Id = (int)S["id"].u.integer;
		else if(S["id"].type == json_string)
			Id = i + 1;
		if(Id <= 0)
			continue;

		SSkillDescription &Def = m_aSkills[m_NumSkills++];
		mem_zero(&Def, sizeof(Def));
		Def.m_Id = Id;
		if(S["key"].type == json_string)
			str_copy(Def.m_aKey, S["key"].u.string.ptr, sizeof(Def.m_aKey));
		else if(S["id"].type == json_string)
			str_copy(Def.m_aKey, S["id"].u.string.ptr, sizeof(Def.m_aKey));
		Def.m_Passive = S["passive"].type == json_boolean && S["passive"].u.boolean != 0;
		Def.m_AutoLearn = S["auto_learn"].type == json_boolean && S["auto_learn"].u.boolean != 0;
		if(S["mana_cost_pct"].type == json_integer)
			Def.m_ManaCostPct = (int)S["mana_cost_pct"].u.integer;
		if(S["cooldown_ticks"].type == json_integer)
			Def.m_CooldownTicks = (int)S["cooldown_ticks"].u.integer;
		if(S["learn_cost_hearts"].type == json_integer)
			Def.m_LearnCostHearts = (int)S["learn_cost_hearts"].u.integer;
		const json_value &P = S["params"];
		if(P.type == json_object)
		{
			if(P["distance"].type == json_integer)
				Def.m_Distance = (int)P["distance"].u.integer;
			if(P["radius"].type == json_integer)
				Def.m_Radius = (int)P["radius"].u.integer;
			if(P["heal"].type == json_integer)
				Def.m_Heal = (int)P["heal"].u.integer;
			if(P["repair"].type == json_integer)
				Def.m_Repair = (int)P["repair"].u.integer;
		}
	}
	dbg_msg("skills", "loaded %d skills", m_NumSkills);
}

void CSkillManager::OnInitWorld(const char *pWhereLocalWorld)
{
	(void)pWhereLocalWorld;
	LoadSkills();
}

void CSkillManager::ResetClientSkills(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;
	mem_zero(m_aaInstances[ClientID], sizeof(m_aaInstances[ClientID]));
}

void CSkillManager::OnClientReset(int ClientID)
{
	ResetClientSkills(ClientID);
}

int CSkillManager::FindDescriptionIndex(int SkillId) const
{
	for(int i = 0; i < m_NumSkills; i++)
	{
		if(m_aSkills[i].m_Id == SkillId)
			return i;
	}
	return -1;
}

const SSkillDescription *CSkillManager::FindDescription(int SkillId) const
{
	const int Idx = FindDescriptionIndex(SkillId);
	return Idx >= 0 ? &m_aSkills[Idx] : nullptr;
}

SSkillInstance *CSkillManager::GetInstance(CPlayer *pPlayer, int SkillId)
{
	if(!pPlayer)
		return nullptr;
	const int Idx = FindDescriptionIndex(SkillId);
	if(Idx < 0)
		return nullptr;
	const int CID = pPlayer->GetCID();
	if(CID < 0 || CID >= MAX_CLIENTS)
		return nullptr;

	SSkillInstance &Inst = m_aaInstances[CID][Idx];
	if(Inst.m_SkillId != SkillId)
	{
		mem_zero(&Inst, sizeof(Inst));
		Inst.m_SkillId = SkillId;
		Inst.m_EmoticonBind = SKILL_EMOTICON_NONE;
	}
	return &Inst;
}

void CSkillManager::AutoLearnForPlayer(CPlayer *pPlayer)
{
	if(!pPlayer || pPlayer->IsDummy())
		return;
	for(int i = 0; i < m_NumSkills; i++)
	{
		if(!m_aSkills[i].m_AutoLearn)
			continue;
		if(SSkillInstance *pInst = GetInstance(pPlayer, m_aSkills[i].m_Id))
			pInst->m_Learned = true;
	}
}

void CSkillManager::OnPlayerLogin(CPlayer *pPlayer)
{
	AutoLearnForPlayer(pPlayer);
}

bool CSkillManager::Learn(CPlayer *pPlayer, int SkillId)
{
	if(!pPlayer || !GS())
		return false;
	const SSkillDescription *pDef = FindDescription(SkillId);
	if(!pDef || pDef->m_Passive)
		return false;
	SSkillInstance *pInst = GetInstance(pPlayer, SkillId);
	if(!pInst || pInst->m_Learned)
		return false;

	if(pDef->m_LearnCostHearts > 0)
	{
		if(pPlayer->m_AccData.m_aItems[ITEM_ZOMBIEHEART].m_Num < pDef->m_LearnCostHearts)
		{
			GS()->SendChatLoc(pPlayer->GetCID(), "skill.learn.need_hearts", u8"僵尸之心不足，无法学习技能。");
			return false;
		}
		pPlayer->m_AccData.m_aItems[ITEM_ZOMBIEHEART].m_Num -= pDef->m_LearnCostHearts;
		if(GS()->Accounts() && GS()->Accounts()->IsEnabled() && pPlayer->GetAccountId() >= 0)
			GS()->Accounts()->RequestSaveItems(pPlayer->GetCID());
	}

	pInst->m_Learned = true;
	char aKey[48];
	str_format(aKey, sizeof(aKey), "skill.%s", pDef->m_aKey);
	GS()->SendChatLocF(pPlayer->GetCID(), "skill.learned", u8"已学习技能：%s", GS()->Loc(pPlayer->GetCID(), aKey, pDef->m_aKey));
	return true;
}

void CSkillManager::CycleEmoticonBind(CPlayer *pPlayer, int SkillId)
{
	SSkillInstance *pInst = GetInstance(pPlayer, SkillId);
	if(!pInst || !pInst->m_Learned)
		return;

	if(pInst->m_EmoticonBind == SKILL_EMOTICON_NONE)
		pInst->m_EmoticonBind = 0;
	else if(pInst->m_EmoticonBind >= NUM_SKILL_EMOTICONS - 1)
		pInst->m_EmoticonBind = SKILL_EMOTICON_NONE;
	else
		pInst->m_EmoticonBind++;

	const SSkillDescription *pDef = FindDescription(SkillId);
	char aKey[48];
	if(pDef)
		str_format(aKey, sizeof(aKey), "skill.%s", pDef->m_aKey);
	else
		aKey[0] = 0;
	GS()->SendChatLocF(pPlayer->GetCID(), "skill.emote_bind", u8"[%s] 表情触发：%s",
		pDef ? GS()->Loc(pPlayer->GetCID(), aKey, pDef->m_aKey) : "?",
		SkillEmoticonName(pInst->m_EmoticonBind));

	if(GS() && GS()->Accounts() && GS()->Accounts()->IsEnabled() && pPlayer->GetAccountId() >= 0)
	{
		// Persist skill bind changes to account data.
		char aBinds[4096];
		SerializeSkillBindsForSave(pPlayer->GetCID(), aBinds, sizeof(aBinds));
		GS()->Accounts()->SetSkillBinds(pPlayer->GetCID(), aBinds);
		GS()->Accounts()->RequestSaveAccount(pPlayer->GetCID());
	}
}

int CSkillManager::GetEmoticonBindForClient(int ClientID, int SkillIdx) const
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || SkillIdx < 0 || SkillIdx >= m_NumSkills)
		return SKILL_EMOTICON_NONE;
	const SSkillInstance &Inst = m_aaInstances[ClientID][SkillIdx];
	if(Inst.m_SkillId != m_aSkills[SkillIdx].m_Id)
		return SKILL_EMOTICON_NONE;
	return Inst.m_EmoticonBind;
}

void CSkillManager::SetEmoticonBindForClient(int ClientID, int SkillIdx, int Bind)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || SkillIdx < 0 || SkillIdx >= m_NumSkills)
		return;
	SSkillInstance &Inst = m_aaInstances[ClientID][SkillIdx];
	if(Inst.m_SkillId != m_aSkills[SkillIdx].m_Id)
	{
		mem_zero(&Inst, sizeof(Inst));
		Inst.m_SkillId = m_aSkills[SkillIdx].m_Id;
	}
	Inst.m_EmoticonBind = clamp(Bind, -1, NUM_SKILL_EMOTICONS - 1);
}

void CSkillManager::RestoreSkillBinds(CPlayer *pPlayer)
{
	if(!pPlayer || !GS())
		return;
	const int CID = pPlayer->GetCID();
	CAccountSystem *pAcc = GS()->Accounts();
	if(!pAcc || !pAcc->IsEnabled())
		return;
	const char *pJson = pAcc->GetSkillBinds(CID);
	if(!pJson || !pJson[0])
		return;
	CJsonParser P;
	json_value *pRoot = P.ParseString(pJson);
	if(!pRoot || pRoot->type != json_object)
		return;
	for(unsigned i = 0; i < pRoot->u.object.length; i++)
	{
		const int SkillIdx = str_toint(pRoot->u.object.values[i].name);
		const int Idx = SkillIdx;
		if(Idx < 0 || Idx >= m_NumSkills)
			continue;
		const int Bind = (int)pRoot->u.object.values[i].value->u.integer;
		if(Bind < 0 || Bind >= NUM_SKILL_EMOTICONS)
			continue;
		SSkillInstance &Inst = m_aaInstances[CID][Idx];
		if(Inst.m_SkillId != m_aSkills[Idx].m_Id)
		{
			mem_zero(&Inst, sizeof(Inst));
			Inst.m_SkillId = m_aSkills[Idx].m_Id;
		}
		Inst.m_EmoticonBind = Bind;
	}
}

void CSkillManager::SerializeSkillBindsForSave(int ClientID, char *pOut, int OutLen) const
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || OutLen <= 2)
	{
		if(pOut && OutLen > 0)
		{
			pOut[0] = '{';
			if(OutLen > 1) pOut[1] = '}';
			if(OutLen > 2) pOut[2] = 0;
		}
		return;
	}
	char aBuf[4096];
	aBuf[0] = '{';
	aBuf[1] = 0;
	bool First = true;
	for(int i = 0; i < m_NumSkills; i++)
	{
		const SSkillInstance &Inst = m_aaInstances[ClientID][i];
		if(Inst.m_EmoticonBind < 0)
			continue;
		char aEntry[64];
		if(First)
			str_format(aEntry, sizeof(aEntry), "\"%d\":%d", i, Inst.m_EmoticonBind);
		else
			str_format(aEntry, sizeof(aEntry), ",\"%d\":%d", i, Inst.m_EmoticonBind);
		str_append(aBuf, aEntry, sizeof(aBuf));
		First = false;
	}
	str_append(aBuf, "}", sizeof(aBuf));
	str_copy(pOut, aBuf, OutLen);
}

bool CSkillManager::ExecuteSkill(CPlayer *pPlayer, const SSkillDescription &Def)
{
	if(!pPlayer || !GS())
		return false;
	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr || !pChr->IsAlive())
		return false;

	if(str_comp(Def.m_aKey, "dash") == 0 || str_comp(Def.m_aKey, "attack_teleport") == 0)
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		if(length(Dir) < 0.01f)
			Dir = vec2(1.f, 0.f);
		const float Speed = g_pData->m_Weapons.m_Ninja.m_Velocity;
		const int MoveTime = Speed > 0.01f ? maximum(1, (int)((float)maximum(32, Def.m_Distance) / Speed + 0.5f)) : 4;
		pChr->GiveNinja();
		pChr->DoNinjaFire(Dir, MoveTime);
		const int DurationTicks = g_pData->m_Weapons.m_Ninja.m_Duration * GS()->Server()->TickSpeed() / 1000;
		pChr->SetNinjaActivationTick(GS()->Server()->Tick() + MoveTime - DurationTicks - 1);
		GS()->m_World.CreateSound(pChr->GetPos(), SOUND_NINJA_FIRE);
		return true;
	}
	if(str_comp(Def.m_aKey, "heal_pulse") == 0 || str_comp(Def.m_aKey, "cure") == 0)
	{
		const vec2 Pos = pChr->GetPos();
		const float Radius = (float)maximum(64, Def.m_Radius);
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			CPlayer *pAlly = GS()->m_apPlayers[i];
			if(!pAlly || pAlly->IsDummy() || !pAlly->GetCharacter() || !pAlly->GetCharacter()->IsAlive())
				continue;
			if(distance(Pos, pAlly->GetCharacter()->GetPos()) <= Radius)
				pAlly->GetCharacter()->IncreaseHealth(maximum(1, Def.m_Heal));
		}
		return true;
	}
	if(str_comp(Def.m_aKey, "tower_repair_aura") == 0 || str_comp(Def.m_aKey, "heart_turret") == 0)
	{
		const vec2 Pos = pChr->GetPos();
		const float Radius = (float)maximum(96, Def.m_Radius);
		for(CGameWorld::TypeRange r = GS()->m_World.DoTypeRange(CGameWorld::ENTTYPE_TURRET); !r.empty(); r.pop_front())
		{
			CTurret *pT = static_cast<CTurret *>(r.front());
			if(!pT || pT->IsBroken())
				continue;
			if(distance(Pos, pT->GetPos()) <= Radius)
				pT->Repair();
		}
		return true;
	}
	if(str_comp(Def.m_aKey, "frost_nova") == 0 && Core() && Core()->StatusManager())
	{
		const vec2 Pos = pChr->GetPos();
		const float Radius = (float)maximum(96, Def.m_Radius);
		const int SlowTicks = GS()->Server()->TickSpeed() * 3;
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			CPlayer *pZ = GS()->m_apPlayers[i];
			if(!pZ || !pZ->IsDummy() || pZ->GetZomb() == ZOMB_NONE || !pZ->GetCharacter() || !pZ->GetCharacter()->IsAlive())
				continue;
			if(distance(Pos, pZ->GetCharacter()->GetPos()) <= Radius)
				Core()->StatusManager()->ApplyStatus(pZ->GetCharacter(), "frost", 1, SlowTicks, 0.55f);
		}
		return true;
	}
	if(str_comp(Def.m_aKey, "poison_cloud") == 0 && Core() && Core()->StatusManager())
	{
		const vec2 Pos = pChr->GetPos();
		const float Radius = (float)maximum(96, Def.m_Radius);
		const int Stacks = maximum(1, Def.m_Heal);
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			CPlayer *pZ = GS()->m_apPlayers[i];
			if(!pZ || !pZ->IsDummy() || pZ->GetZomb() == ZOMB_NONE || !pZ->GetCharacter() || !pZ->GetCharacter()->IsAlive())
				continue;
			if(distance(Pos, pZ->GetCharacter()->GetPos()) <= Radius)
				Core()->StatusManager()->ApplyStatus(pZ->GetCharacter(), "poison", Stacks, GS()->Server()->TickSpeed() * 5);
		}
		return true;
	}
	if(str_comp(Def.m_aKey, "battle_cry") == 0)
	{
		const vec2 Pos = pChr->GetPos();
		const float Radius = (float)maximum(96, Def.m_Radius);
		const int Heal = maximum(1, Def.m_Heal);
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			CPlayer *pAlly = GS()->m_apPlayers[i];
			if(!pAlly || pAlly->IsDummy() || !pAlly->GetCharacter() || !pAlly->GetCharacter()->IsAlive())
				continue;
			if(distance(Pos, pAlly->GetCharacter()->GetPos()) <= Radius)
				pAlly->GetCharacter()->IncreaseHealth(Heal);
		}
		return true;
	}
	if(str_comp(Def.m_aKey, "arcane_shield") == 0 && Core() && Core()->StatusManager())
	{
		const int Shield = maximum(4, Def.m_Heal);
		Core()->StatusManager()->ApplyStatus(pChr, "shield", 1, GS()->Server()->TickSpeed() * 8, 0.f, Shield);
		return true;
	}
	return false;
}

bool CSkillManager::Use(CPlayer *pPlayer, int SkillId)
{
	if(!pPlayer || !GS())
		return false;
	const SSkillDescription *pDef = FindDescription(SkillId);
	if(!pDef || pDef->m_Passive)
		return false;

	SSkillInstance *pInst = GetInstance(pPlayer, SkillId);
	if(!pInst || !pInst->m_Learned)
	{
		GS()->SendChatLoc(pPlayer->GetCID(), "skill.not_learned", u8"尚未学习该技能。");
		return false;
	}

	const int CID = pPlayer->GetCID();
	const int Tick = GS()->Server()->Tick();
	if(pInst->m_CooldownEnd > Tick)
	{
		GS()->SendChatLocF(CID, "skill.cooldown", u8"技能冷却中（%d 秒）",
			(pInst->m_CooldownEnd - Tick + GS()->Server()->TickSpeed() - 1) / GS()->Server()->TickSpeed());
		return false;
	}

	if(!ExecuteSkill(pPlayer, *pDef))
		return false;

	float CdMul = 1.f;
	if(Core() && Core()->TraitManager())
		CdMul = Core()->TraitManager()->GetSkillCdMul(pPlayer);
	pInst->m_CooldownEnd = Tick + maximum(1, (int)(pDef->m_CooldownTicks * CdMul + 0.5f));
	char aKey[48];
	str_format(aKey, sizeof(aKey), "skill.%s", pDef->m_aKey);
	GS()->SendChatLocF(CID, "skill.used", u8"已释放技能：%s", GS()->Loc(CID, aKey, pDef->m_aKey));
	return true;
}

void CSkillManager::UseSkillsByEmoticon(CPlayer *pPlayer, int EmoticonId)
{
	if(!pPlayer || pPlayer->IsDummy() || !pPlayer->GetCharacter())
		return;
	for(int i = 0; i < m_NumSkills; i++)
	{
		const int SkillId = m_aSkills[i].m_Id;
		SSkillInstance *pInst = GetInstance(pPlayer, SkillId);
		if(!pInst || !pInst->m_Learned || pInst->m_EmoticonBind != EmoticonId)
			continue;
		if(m_aSkills[i].m_Passive)
			continue;
		Use(pPlayer, SkillId);
	}
}

int CSkillManager::ResolveSkillIdFromArg(const char *pArg) const
{
	if(!pArg || !pArg[0])
		return -1;
	for(int i = 0; i < m_NumSkills; i++)
	{
		if(str_comp(m_aSkills[i].m_aKey, pArg) == 0)
			return m_aSkills[i].m_Id;
	}
	return str_toint(pArg);
}

void CSkillManager::BuildSkillsListPage(int ClientID)
{
	if(!GS() || !Core() || !Core()->VoteMenuManager())
		return;

	CVoteMenuManager *pVote = Core()->VoteMenuManager();
	CPlayer *pP = GS()->m_apPlayers[ClientID];
	pVote->SetVoteBuildClientID(ClientID);
	pVote->AddVote_PageHeader(GS()->Loc(ClientID, "skill.menu.title", u8"技能"));
	pVote->AddVote_Separator();

	for(int i = 0; i < m_NumSkills; i++)
	{
		const SSkillDescription &Def = m_aSkills[i];
		char aKey[48];
		str_format(aKey, sizeof(aKey), "skill.%s", Def.m_aKey);
		const char *pName = GS()->Loc(ClientID, aKey, Def.m_aKey);
		SSkillInstance *pInst = pP ? GetInstance(pP, Def.m_Id) : nullptr;
		char aLine[VOTE_DESC_LENGTH];
		if(pInst && pInst->m_Learned)
			str_format(aLine, sizeof(aLine), GS()->Loc(ClientID, "skill.entry.learned", u8"▹ %s ✓"), pName);
		else
			str_format(aLine, sizeof(aLine), GS()->Loc(ClientID, "skill.entry", u8"▹ %s"), pName);
		char aCmd[48];
		str_format(aCmd, sizeof(aCmd), "ccv_menuskillsel %d", Def.m_Id);
		pVote->AddVote(aLine, aCmd, ClientID);
	}
	pVote->AddVote_PageFooter();
}

void CSkillManager::BuildSkillDetailPage(int ClientID, int SkillId)
{
	if(!GS() || !Core() || !Core()->VoteMenuManager())
		return;

	const SSkillDescription *pDef = FindDescription(SkillId);
	CPlayer *pP = GS()->m_apPlayers[ClientID];
	if(!pDef || !pP)
		return;

	CVoteMenuManager *pVote = Core()->VoteMenuManager();
	SSkillInstance *pInst = GetInstance(pP, SkillId);
	const int Tick = GS()->Server()->Tick();

	char aKey[48];
	str_format(aKey, sizeof(aKey), "skill.%s", pDef->m_aKey);
	char aDescKey[56];
	str_format(aDescKey, sizeof(aDescKey), "skill.%s.desc", pDef->m_aKey);

	pVote->SetVoteBuildClientID(ClientID);
	pVote->AddVote_PageHeader(GS()->Loc(ClientID, aKey, pDef->m_aKey));
	pVote->AddVote_PageSubtitle(GS()->Loc(ClientID, aDescKey, pDef->m_aKey));
	if(!pDef->m_Passive && pInst && pInst->m_Learned)
	{
		char aMana[64];
		str_format(aMana, sizeof(aMana), GS()->Loc(ClientID, "skill.mana_pct", u8"法力消耗：%d%%"), pDef->m_ManaCostPct);
		pVote->AddVote_TextLine(aMana);
	}
	pVote->AddVote_Separator();

	if(pInst && pInst->m_Learned)
	{
		if(!pDef->m_Passive)
		{
			char aBind[128];
			str_format(aBind, sizeof(aBind), GS()->Loc(ClientID, "skill.bind_hint", u8"绑定：bind 'F1' say \"/use_skill %d\""), SkillId);
			pVote->AddVote_TextLine(aBind);
			char aEmo[128];
			str_format(aEmo, sizeof(aEmo), GS()->Loc(ClientID, "skill.emote_current", u8"表情触发：%s"), SkillEmoticonName(pInst->m_EmoticonBind));
			pVote->AddVote_TextLine(aEmo);
			char aCmdEmo[48];
			str_format(aCmdEmo, sizeof(aCmdEmo), "ccv_skillemote %d", SkillId);
			pVote->AddVote(GS()->Loc(ClientID, "skill.change_emote", u8"切换表情绑定"), aCmdEmo, ClientID);

			if(pInst->m_CooldownEnd <= Tick)
			{
				char aUseCmd[48];
				str_format(aUseCmd, sizeof(aUseCmd), "ccv_skilluse %d", SkillId);
				pVote->AddVote(GS()->Loc(ClientID, "skill.cast", u8"☝ 立即释放"), aUseCmd, ClientID);
			}
		}
	}
	else if(!pDef->m_AutoLearn)
	{
		char aLearn[128];
		str_format(aLearn, sizeof(aLearn), GS()->Loc(ClientID, "skill.learn_btn", u8"☆ 学习（需僵尸之心 ×%d）"), pDef->m_LearnCostHearts);
		char aLearnCmd[48];
		str_format(aLearnCmd, sizeof(aLearnCmd), "ccv_skilllearn %d", SkillId);
		pVote->AddVote(aLearn, aLearnCmd, ClientID);
	}
	else
	{
		pVote->AddVote_EmptyHint(GS()->Loc(ClientID, "skill.auto_learn", u8"登录后自动习得"));
	}

	pVote->AddVote_PageFooter();
}

static void ComChatUseSkill(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = static_cast<CCommandManager::SCommandContext *>(pUser);
	CGameContext *pGame = static_cast<CGameContext *>(pCtx->m_pContext);
	if(!pGame || !pGame->Core() || !pGame->Core()->SkillManager() || pResult->NumArguments() < 1)
		return;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP)
		return;
	const int SkillId = pGame->Core()->SkillManager()->ResolveSkillIdFromArg(pResult->GetString(0));
	if(SkillId > 0)
		pGame->Core()->SkillManager()->Use(pP, SkillId);
}

static void ComVoteSkillSel(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = static_cast<CCommandManager::SCommandContext *>(pUser);
	CGameContext *pGame = static_cast<CGameContext *>(pCtx->m_pContext);
	if(!pGame || !pGame->Core() || !pGame->Core()->VoteMenuManager())
		return;
	SPlayerVote *pV = pGame->Core()->VoteMenuManager()->GetPlayerVote(pCtx->m_ClientID);
	pV->m_SkillId = pResult->GetInteger(0);
	pV->m_Page = PAGE_SKILL_SELECT;
	pV->m_LastPage = PAGE_SKILLS;
	pGame->Core()->VoteMenuManager()->ClearVotes(pCtx->m_ClientID);
}

static void ComVoteSkillLearn(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = static_cast<CCommandManager::SCommandContext *>(pUser);
	CGameContext *pGame = static_cast<CGameContext *>(pCtx->m_pContext);
	if(!pGame || !pGame->Core() || !pGame->Core()->SkillManager())
		return;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP)
		return;
	const int SkillId = pResult->GetInteger(0);
	if(pGame->Core()->SkillManager()->Learn(pP, SkillId))
	{
		SPlayerVote *pV = pGame->Core()->VoteMenuManager()->GetPlayerVote(pCtx->m_ClientID);
		pV->m_SkillId = SkillId;
		pV->m_Page = PAGE_SKILL_SELECT;
		pGame->Core()->VoteMenuManager()->ClearVotes(pCtx->m_ClientID);
	}
}

static void ComVoteSkillEmote(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = static_cast<CCommandManager::SCommandContext *>(pUser);
	CGameContext *pGame = static_cast<CGameContext *>(pCtx->m_pContext);
	if(!pGame || !pGame->Core() || !pGame->Core()->SkillManager())
		return;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP)
		return;
	const int SkillId = pResult->GetInteger(0);
	pGame->Core()->SkillManager()->CycleEmoticonBind(pP, SkillId);
	SPlayerVote *pV = pGame->Core()->VoteMenuManager()->GetPlayerVote(pCtx->m_ClientID);
	pV->m_SkillId = SkillId;
	pV->m_Page = PAGE_SKILL_SELECT;
	pGame->Core()->VoteMenuManager()->ClearVotes(pCtx->m_ClientID);
}

static void ComVoteSkillUse(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = static_cast<CCommandManager::SCommandContext *>(pUser);
	CGameContext *pGame = static_cast<CGameContext *>(pCtx->m_pContext);
	if(!pGame || !pGame->Core() || !pGame->Core()->SkillManager())
		return;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(pP)
		pGame->Core()->SkillManager()->Use(pP, pResult->GetInteger(0));
}

void CSkillManager::RegisterChatCommands(CCommandManager *pMgr)
{
	if(!pMgr || !GS())
		return;
	pMgr->AddCommand("use_skill", "cmd.use_skill.help", "r", ComChatUseSkill, GS());
	pMgr->AddCommand("skill", "cmd.skill.help", "r", ComChatUseSkill, GS());
}

void CSkillManager::RegisterVoteCommands(CCommandManager *pMgr)
{
	if(!pMgr || !GS())
		return;
	pMgr->AddCommand("menuskillsel", "", "i", ComVoteSkillSel, GS());
	pMgr->AddCommand("skilllearn", "", "i", ComVoteSkillLearn, GS());
	pMgr->AddCommand("skillemote", "", "i", ComVoteSkillEmote, GS());
	pMgr->AddCommand("skilluse", "", "i", ComVoteSkillUse, GS());
}
