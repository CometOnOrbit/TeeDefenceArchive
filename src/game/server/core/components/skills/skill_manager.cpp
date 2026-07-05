#include <engine/shared/jsonparser.h>

#include <game/commands.h>
#include <game/server/account.h>
#include <game/server/core/components/content/status_manager.h>
#include <game/server/core/components/content/trait_manager.h>
#include <game/server/core/components/skills/skill_manager.h>
#include <game/server/core/components/vote/vote_menu_manager.h>
#include <game/server/core/components/vote/vote_menu_types.h>
#include <game/server/core/components/vote/vote_wrapper.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/entities/character.h>
#include <game/server/entities/growingexplosion.h>
#include <game/server/entities/skills/skill_spawn.h>
#include <game/server/entities/skills/skill_detonation_beam.h>
#include <game/server/entities/skills/skill_magic_bolt.h>
#include <game/server/entities/skills/skill_cast_ring.h>
#include <game/server/entities/skills/skill_target_query.h>
#include <game/server/entities/turret.h>
#include <game/server/gamecontext.h>
#include <game/server/interaction_sound.h>
#include <generated/server_data.h>
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
		if(S["name"].type == json_string)
			str_copy(Def.m_aName, S["name"].u.string.ptr, sizeof(Def.m_aName));
		else
			str_copy(Def.m_aName, Def.m_aKey, sizeof(Def.m_aName));
		Def.m_Passive = S["passive"].type == json_boolean && S["passive"].u.boolean != 0;
		Def.m_AutoLearn = S["auto_learn"].type == json_boolean && S["auto_learn"].u.boolean != 0;
		if(S["mana_cost_pct"].type == json_integer)
			Def.m_ManaCostPct = (int)S["mana_cost_pct"].u.integer;
		if(S["cooldown_ticks"].type == json_integer)
			Def.m_CooldownTicks = (int)S["cooldown_ticks"].u.integer;
		if(S["learn_cost_hearts"].type == json_integer)
			Def.m_LearnCostHearts = (int)S["learn_cost_hearts"].u.integer;
		if(S["category"].type == json_string)
			Def.m_Category = SkillCategoryFromName(S["category"].u.string.ptr);
		const json_value &P = S["params"];
		if(P.type == json_object)
		{
			if(P["distance"].type == json_integer)
				Def.m_Distance = (int)P["distance"].u.integer;
			if(P["radius"].type == json_integer)
				Def.m_Radius = (int)P["radius"].u.integer;
			if(P["heal"].type == json_integer)
				Def.m_Heal = (int)P["heal"].u.integer;
			if(P["damage"].type == json_integer)
				Def.m_Damage = (int)P["damage"].u.integer;
			if(P["repair"].type == json_integer)
				Def.m_Repair = (int)P["repair"].u.integer;
		}
		Def.m_MaxLevel = SKILL_DEFAULT_MAX_LEVEL;
		Def.m_UsesPerLevel = SKILL_DEFAULT_USES_PER_LEVEL;
		if(S["max_level"].type == json_integer)
			Def.m_MaxLevel = maximum(1, (int)S["max_level"].u.integer);
		if(S["uses_per_level"].type == json_integer)
			Def.m_UsesPerLevel = maximum(1, (int)S["uses_per_level"].u.integer);
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

const SSkillInstance *CSkillManager::GetInstance(CPlayer *pPlayer, int SkillId) const
{
	return const_cast<CSkillManager *>(this)->GetInstance(pPlayer, SkillId);
}

int CSkillManager::GetSkillLevel(CPlayer *pPlayer, int SkillId) const
{
	const SSkillInstance *pInst = GetInstance(pPlayer, SkillId);
	if(!pInst || !pInst->m_Learned)
		return 1;
	return maximum(1, pInst->m_Level);
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
		{
			pInst->m_Learned = true;
			if(pInst->m_Level <= 0)
				pInst->m_Level = 1;
		}
	}
}

void CSkillManager::OnPlayerLogin(CPlayer *pPlayer)
{
	AutoLearnForPlayer(pPlayer);
	if(pPlayer && pPlayer->GetCharacter())
		pPlayer->GetCharacter()->RefillMana();
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
			GS()->SendChatLoc(pPlayer->GetCID(), "skill.learn.need_hearts", "僵尸之心不足，无法学习魔法。");
			return false;
		}
		pPlayer->m_AccData.m_aItems[ITEM_ZOMBIEHEART].m_Num -= pDef->m_LearnCostHearts;
		if(GS()->Accounts() && GS()->Accounts()->IsEnabled() && pPlayer->GetAccountId() >= 0)
			GS()->Accounts()->RequestSaveItems(pPlayer->GetCID());
	}

	pInst->m_Learned = true;
	pInst->m_Level = 1;
	pInst->m_UseCount = 0;

	// Auto-bind to first available emote slot (frequent operations use emote, not commands)
	{
		bool aUsed[NUM_SKILL_EMOTICONS] = {false};
		for(int si = 0; si < m_NumSkills; si++)
		{
			const int SID = m_aSkills[si].m_Id;
			SSkillInstance *pOther = GetInstance(pPlayer, SID);
			if(pOther && pOther->m_Learned && pOther->m_EmoticonBind >= 0 && pOther->m_EmoticonBind < NUM_SKILL_EMOTICONS)
				aUsed[pOther->m_EmoticonBind] = true;
		}
		for(int bi = 0; bi < NUM_SKILL_EMOTICONS; bi++)
		{
			if(!aUsed[bi])
			{
				pInst->m_EmoticonBind = bi;
				char aEmoName[64];
				str_format(aEmoName, sizeof(aEmoName), "（表情槽 %d: %s）", bi + 1, SkillEmoticonName(bi));
				GS()->SendChatLoc(pPlayer->GetCID(), "skill.auto_bind", "已自动绑定到表情：");
				GS()->SendChatTo(pPlayer->GetCID(), aEmoName);
				break;
			}
		}
	}

	char aKey[48];
	str_format(aKey, sizeof(aKey), "skill.%s", pDef->m_aKey);
	GS()->SendChatLocF(pPlayer->GetCID(), "skill.learned", "已学习魔法：%s", GS()->Loc(pPlayer->GetCID(), aKey, pDef->m_aKey));
	RequestSaveSkillProgress(pPlayer->GetCID());
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
	GS()->SendChatLocF(pPlayer->GetCID(), "skill.emote_bind", "[%s] 表情触发：%s",
		pDef ? GS()->Loc(pPlayer->GetCID(), aKey, pDef->m_aKey) : "?",
		SkillEmoticonName(pInst->m_EmoticonBind));

	if(GS() && GS()->Accounts() && GS()->Accounts()->IsEnabled() && pPlayer->GetAccountId() >= 0)
		RequestSaveSkillProgress(pPlayer->GetCID());
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

	const bool NewFormat = (*pRoot)["b"].type == json_object || (*pRoot)["l"].type == json_object ||
		(*pRoot)["u"].type == json_object || (*pRoot)["k"].type == json_object;

	auto EnsureInst = [&](int Idx) -> SSkillInstance * {
		if(Idx < 0 || Idx >= m_NumSkills)
			return nullptr;
		SSkillInstance &Inst = m_aaInstances[CID][Idx];
		if(Inst.m_SkillId != m_aSkills[Idx].m_Id)
		{
			mem_zero(&Inst, sizeof(Inst));
			Inst.m_SkillId = m_aSkills[Idx].m_Id;
			Inst.m_EmoticonBind = SKILL_EMOTICON_NONE;
		}
		return &Inst;
	};

	auto ParseIdxIntObj = [&](const json_value &Obj) {
		if(Obj.type != json_object)
			return;
		for(unsigned i = 0; i < Obj.u.object.length; i++)
		{
			const int Idx = str_toint(Obj.u.object.values[i].name);
			if(Obj.u.object.values[i].value->type != json_integer)
				continue;
			const int Val = (int)Obj.u.object.values[i].value->u.integer;
			if(Val < 0 || Val >= NUM_SKILL_EMOTICONS || Idx < 0 || Idx >= m_NumSkills)
				continue;
			if(SSkillInstance *pInst = EnsureInst(Idx))
				pInst->m_EmoticonBind = Val;
		}
	};

	auto ParseIdxLevelObj = [&](const json_value &Obj) {
		if(Obj.type != json_object)
			return;
		for(unsigned i = 0; i < Obj.u.object.length; i++)
		{
			const int Idx = str_toint(Obj.u.object.values[i].name);
			if(Obj.u.object.values[i].value->type != json_integer)
				continue;
			const int Val = (int)Obj.u.object.values[i].value->u.integer;
			if(Idx < 0 || Idx >= m_NumSkills)
				continue;
			if(SSkillInstance *pInst = EnsureInst(Idx))
				pInst->m_Level = maximum(1, Val);
		}
	};

	auto ParseIdxUsesObj = [&](const json_value &Obj) {
		if(Obj.type != json_object)
			return;
		for(unsigned i = 0; i < Obj.u.object.length; i++)
		{
			const int Idx = str_toint(Obj.u.object.values[i].name);
			if(Obj.u.object.values[i].value->type != json_integer)
				continue;
			const int Val = (int)Obj.u.object.values[i].value->u.integer;
			if(Idx < 0 || Idx >= m_NumSkills)
				continue;
			if(SSkillInstance *pInst = EnsureInst(Idx))
				pInst->m_UseCount = maximum(0, Val);
		}
	};

	auto ParseIdxLearnedObj = [&](const json_value &Obj) {
		if(Obj.type != json_object)
			return;
		for(unsigned i = 0; i < Obj.u.object.length; i++)
		{
			const int Idx = str_toint(Obj.u.object.values[i].name);
			if(Obj.u.object.values[i].value->type != json_integer)
				continue;
			const int Val = (int)Obj.u.object.values[i].value->u.integer;
			if(Val <= 0 || Idx < 0 || Idx >= m_NumSkills)
				continue;
			if(SSkillInstance *pInst = EnsureInst(Idx))
			{
				pInst->m_Learned = true;
				if(pInst->m_Level <= 0)
					pInst->m_Level = 1;
			}
		}
	};

	if(NewFormat)
	{
		ParseIdxIntObj((*pRoot)["b"]);
		ParseIdxLevelObj((*pRoot)["l"]);
		ParseIdxUsesObj((*pRoot)["u"]);
		ParseIdxLearnedObj((*pRoot)["k"]);
		return;
	}

	for(unsigned i = 0; i < pRoot->u.object.length; i++)
	{
		const int SkillIdx = str_toint(pRoot->u.object.values[i].name);
		if(SkillIdx < 0 || SkillIdx >= m_NumSkills)
			continue;
		if(pRoot->u.object.values[i].value->type != json_integer)
			continue;
		const int Bind = (int)pRoot->u.object.values[i].value->u.integer;
		if(Bind < 0 || Bind >= NUM_SKILL_EMOTICONS)
			continue;
		if(SSkillInstance *pInst = EnsureInst(SkillIdx))
			pInst->m_EmoticonBind = Bind;
	}
}

void CSkillManager::RequestSaveSkillProgress(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !GS() || !GS()->Accounts())
		return;
	CPlayer *pPlayer = GS()->m_apPlayers[ClientID];
	if(!pPlayer)
		return;
	if(pPlayer->ShouldDeferSkillProgressSave(false))
		return;

	char aBuf[4096];
	SerializeSkillBindsForSave(ClientID, aBuf, sizeof(aBuf));
	GS()->Accounts()->SetSkillBinds(ClientID, aBuf);
	GS()->Accounts()->RequestSaveAccount(ClientID);
	pPlayer->MarkSkillProgressSaved();
}

static void AppendSkillJsonObject(char *pBuf, int BufSize, const char *pKey, const char *pBody, bool &NeedComma)
{
	if(!pBody || !pBody[0])
		return;
	char aPart[1200];
	str_format(aPart, sizeof(aPart), "%s\"%s\":{%s}", NeedComma ? "," : "", pKey, pBody);
	str_append(pBuf, aPart, BufSize);
	NeedComma = true;
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

	char aBinds[1024], aLevels[1024], aUses[1024], aLearned[512];
	aBinds[0] = aLevels[0] = aUses[0] = aLearned[0] = 0;

	auto AppendEntry = [](char *pSection, int SectionSize, int Idx, int Val, bool &First) {
		char aEntry[48];
		str_format(aEntry, sizeof(aEntry), "%s\"%d\":%d", First ? "" : ",", Idx, Val);
		str_append(pSection, aEntry, SectionSize);
		First = false;
	};

	bool First = true;
	for(int i = 0; i < m_NumSkills; i++)
	{
		const SSkillInstance &Inst = m_aaInstances[ClientID][i];
		if(Inst.m_EmoticonBind < 0)
			continue;
		AppendEntry(aBinds, sizeof(aBinds), i, Inst.m_EmoticonBind, First);
	}

	First = true;
	for(int i = 0; i < m_NumSkills; i++)
	{
		const SSkillInstance &Inst = m_aaInstances[ClientID][i];
		if(Inst.m_Level <= 1)
			continue;
		AppendEntry(aLevels, sizeof(aLevels), i, Inst.m_Level, First);
	}

	First = true;
	for(int i = 0; i < m_NumSkills; i++)
	{
		const SSkillInstance &Inst = m_aaInstances[ClientID][i];
		if(Inst.m_UseCount <= 0)
			continue;
		AppendEntry(aUses, sizeof(aUses), i, Inst.m_UseCount, First);
	}

	First = true;
	for(int i = 0; i < m_NumSkills; i++)
	{
		const SSkillInstance &Inst = m_aaInstances[ClientID][i];
		if(!Inst.m_Learned || m_aSkills[i].m_AutoLearn)
			continue;
		AppendEntry(aLearned, sizeof(aLearned), i, 1, First);
	}

	char aBuf[4096];
	aBuf[0] = '{';
	aBuf[1] = 0;
	bool NeedComma = false;
	AppendSkillJsonObject(aBuf, sizeof(aBuf), "b", aBinds, NeedComma);
	AppendSkillJsonObject(aBuf, sizeof(aBuf), "l", aLevels, NeedComma);
	AppendSkillJsonObject(aBuf, sizeof(aBuf), "u", aUses, NeedComma);
	AppendSkillJsonObject(aBuf, sizeof(aBuf), "k", aLearned, NeedComma);
	str_append(aBuf, "}", sizeof(aBuf));
	str_copy(pOut, aBuf, OutLen);
}

void CSkillManager::RecordSkillUse(CPlayer *pPlayer, int SkillId)
{
	if(!pPlayer || !GS())
		return;
	SSkillInstance *pInst = GetInstance(pPlayer, SkillId);
	const SSkillDescription *pDef = FindDescription(SkillId);
	if(!pInst || !pDef || !pInst->m_Learned || pDef->m_Passive)
		return;

	const int MaxLevel = maximum(1, pDef->m_MaxLevel);
	if(pInst->m_Level <= 0)
		pInst->m_Level = 1;
	if(pInst->m_Level >= MaxLevel)
		return;

	const int CombatWindow = maximum(1, GS()->Server()->TickSpeed() * SKILL_COMBAT_RECENCY_SEC);
	if(pPlayer->m_LastCombatTick <= 0 || GS()->Server()->Tick() - pPlayer->m_LastCombatTick > CombatWindow)
		return;
	if(!pPlayer->TryConsumeSkillProficiency())
		return;

	pInst->m_UseCount++;
	const int Required = SkillUsesRequiredForLevel(pInst->m_Level, pDef->m_UsesPerLevel);
	const bool LeveledUp = pInst->m_UseCount >= Required;
	if(LeveledUp)
	{
		pInst->m_UseCount = 0;
		pInst->m_Level++;

		char aKey[48];
		str_format(aKey, sizeof(aKey), "skill.%s", pDef->m_aKey);
		GS()->SendChatLocF(pPlayer->GetCID(), "skill.level_up", "魔法升级：%s Lv%d",
			GS()->Loc(pPlayer->GetCID(), aKey, pDef->m_aKey), pInst->m_Level);
		if(CCharacter *pChr = pPlayer->GetCharacter())
			GS()->m_World.CreateSound(pChr->GetPos(), SOUND_SFX_UPGRADE, CmaskOne(pPlayer->GetCID()));
	}

	if(GS()->Accounts() && GS()->Accounts()->IsEnabled() && pPlayer->GetAccountId() >= 0)
	{
		if(LeveledUp || !pPlayer->ShouldDeferSkillProgressSave(false))
			RequestSaveSkillProgress(pPlayer->GetCID());
	}
}

bool CSkillManager::ExecuteSkill(CPlayer *pPlayer, const SSkillDescription &DefIn)
{
	if(!pPlayer || !GS())
		return false;
	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr || !pChr->IsAlive())
		return false;

	SSkillDescription Def = DefIn;
	const int SkillLevel = GetSkillLevel(pPlayer, Def.m_Id);
	if(SkillLevel > 1)
	{
		if(Def.m_Damage > 0)
			Def.m_Damage = SkillScaleInt(Def.m_Damage, SkillLevel);
		if(Def.m_Heal > 0)
			Def.m_Heal = SkillScaleInt(Def.m_Heal, SkillLevel);
		if(Def.m_Radius > 0)
			Def.m_Radius = SkillScaleInt(Def.m_Radius, SkillLevel);
		if(Def.m_Distance > 0)
			Def.m_Distance = SkillScaleInt(Def.m_Distance, SkillLevel);
		if(Def.m_Repair > 0)
			Def.m_Repair = SkillScaleInt(Def.m_Repair, SkillLevel);
	}

	// ─── Dash (敏捷→距离) ─────────────────────────────────
	if(str_comp(Def.m_aKey, "dash") == 0 || str_comp(Def.m_aKey, "attack_teleport") == 0)
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		if(length(Dir) < 0.01f)
			Dir = vec2(1.f, 0.f);
		const float Speed = g_pData->m_Weapons.m_Ninja.m_Velocity;
		// DEX scaling: each DEX adds 2 units to dash distance
		const int DexBonus = pPlayer->GetStat(AttributeIdentifier::DEX) * 2;
		const int MoveTime = Speed > 0.01f ? maximum(1, (int)((float)maximum(32, Def.m_Distance + DexBonus) / Speed + 0.5f)) : 4;
		pChr->GiveNinja();
		pChr->DoNinjaFire(Dir, MoveTime);
		const int DurationTicks = g_pData->m_Weapons.m_Ninja.m_Duration * GS()->Server()->TickSpeed() / 1000;
		pChr->SetNinjaActivationTick(GS()->Server()->Tick() + MoveTime - DurationTicks - 1);
		const vec2 From = pChr->GetPos();
		const float DashDist = (float)maximum(32, Def.m_Distance + DexBonus);
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, From, 48.f);
		SpawnSkillDashTrail(&GS()->m_World, From, From + Dir * DashDist, SKILL_VFX_ARCANE, MoveTime);
		GS()->m_World.CreateSound(pChr->GetPos(), SOUND_NINJA_FIRE);
		return true;
	}

	// ─── Battle Cry (魅力→范围/治疗) ────────────────────────
	if(str_comp(Def.m_aKey, "battle_cry") == 0)
	{
		const vec2 Pos = pChr->GetPos();
		// CHA: 每点魅力+8范围, +1治疗
		const int ChaBonus = pPlayer->GetStat(AttributeIdentifier::CHA);
		const float Radius = (float)maximum(96, Def.m_Radius + ChaBonus * 8);
		const int Heal = maximum(1, Def.m_Heal + ChaBonus);
		const int BuffTicks = GS()->Server()->TickSpeed() * 6;
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, Pos, Radius);
		ForEachAllyInRadius(&GS()->m_World, Pos, Radius, true, [&](CCharacter *pAllyChr, CPlayer *pAlly) {
			pAllyChr->IncreaseHealth(Heal);
			Core()->StatusManager()->ApplyStatus(pAllyChr, "atk_boost", 1, BuffTicks, 1.0f, 2);
			SpawnSkillFollowAura(&GS()->m_World, pAlly->GetCID(), BuffTicks, SKILL_VFX_HOLY, minimum(Radius * 0.35f, 56.f));
			return true;
		});
		GS()->m_World.CreateSound(Pos, SOUND_GRENADE_EXPLODE);
		return true;
	}

	// ─── Frost Nova (智力→伤害/冻结时长) ────────────────────
	if(str_comp(Def.m_aKey, "frost_nova") == 0 && Core() && Core()->StatusManager())
	{
		const vec2 Pos = pChr->GetPos();
		const float Radius = (float)maximum(96, Def.m_Radius);
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, Pos, Radius);
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const int SlowTicks = GS()->Server()->TickSpeed() * 3 + IntVal * 2;
		const int FreezeDamage = 8 + IntVal * 3;
		SpawnSkillZoneAura(&GS()->m_World, Pos, SlowTicks, SKILL_VFX_FROST, Radius * 0.5f);
		ForEachHostileInRadius(&GS()->m_World, GS(), pPlayer, Pos, Radius, [&](CCharacter *pTargetChr) {
			if(Core()->StatusManager()->GetSlowTicks(pTargetChr) > 0)
			{
				pTargetChr->TakeDamage(vec2(0, 0), Pos, FreezeDamage, pPlayer->GetCID(), WEAPON_GAME);
				SpawnSkillHitBurst(&GS()->m_World, pTargetChr->GetPos(), 48.f, SKILL_VFX_FROST);
				GS()->m_World.CreateSound(pTargetChr->GetPos(), SOUND_GRENADE_EXPLODE);
			}
			else
			{
				Core()->StatusManager()->ApplyStatus(pTargetChr, "frost", 1, SlowTicks, 0.55f);
				SpawnSkillHitBurst(&GS()->m_World, pTargetChr->GetPos(), 40.f, SKILL_VFX_FROST);
			}
			return true;
		});
		return true;
	}

	// ─── Arcane Shield (智力→吸收量) ────────────────────────
	if(str_comp(Def.m_aKey, "arcane_shield") == 0 && Core() && Core()->StatusManager())
	{
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const int Shield = maximum(4, Def.m_Heal + IntVal * 2);
		const int ShieldTicks = GS()->Server()->TickSpeed() * 6;
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, pChr->GetPos(), 56.f);
		SpawnSkillFollowAura(&GS()->m_World, pPlayer->GetCID(), ShieldTicks, SKILL_VFX_HOLY, 56.f);
		Core()->StatusManager()->ApplyStatus(pChr, "shield", 1, ShieldTicks, 0.f, Shield);
		GS()->m_World.CreateSound(pChr->GetPos(), SOUND_PICKUP_ARMOR);
		return true;
	}

	// ─── Blink (智力→传送距离) ────────────────────────────────
	if(str_comp(Def.m_aKey, "blink") == 0)
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		if(length(Dir) < 0.01f)
			Dir = vec2(1.f, 0.f);
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const float Distance = (float)maximum(64, Def.m_Distance + IntVal * 4);
		const vec2 OldPos = pChr->GetPos();
		vec2 NewPos = OldPos + Dir * Distance;
		// Clamp to map bounds
		CCollision *pColl = GS()->Collision();
		if(pColl)
		{
			NewPos.x = clamp(NewPos.x, 16.0f, (float)(pColl->GetWidth() * 32 - 16));
			NewPos.y = clamp(NewPos.y, 16.0f, (float)(pColl->GetHeight() * 32 - 16));
		}
		pChr->SetCharacterPos(NewPos);
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, NewPos, 0.f, OldPos, NewPos);
		SpawnSkillDashTrail(&GS()->m_World, OldPos, NewPos, SKILL_VFX_ARCANE, GS()->Server()->TickSpeed() / 2);
		return true;
	}

	// ─── Renew (智慧→治疗量) ────────────────────────────────
	if(str_comp(Def.m_aKey, "renew") == 0)
	{
		const int WisVal = pPlayer->GetStat(AttributeIdentifier::WIS);
		const int HealPerTick = maximum(1, Def.m_Heal + WisVal);
		const int TotalTicks = GS()->Server()->TickSpeed() * 5;
		pChr->m_RenewTicks = TotalTicks;
		pChr->m_RenewAmount = HealPerTick;
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, pChr->GetPos(), 64.f);
		SpawnSkillFollowAura(&GS()->m_World, pPlayer->GetCID(), TotalTicks, SKILL_VFX_HEAL, 56.f);
		GS()->m_World.CreateSound(pChr->GetPos(), SOUND_PICKUP_HEALTH);
		return true;
	}

	// ─── Iron Will (体质→持续时间) ───────────────────────────
	if(str_comp(Def.m_aKey, "iron_will") == 0)
	{
		const int ConVal = pPlayer->GetStat(AttributeIdentifier::CON);
		const int DurationTicks = GS()->Server()->TickSpeed() * (3 + ConVal / 10);
		pChr->m_IronWillTicks = DurationTicks;
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, pChr->GetPos(), 56.f);
		SpawnSkillFollowAura(&GS()->m_World, pPlayer->GetCID(), DurationTicks, SKILL_VFX_PHYSICAL, 52.f);
		Core()->StatusManager()->ApplyStatus(pChr, "frost", 1, DurationTicks, 0.70f);
		GS()->m_World.CreateSound(pChr->GetPos(), SOUND_PICKUP_ARMOR);
		return true;
	}

	// ─── Ground Slam (力量→伤害/击退) ────────────────────────
	if(str_comp(Def.m_aKey, "ground_slam") == 0)
	{
		const vec2 Pos = pChr->GetPos();
		const float Radius = (float)maximum(96, Def.m_Radius);
		const int StrVal = pPlayer->GetStat(AttributeIdentifier::STR);
		const int BaseDmg = maximum(2, Def.m_Heal) + StrVal * 3;
		const int KnockbackForce = 10 + StrVal / 2;
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, Pos, Radius);
		SpawnSkillZoneAura(&GS()->m_World, Pos, GS()->Server()->TickSpeed() * 2, SKILL_VFX_PHYSICAL, Radius * 0.45f);
		ForEachHostileInRadius(&GS()->m_World, GS(), pPlayer, Pos, Radius, [&](CCharacter *pTargetChr) {
			const vec2 TargetPos = pTargetChr->GetPos();
			vec2 KnockDir = normalize(TargetPos - Pos);
			if(length(KnockDir) < 0.01f)
				KnockDir = vec2(1.f, 0.f);
			const float Dist = distance(Pos, TargetPos);
			pTargetChr->TakeDamage(KnockDir * (float)KnockbackForce * (1.0f - Dist / Radius),
				Pos, BaseDmg, pPlayer->GetCID(), WEAPON_HAMMER);
			return true;
		});
		SpawnSkillHitBurst(&GS()->m_World, Pos, Radius * 0.5f, SKILL_VFX_PHYSICAL);
		GS()->m_World.CreateSound(Pos, SOUND_HAMMER_FIRE);
		return true;
	}

	// ─── Entangle (智力→藤蔓追踪弹) ────────────────────────────
	if(str_comp(Def.m_aKey, "entangle") == 0)
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		if(length(Dir) < 0.01f)
			Dir = vec2(1.f, 0.f);
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const float MaxDist = (float)maximum(128, Def.m_Distance + IntVal * 3);
		const vec2 Start = pChr->GetPos();
		CCollision *pColl = GS()->Collision();
		if(!pColl)
			return false;
		CCharacter *pHit = FindNearestSkillHostile(GS(), pPlayer, Start, MaxDist, Dir, 0.707f, pColl);
		const int HitCID = SkillHostileCID(pHit);
		SpawnSkillMagicBolt(&GS()->m_World, pPlayer->GetCID(), Start + Dir * 24.f, Dir, 1, SKILL_BOLT_ENTANGLE, HitCID);
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, Start, 48.f);
		SpawnSkillFollowAura(&GS()->m_World, pPlayer->GetCID(), GS()->Server()->TickSpeed() / 2, SKILL_VFX_POISON, 40.f);
		GS()->m_World.CreateSound(Start, SOUND_HOOK_LOOP);
		return true;
	}

	// ─── Shadow Step (敏捷→隐身时长+暴击倍率) ───────────────
	if(str_comp(Def.m_aKey, "shadow_step") == 0)
	{
		const int DexVal = pPlayer->GetStat(AttributeIdentifier::DEX);
		const int DurationTicks = GS()->Server()->TickSpeed() * (2 + DexVal / 5);
		pChr->m_ShadowTicks = DurationTicks;
		pChr->m_IsInvisible = true;
		pChr->m_ShadowNextCrit = true;
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, pChr->GetPos(), 64.f);
		SpawnSkillFollowAura(&GS()->m_World, pPlayer->GetCID(), DurationTicks, SKILL_VFX_SHADOW, 60.f);
		GS()->m_World.CreateSound(pChr->GetPos(), SOUND_WEAPON_SPAWN);
		return true;
	}

	// ─── Enlighten (智慧→治疗+增益时长) ──────────────────────
	if(str_comp(Def.m_aKey, "enlighten") == 0 && Core() && Core()->StatusManager())
	{
		const vec2 Pos = pChr->GetPos();
		const int WisVal = pPlayer->GetStat(AttributeIdentifier::WIS);
		const float Radius = (float)maximum(96, Def.m_Radius + WisVal * 8);
		const int Heal = 2 + WisVal;
		const int DefBuffTicks = GS()->Server()->TickSpeed() * (4 + WisVal / 5);
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, Pos, Radius);
		ForEachAllyInRadius(&GS()->m_World, Pos, Radius, false, [&](CCharacter *pAllyChr, CPlayer *pAlly) {
			pAllyChr->IncreaseHealth(Heal);
			Core()->StatusManager()->ClearDebuffs(pAllyChr);
			Core()->StatusManager()->ApplyStatus(pAllyChr, "atk_boost", 1, DefBuffTicks, 1.0f, 2);
			SpawnSkillFollowAura(&GS()->m_World, pAlly->GetCID(), DefBuffTicks, SKILL_VFX_HOLY, minimum(Radius * 0.35f, 56.f));
			return true;
		});
		GS()->m_World.CreateSound(Pos, SOUND_PICKUP_HEALTH);
		return true;
	}

	// ─── Fireball (智力→飞行火球实体) ───────────────────────────
	if(str_comp(Def.m_aKey, "fireball") == 0)
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		if(length(Dir) < 0.01f)
			Dir = vec2(1.f, 0.f);
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const int BaseDmg = maximum(4, Def.m_Damage + IntVal * 2);
		const vec2 SpawnPos = pChr->GetPos() + Dir * 28.f;
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, pChr->GetPos(), 48.f);
		SpawnSkillFollowAura(&GS()->m_World, pPlayer->GetCID(), GS()->Server()->TickSpeed() / 2, SKILL_VFX_FIRE, 40.f);
		SpawnSkillFireball(&GS()->m_World, pPlayer->GetCID(), SpawnPos, Dir, BaseDmg, true);
		return true;
	}

	// ─── Chain Lightning (智力→Tesla 链式实体) ─────────────────
	if(str_comp(Def.m_aKey, "chain_lightning") == 0)
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		if(length(Dir) < 0.01f)
			Dir = vec2(1.f, 0.f);
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const float Radius = (float)maximum(120, Def.m_Radius);
		const int BaseDmg = maximum(3, Def.m_Damage + IntVal * 2);
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, pChr->GetPos(), Radius);
		SpawnSkillFollowAura(&GS()->m_World, pPlayer->GetCID(), GS()->Server()->TickSpeed() / 2, SKILL_VFX_ARCANE, 44.f);
		SpawnSkillChainLightning(&GS()->m_World, pPlayer->GetCID(), pChr->GetPos(), Dir, BaseDmg, Radius, 3, 0.65f);
		return true;
	}

	// ─── Poison Mist (智力→毒雾实体) ─────────────────────────────
	if(str_comp(Def.m_aKey, "poison_mist") == 0)
	{
		const vec2 Pos = pChr->GetPos();
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const float Radius = (float)maximum(120, Def.m_Radius);
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, Pos, Radius);
		SpawnSkillPoisonCloud(&GS()->m_World, pPlayer->GetCID(), Pos, Radius, 1 + IntVal / 8);
		return true;
	}

	// ─── Holy Light (智慧→自身治疗) ─────────────────────────────
	if(str_comp(Def.m_aKey, "holy_light") == 0)
	{
		const int WisVal = pPlayer->GetStat(AttributeIdentifier::WIS);
		const int Heal = maximum(4, Def.m_Heal + WisVal * 2);
		pChr->IncreaseHealth(Heal);
		SpawnSkillHitBurst(&GS()->m_World, pChr->GetPos(), 48.f, SKILL_VFX_HEAL);
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, pChr->GetPos(), 64.f);
		SpawnSkillFollowAura(&GS()->m_World, pPlayer->GetCID(), GS()->Server()->TickSpeed() * 2, SKILL_VFX_HEAL, 56.f);
		GS()->m_World.CreateSound(pChr->GetPos(), SOUND_PICKUP_HEALTH);
		return true;
	}

	// ─── Blessing (魅力→范围治疗+攻击增益) ─────────────────────
	if(str_comp(Def.m_aKey, "blessing") == 0 && Core() && Core()->StatusManager())
	{
		const vec2 Pos = pChr->GetPos();
		const int ChaVal = pPlayer->GetStat(AttributeIdentifier::CHA);
		const float Radius = (float)maximum(120, Def.m_Radius + ChaVal * 6);
		const int Heal = maximum(2, Def.m_Heal + ChaVal);
		const int BuffTicks = GS()->Server()->TickSpeed() * 5;
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, Pos, Radius);
		ForEachAllyInRadius(&GS()->m_World, Pos, Radius, true, [&](CCharacter *pAllyChr, CPlayer *pAlly) {
			pAllyChr->IncreaseHealth(Heal);
			Core()->StatusManager()->ApplyStatus(pAllyChr, "atk_boost", 1, BuffTicks, 1.0f, 2 + ChaVal / 3);
			SpawnSkillFollowAura(&GS()->m_World, pAlly->GetCID(), BuffTicks, SKILL_VFX_HOLY, minimum(Radius * 0.35f, 56.f));
			return true;
		});
		GS()->m_World.CreateSound(Pos, SOUND_PICKUP_HEALTH);
		return true;
	}

	// ─── Rage (力量→自身攻击强化) ───────────────────────────────
	if(str_comp(Def.m_aKey, "rage") == 0 && Core() && Core()->StatusManager())
	{
		const int StrVal = pPlayer->GetStat(AttributeIdentifier::STR);
		const int BuffTicks = GS()->Server()->TickSpeed() * (5 + StrVal / 6);
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, pChr->GetPos(), 72.f);
		SpawnSkillFollowAura(&GS()->m_World, pPlayer->GetCID(), BuffTicks, SKILL_VFX_PHYSICAL, 64.f);
		Core()->StatusManager()->ApplyStatus(pChr, "atk_boost", 1, BuffTicks, 1.0f, 3 + StrVal / 2);
		return true;
	}

	// ─── Haste (敏捷→移动加速) ─────────────────────────────────
	if(str_comp(Def.m_aKey, "haste") == 0 && Core() && Core()->StatusManager())
	{
		const int DexVal = pPlayer->GetStat(AttributeIdentifier::DEX);
		const int BuffTicks = GS()->Server()->TickSpeed() * (4 + DexVal / 5);
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, pChr->GetPos(), 56.f);
		SpawnSkillFollowAura(&GS()->m_World, pPlayer->GetCID(), BuffTicks, SKILL_VFX_ARCANE, 52.f);
		Core()->StatusManager()->ApplyStatus(pChr, "haste", 1 + DexVal / 8, BuffTicks, 1.0f);
		return true;
	}

	// ─── Life Drain (智力→追踪吸血弹) ────────────────────────────
	if(str_comp(Def.m_aKey, "life_drain") == 0)
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		if(length(Dir) < 0.01f)
			Dir = vec2(1.f, 0.f);
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const float MaxDist = (float)maximum(128, Def.m_Distance + IntVal * 3);
		const int BaseDmg = maximum(3, Def.m_Damage + IntVal * 2);
		const vec2 Start = pChr->GetPos();
		const int HitCID = SkillHostileCID(FindNearestSkillHostile(GS(), pPlayer, Start, MaxDist));
		SpawnSkillMagicBolt(&GS()->m_World, pPlayer->GetCID(), Start + Dir * 24.f, Dir, BaseDmg, SKILL_BOLT_LIFE_DRAIN, HitCID);
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, Start, 48.f);
		SpawnSkillFollowAura(&GS()->m_World, pPlayer->GetCID(), GS()->Server()->TickSpeed() / 2, SKILL_VFX_SHADOW, 40.f);
		return true;
	}

	// ─── Thunder Clap (力量→范围震慑) ───────────────────────────
	if(str_comp(Def.m_aKey, "thunder_clap") == 0 && Core() && Core()->StatusManager())
	{
		const vec2 Pos = pChr->GetPos();
		const float Radius = (float)maximum(120, Def.m_Radius);
		const int StrVal = pPlayer->GetStat(AttributeIdentifier::STR);
		const int BaseDmg = maximum(2, Def.m_Damage + StrVal * 2);
		const int RootTicks = GS()->Server()->TickSpeed() * (1 + StrVal / 10);
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, Pos, Radius);
		SpawnSkillZoneAura(&GS()->m_World, Pos, GS()->Server()->TickSpeed() * 2, SKILL_VFX_PHYSICAL, Radius * 0.4f);
		ForEachHostileInRadius(&GS()->m_World, GS(), pPlayer, Pos, Radius, [&](CCharacter *pTargetChr) {
			pTargetChr->TakeDamage(vec2(0, 0), Pos, BaseDmg, pPlayer->GetCID(), WEAPON_HAMMER);
			Core()->StatusManager()->ApplyStatus(pTargetChr, "frost", 1, RootTicks, 0.08f);
			return true;
		});
		SpawnSkillHitBurst(&GS()->m_World, Pos, Radius * 0.45f, SKILL_VFX_PHYSICAL);
		GS()->m_World.CreateSound(Pos, SOUND_HAMMER_FIRE);
		return true;
	}

	// ─── Arcane Missiles (智力→三枚追踪飞弹实体) ────────────────
	if(str_comp(Def.m_aKey, "arcane_missiles") == 0)
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		if(length(Dir) < 0.01f)
			Dir = vec2(1.f, 0.f);
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const float MaxDist = (float)maximum(160, Def.m_Distance + IntVal * 3);
		const int MissileDmg = maximum(2, Def.m_Damage / 3 + IntVal);
		const vec2 Start = pChr->GetPos();
		const int HitCID = SkillHostileCID(FindNearestSkillHostile(GS(), pPlayer, Start, MaxDist));
		SpawnSkillArcaneMissiles(&GS()->m_World, pPlayer->GetCID(), Start + Dir * 24.f, Dir, MissileDmg, HitCID);
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, Start, 48.f);
		SpawnSkillFollowAura(&GS()->m_World, pPlayer->GetCID(), GS()->Server()->TickSpeed() / 2, SKILL_VFX_ARCANE, 40.f);
		return true;
	}

	// ─── Dispel Wave (智慧→净化负面状态) ────────────────────────
	if(str_comp(Def.m_aKey, "dispel_wave") == 0 && Core() && Core()->StatusManager())
	{
		const vec2 Pos = pChr->GetPos();
		const int WisVal = pPlayer->GetStat(AttributeIdentifier::WIS);
		const float Radius = (float)maximum(120, Def.m_Radius + WisVal * 6);
		const int Heal = maximum(1, Def.m_Heal + WisVal / 2);
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, Pos, Radius);
		SpawnSkillZoneAura(&GS()->m_World, Pos, GS()->Server()->TickSpeed() * 2, SKILL_VFX_HOLY, Radius * 0.45f);
		ForEachAllyInRadius(&GS()->m_World, Pos, Radius, true, [&](CCharacter *pAllyChr, CPlayer *) {
			Core()->StatusManager()->ClearDebuffs(pAllyChr);
			pAllyChr->IncreaseHealth(Heal);
			return true;
		});
		GS()->m_World.CreateSound(Pos, SOUND_PICKUP_ARMOR);
		return true;
	}

	// ─── Meteor Strike (智力→延迟陨石轰击) ───────────────────────
	if(str_comp(Def.m_aKey, "meteor_strike") == 0)
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		if(length(Dir) < 0.01f)
			Dir = vec2(1.f, 0.f);
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const float CastDist = (float)maximum(160, Def.m_Distance + IntVal * 4);
		const float Radius = (float)maximum(120, Def.m_Radius + IntVal * 3);
		const int Dmg = maximum(8, Def.m_Damage + IntVal * 3);
		vec2 TargetPos = pChr->GetPos() + Dir * CastDist;
		CCollision *pColl = GS()->Collision();
		if(pColl)
		{
			TargetPos.x = clamp(TargetPos.x, 16.0f, (float)(pColl->GetWidth() * 32 - 16));
			TargetPos.y = clamp(TargetPos.y, 16.0f, (float)(pColl->GetHeight() * 32 - 16));
		}
		const int WarningTicks = GS()->Server()->TickSpeed() + IntVal / 2;
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, TargetPos, Radius);
		SpawnSkillMeteor(&GS()->m_World, pPlayer->GetCID(), TargetPos, Dmg, Radius, WarningTicks);
		return true;
	}

	// ─── Celestial Beam (智慧→旋转圣光扫射) ─────────────────────
	if(str_comp(Def.m_aKey, "celestial_beam") == 0)
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		if(length(Dir) < 0.01f)
			Dir = vec2(1.f, 0.f);
		const int WisVal = pPlayer->GetStat(AttributeIdentifier::WIS);
		const float BeamLength = (float)maximum(240, Def.m_Distance + WisVal * 5);
		const int Dmg = maximum(4, Def.m_Damage + WisVal * 2);
		const int Duration = GS()->Server()->TickSpeed() + WisVal / 2;
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, pChr->GetPos(), 96.f);
		SpawnSkillFollowAura(&GS()->m_World, pPlayer->GetCID(), Duration, SKILL_VFX_HOLY, 72.f);
		SpawnSkillBeamSweep(&GS()->m_World, pPlayer->GetCID(), pChr->GetPos(), angle(Dir), BeamLength, Dmg, Duration);
		return true;
	}

	// ─── Void Rift (智力→虚空漩涡牵引+爆发) ─────────────────────
	if(str_comp(Def.m_aKey, "void_rift") == 0 || str_comp(Def.m_aKey, "black_hole") == 0)
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		if(length(Dir) < 0.01f)
			Dir = vec2(1.f, 0.f);
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const float CastDist = (float)maximum(128, Def.m_Distance + IntVal * 3);
		const float Radius = (float)maximum(140, Def.m_Radius + IntVal * 4);
		const int Dmg = maximum(6, Def.m_Damage + IntVal * 2);
		vec2 TargetPos = pChr->GetPos() + Dir * CastDist;
		CCollision *pColl = GS()->Collision();
		if(pColl)
		{
			TargetPos.x = clamp(TargetPos.x, 16.0f, (float)(pColl->GetWidth() * 32 - 16));
			TargetPos.y = clamp(TargetPos.y, 16.0f, (float)(pColl->GetHeight() * 32 - 16));
		}
		const int PullTicks = GS()->Server()->TickSpeed() + IntVal / 3;
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, TargetPos, Radius);
		SpawnSkillVoidVortex(&GS()->m_World, pPlayer->GetCID(), TargetPos, Radius, Dmg, PullTicks);
		return true;
	}

	// ─── Phoenix Flame (智力→烈焰冲击波) ─────────────────────────
	if(str_comp(Def.m_aKey, "phoenix_flame") == 0 && Core() && Core()->StatusManager())
	{
		const vec2 Pos = pChr->GetPos();
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const float Radius = (float)maximum(140, Def.m_Radius + IntVal * 4);
		const int BurnDmg = maximum(6, Def.m_Damage + IntVal * 2);
		const int BurnDuration = GS()->Server()->TickSpeed() * 5;
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, Pos, Radius);
		SpawnSkillZoneAura(&GS()->m_World, Pos, BurnDuration, SKILL_VFX_FIRE, Radius * 0.5f);
		ForEachHostileInRadius(&GS()->m_World, GS(), pPlayer, Pos, Radius, [&](CCharacter *pTargetChr) {
			vec2 KnockDir = normalize(pTargetChr->GetPos() - Pos);
			if(length(KnockDir) < 0.01f)
				KnockDir = vec2(1.f, 0.f);
			pTargetChr->TakeDamage(KnockDir * 10.f, Pos, BurnDmg, pPlayer->GetCID(), WEAPON_GUN);
			Core()->StatusManager()->ApplyStatus(pTargetChr, "burn", 2, GS()->Server()->TickSpeed() * 5);
			SpawnSkillHitBurst(&GS()->m_World, pTargetChr->GetPos(), 56.f, SKILL_VFX_FIRE);
			return true;
		});
		SpawnSkillHitBurst(&GS()->m_World, Pos, Radius * 0.6f, SKILL_VFX_FIRE, 0, 16);
		GS()->m_World.CreateSound(Pos, SOUND_GRENADE_EXPLODE);
		return true;
	}

	// ─── Starfall (智力→星陨弹幕) ────────────────────────────────
	if(str_comp(Def.m_aKey, "starfall") == 0)
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		if(length(Dir) < 0.01f)
			Dir = vec2(1.f, 0.f);
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const float CastDist = (float)maximum(160, Def.m_Distance + IntVal * 4);
		const float Spread = (float)maximum(120, Def.m_Radius);
		const int BoltDmg = maximum(3, Def.m_Damage / 2 + IntVal);
		const vec2 Center = pChr->GetPos() + Dir * CastDist;
		const int NumBolts = 5 + IntVal / 8;
		const int MarkerTicks = GS()->Server()->TickSpeed() * 2;
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, Center, Spread);
		SpawnSkillZoneAura(&GS()->m_World, Center, MarkerTicks, SKILL_VFX_ARCANE, Spread * 0.45f);
		for(int i = 0; i < NumBolts; i++)
		{
			const float OffX = ((float)(random_int() % 200) / 100.f - 1.f) * Spread;
			const float OffY = ((float)(random_int() % 200) / 100.f - 1.f) * Spread * 0.6f;
			const vec2 BoltPos = Center + vec2(OffX, OffY);
			const vec2 BoltDir = vec2(0.f, 1.f);
			SpawnSkillMagicBolt(&GS()->m_World, pPlayer->GetCID(), BoltPos + vec2(0.f, -96.f), BoltDir, BoltDmg, SKILL_BOLT_DAMAGE, -1);
		}
		return true;
	}

	// ─── Thunderstorm (智力→连环雷暴) ───────────────────────────
	if(str_comp(Def.m_aKey, "thunderstorm") == 0)
	{
		const vec2 Pos = pChr->GetPos();
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const float Radius = (float)maximum(200, Def.m_Radius + IntVal * 5);
		const int BaseDmg = maximum(4, Def.m_Damage + IntVal * 2);
		const int StormTicks = GS()->Server()->TickSpeed() * 2;
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, Pos, Radius);
		SpawnSkillZoneAura(&GS()->m_World, Pos, StormTicks, SKILL_VFX_ARCANE, Radius * 0.4f);
		int Strikes = 0;
		ForEachHostileInRadius(&GS()->m_World, GS(), pPlayer, Pos, Radius, [&](CCharacter *pTargetChr) {
			if(Strikes >= 4)
				return false;
			const vec2 TargetPos = pTargetChr->GetPos();
			const vec2 StrikeDir = normalize(TargetPos - Pos);
			SpawnSkillChainLightning(&GS()->m_World, pPlayer->GetCID(), TargetPos, StrikeDir, BaseDmg, 180.f, 2, 0.75f);
			Strikes++;
			return true;
		});
		if(Strikes == 0)
			SpawnSkillChainLightning(&GS()->m_World, pPlayer->GetCID(), Pos, vec2(1.f, 0.f), BaseDmg, Radius, 3, 0.7f);
		return true;
	}

	// ─── Arcane Vortex (智力→奥术旋涡飞弹雨) ────────────────────
	if(str_comp(Def.m_aKey, "arcane_vortex") == 0)
	{
		const vec2 Pos = pChr->GetPos();
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const float Radius = (float)maximum(180, Def.m_Radius + IntVal * 4);
		const int MissileDmg = maximum(2, Def.m_Damage / 3 + IntVal);
		const int VortexTicks = GS()->Server()->TickSpeed() * 2;
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, Pos, Radius);
		SpawnSkillZoneAura(&GS()->m_World, Pos, VortexTicks, SKILL_VFX_ARCANE, Radius * 0.35f);
		ForEachHostileInRadius(&GS()->m_World, GS(), pPlayer, Pos, Radius, [&](CCharacter *pTargetChr) {
			CPlayer *pTarget = pTargetChr->GetPlayer();
			if(!pTarget)
				return true;
			const vec2 MissileDir = normalize(pTargetChr->GetPos() - Pos);
			SpawnSkillArcaneMissiles(&GS()->m_World, pPlayer->GetCID(), Pos + MissileDir * 32.f, MissileDir, MissileDmg, pTarget->GetCID());
			return true;
		});
		return true;
	}

	// ─── Forked Lightning (智力→分叉闪电) ───────────────────────
	if(str_comp(Def.m_aKey, "forked_lightning") == 0)
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		if(length(Dir) < 0.01f)
			Dir = vec2(1.f, 0.f);
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const float Range = (float)maximum(280, Def.m_Distance + IntVal * 4);
		const int BaseDmg = maximum(4, Def.m_Damage + IntVal * 2);
		const int MaxForks = 2 + IntVal / 12;
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, pChr->GetPos(), 64.f);
		SpawnSkillFollowAura(&GS()->m_World, pPlayer->GetCID(), GS()->Server()->TickSpeed() / 2, SKILL_VFX_ARCANE, 44.f);
		SpawnSkillForkLightning(&GS()->m_World, pPlayer->GetCID(), pChr->GetPos(), Dir, BaseDmg, Range, MaxForks);
		return true;
	}

	// ─── Electro Arc (智力→分形电弧) ────────────────────────────
	if(str_comp(Def.m_aKey, "electro_arc") == 0)
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		if(length(Dir) < 0.01f)
			Dir = vec2(1.f, 0.f);
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const float CastDist = (float)maximum(200, Def.m_Distance + IntVal * 4);
		const int BaseDmg = maximum(5, Def.m_Damage + IntVal * 2);
		const vec2 From = pChr->GetPos();
		vec2 To = From + Dir * CastDist;
		CCollision *pColl = GS()->Collision();
		if(pColl)
			pColl->IntersectLine(From, To, 0x0, &To);
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, To, 72.f);
		SpawnSkillFollowAura(&GS()->m_World, pPlayer->GetCID(), GS()->Server()->TickSpeed() / 2, SKILL_VFX_ARCANE, 40.f);
		SpawnSkillElectroArc(&GS()->m_World, pPlayer->GetCID(), From, To, BaseDmg);
		return true;
	}

	// ─── Super Nova (智力→扩散爆炸环) ───────────────────────────
	if(str_comp(Def.m_aKey, "super_nova") == 0)
	{
		const vec2 Pos = pChr->GetPos();
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const int BaseDmg = maximum(5, Def.m_Damage + IntVal * 2);
		const int MaxRings = 3 + IntVal / 10;
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, Pos, (float)maximum(160, Def.m_Radius));
		SpawnSkillSuperNova(&GS()->m_World, pPlayer->GetCID(), Pos, BaseDmg, MaxRings);
		return true;
	}

	// ─── Smoke Veil (智力→烟雾遮蔽) ─────────────────────────────
	if(str_comp(Def.m_aKey, "smoke_veil") == 0)
	{
		const vec2 Pos = pChr->GetPos();
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const float Radius = (float)maximum(160, Def.m_Radius + IntVal * 4);
		const int Duration = GS()->Server()->TickSpeed() * 2 + IntVal / 2;
		const int SlowTicks = GS()->Server()->TickSpeed() * 2;
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, Pos, Radius);
		SpawnSkillSmokeVeil(&GS()->m_World, pPlayer->GetCID(), Pos, Radius, Duration, SlowTicks, 0.45f);
		return true;
	}

	// ─── Death Spiral (智力→死亡螺旋激光) ───────────────────────
	if(str_comp(Def.m_aKey, "death_spiral") == 0)
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		if(length(Dir) < 0.01f)
			Dir = vec2(1.f, 0.f);
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const int BaseDmg = maximum(3, Def.m_Damage + IntVal);
		const int Duration = GS()->Server()->TickSpeed() + IntVal / 4;
		const float BeamLength = (float)maximum(240, Def.m_Distance + IntVal * 4);
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, pChr->GetPos(), 80.f);
		SpawnSkillFollowAura(&GS()->m_World, pPlayer->GetCID(), Duration, SKILL_VFX_ARCANE, 68.f);
		SpawnSkillSpinLaser(&GS()->m_World, pPlayer->GetCID(), angle(Dir), 48.f, BeamLength, BaseDmg, Duration);
		return true;
	}

	// ─── Plasma Field (智力→等离子电场) ─────────────────────────
	if(str_comp(Def.m_aKey, "plasma_field") == 0)
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const int Radius = maximum(6, Def.m_Radius > 0 ? Def.m_Radius / 32 : 8);
		const int Dmg = maximum(4, Def.m_Damage + IntVal * 2);
		const vec2 Pos = pChr->GetPos();
		const int GrowTicks = GS()->Server()->TickSpeed() * 2;
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, Pos, (float)Radius * 32.f);
		SpawnSkillZoneAura(&GS()->m_World, Pos, GrowTicks, SKILL_VFX_ARCANE, (float)Radius * 28.f);
		new CGrowingExplosion(&GS()->m_World, Pos, Dir, pPlayer->GetCID(), Radius, GROWINGEXPLOSIONEFFECT_ELECTRIC, false, GE_TARGET_MMO_HOSTILE, Dmg);
		return true;
	}

	// ─── Flash Wave (智力→寒霜冲击波) ─────────────────────────────
	if(str_comp(Def.m_aKey, "flash_wave") == 0)
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const int Radius = maximum(6, (Def.m_Radius > 0 ? Def.m_Radius / 32 : 8) + IntVal / 16);
		const vec2 Pos = pChr->GetPos();
		const int GrowTicks = GS()->Server()->TickSpeed() * 2;
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, Pos, (float)Radius * 32.f);
		SpawnSkillZoneAura(&GS()->m_World, Pos, GrowTicks, SKILL_VFX_FROST, (float)Radius * 28.f);
		new CGrowingExplosion(&GS()->m_World, Pos, Dir, pPlayer->GetCID(), Radius, GROWINGEXPLOSIONEFFECT_FREEZE, false, GE_TARGET_MMO_HOSTILE);
		return true;
	}

	// ─── Toxic Bloom (智力→剧毒绽放) ─────────────────────────────
	if(str_comp(Def.m_aKey, "toxic_bloom") == 0)
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const int Radius = maximum(6, Def.m_Radius > 0 ? Def.m_Radius / 32 : 8);
		const int Stacks = maximum(1, 1 + IntVal / 8);
		const vec2 Pos = pChr->GetPos();
		const int GrowTicks = GS()->Server()->TickSpeed() * 2;
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, Pos, (float)Radius * 32.f);
		SpawnSkillZoneAura(&GS()->m_World, Pos, GrowTicks, SKILL_VFX_POISON, (float)Radius * 28.f);
		new CGrowingExplosion(&GS()->m_World, Pos, Dir, pPlayer->GetCID(), Radius, GROWINGEXPLOSIONEFFECT_POISON, false, GE_TARGET_MMO_HOSTILE, Stacks);
		return true;
	}

	// ─── Healing Mist (智慧→治愈之雾) ─────────────────────────────
	if(str_comp(Def.m_aKey, "healing_mist") == 0)
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		const int WisVal = pPlayer->GetStat(AttributeIdentifier::WIS);
		const int Radius = maximum(6, Def.m_Radius > 0 ? Def.m_Radius / 32 : 8);
		const int Heal = maximum(2, Def.m_Heal + WisVal / 2);
		const vec2 Pos = pChr->GetPos();
		const int GrowTicks = GS()->Server()->TickSpeed() * 2;
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, Pos, (float)Radius * 32.f);
		SpawnSkillZoneAura(&GS()->m_World, Pos, GrowTicks, SKILL_VFX_HEAL, (float)Radius * 28.f);
		new CGrowingExplosion(&GS()->m_World, Pos, Dir, pPlayer->GetCID(), Radius, GROWINGEXPLOSIONEFFECT_HEAL, false, GE_TARGET_MMO_ALLY, Heal);
		GS()->m_World.CreateSound(Pos, SOUND_PICKUP_HEALTH);
		return true;
	}

	// ─── Gravity Well (智力→白洞牵引+爆发) ─────────────────────────
	if(str_comp(Def.m_aKey, "gravity_well") == 0)
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		if(length(Dir) < 0.01f)
			Dir = vec2(1.f, 0.f);
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const float CastDist = (float)maximum(128, Def.m_Distance + IntVal * 3);
		const float Radius = (float)maximum(140, Def.m_Radius + IntVal * 4);
		const int Dmg = maximum(6, Def.m_Damage + IntVal * 2);
		const int ExplosionRadiusTiles = maximum(4, (Def.m_Radius > 0 ? Def.m_Radius / 32 : 6) + IntVal / 12);
		vec2 TargetPos = pChr->GetPos() + Dir * CastDist;
		CCollision *pColl = GS()->Collision();
		if(pColl)
		{
			TargetPos.x = clamp(TargetPos.x, 16.0f, (float)(pColl->GetWidth() * 32 - 16));
			TargetPos.y = clamp(TargetPos.y, 16.0f, (float)(pColl->GetHeight() * 32 - 16));
		}
		const int PullTicks = (int)(GS()->Server()->TickSpeed() * 2.5f);
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, TargetPos, Radius);
		SpawnSkillGravityWell(&GS()->m_World, pPlayer->GetCID(), TargetPos, Radius, PullTicks, ExplosionRadiusTiles, Dmg);
		return true;
	}

	// ─── Scatter Blast (智力→散射榴弹) ───────────────────────────
	if(str_comp(Def.m_aKey, "scatter_blast") == 0)
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		if(length(Dir) < 0.01f)
			Dir = vec2(1.f, 0.f);
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const int Dmg = maximum(4, Def.m_Damage + IntVal * 2);
		const int ExplosionRadiusTiles = maximum(3, (Def.m_Radius > 0 ? Def.m_Radius / 32 : 4) + IntVal / 16);
		const vec2 SpawnPos = pChr->GetPos() + Dir * 32.f;
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, SpawnPos, 64.f);
		SpawnSkillFollowAura(&GS()->m_World, pPlayer->GetCID(), GS()->Server()->TickSpeed() / 2, SKILL_VFX_FIRE, 40.f);
		SpawnSkillScatterBlast(&GS()->m_World, pPlayer->GetCID(), SpawnPos, Dir, Dmg, ExplosionRadiusTiles, 3);
		return true;
	}

	// ─── Detonation Beam (智力→爆炸激光) ─────────────────────────
	if(str_comp(Def.m_aKey, "detonation_beam") == 0)
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		if(length(Dir) < 0.01f)
			Dir = vec2(1.f, 0.f);
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const float CastDist = (float)maximum(240, Def.m_Distance + IntVal * 4);
		const int Dmg = maximum(6, Def.m_Damage + IntVal * 2);
		const int ExplosionRadiusTiles = maximum(3, (Def.m_Radius > 0 ? Def.m_Radius / 32 : 5) + IntVal / 10);
		const vec2 From = pChr->GetPos();
		vec2 To = From + Dir * CastDist;
		CCollision *pColl = GS()->Collision();
		if(pColl)
			pColl->IntersectLine(From, To, 0x0, &To);
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, To, (float)ExplosionRadiusTiles * 32.f);
		SpawnSkillDetonationBeam(&GS()->m_World, pPlayer->GetCID(), From, To, ExplosionRadiusTiles, Dmg, true);
		return true;
	}

	// ─── Homing Plasma (智力+智慧→追踪等离子) ─────────────────────
	if(str_comp(Def.m_aKey, "homing_plasma") == 0)
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		if(length(Dir) < 0.01f)
			Dir = vec2(1.f, 0.f);
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const int WisVal = pPlayer->GetStat(AttributeIdentifier::WIS);
		const float LockRange = (float)maximum(320, Def.m_Distance + WisVal * 6);
		const int Dmg = maximum(5, Def.m_Damage + IntVal * 2);
		const int ExplosionRadiusTiles = maximum(3, (Def.m_Radius > 0 ? Def.m_Radius / 32 : 4) + IntVal / 14);
		const float TrackingStrength = 8.f + WisVal * 0.4f;
		const vec2 From = pChr->GetPos() + Dir * 24.f;

		CCharacter *pTracked = FindNearestSkillHostile(GS(), pPlayer, From, LockRange);
		const int TrackedCID = SkillHostileCID(pTracked);
		if(pTracked)
			Dir = normalize(pTracked->GetPos() - From);

		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, From, 48.f);
		SpawnSkillFollowAura(&GS()->m_World, pPlayer->GetCID(), GS()->Server()->TickSpeed() / 2, SKILL_VFX_ARCANE, 40.f);
		SpawnSkillHomingPlasma(&GS()->m_World, pPlayer->GetCID(), From, Dir, Dmg, ExplosionRadiusTiles, TrackedCID, TrackingStrength);
		return true;
	}

	// ─── Ricochet Shot (智力→弹跳弹) ─────────────────────────────
	if(str_comp(Def.m_aKey, "ricochet_shot") == 0)
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		if(length(Dir) < 0.01f)
			Dir = vec2(1.f, 0.f);
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const int Dmg = maximum(4, Def.m_Damage + IntVal * 2);
		const vec2 SpawnPos = pChr->GetPos() + Dir * 32.f;
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, SpawnPos, 48.f);
		SpawnSkillFollowAura(&GS()->m_World, pPlayer->GetCID(), GS()->Server()->TickSpeed() / 2, SKILL_VFX_ARCANE, 40.f);
		SpawnSkillRicochetShot(&GS()->m_World, pPlayer->GetCID(), SpawnPos, Dir, Dmg);
		return true;
	}

	// ─── Laser Trap (智力→激光围栏) ─────────────────────────────
	if(str_comp(Def.m_aKey, "laser_trap") == 0)
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		if(length(Dir) < 0.01f)
			Dir = vec2(1.f, 0.f);
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const float CastDist = (float)maximum(240, Def.m_Distance + IntVal * 3);
		const int Dmg = maximum(5, Def.m_Damage + IntVal * 2);
		const int NumBeams = clamp(3 + IntVal / 20, 3, 5);
		const int DurationTicks = GS()->Server()->TickSpeed() * 15;
		const vec2 From = pChr->GetPos();
		vec2 To = From + Dir * CastDist;
		CCollision *pColl = GS()->Collision();
		if(pColl)
			pColl->IntersectLine(From, To, 0x0, &To);
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, To, 96.f);
		SpawnSkillLaserTrap(&GS()->m_World, pPlayer->GetCID(), From, To, NumBeams, Dmg, DurationTicks);
		return true;
	}

	// ─── Bomb Sentinel (智力/敏捷/智慧→哨戒核心) ─────────────────
	if(str_comp(Def.m_aKey, "bomb_sentinel") == 0)
	{
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const int DexVal = pPlayer->GetStat(AttributeIdentifier::DEX);
		const int WisVal = pPlayer->GetStat(AttributeIdentifier::WIS);
		const int MaxShots = maximum(3, 3 + IntVal / 4);
		const int Dmg = 35 + IntVal * 3;
		const float IntervalSec = clamp(1.2f - DexVal * 0.015f, 0.5f, 1.2f);
		const float Range = 500.f + WisVal * 12.f;
		const vec2 Pos = pChr->GetPos();
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, Pos, 80.f);
		SpawnSkillBombSentinel(&GS()->m_World, pPlayer->GetCID(), Pos, MaxShots, Dmg, Range, IntervalSec);
		return true;
	}

	// ─── Acid Pool (智力→酸液池) ─────────────────────────────────
	if(str_comp(Def.m_aKey, "acid_pool") == 0)
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		if(length(Dir) < 0.01f)
			Dir = vec2(1.f, 0.f);
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const float CastDist = (float)maximum(64, Def.m_Radius > 0 ? Def.m_Radius : 100);
		const float Radius = (float)maximum(80.f, Def.m_Radius > 0 ? (float)Def.m_Radius : 100.f);
		const int Stacks = maximum(1, (Def.m_Damage > 0 ? Def.m_Damage : 2) + IntVal / 10);
		const int DurationTicks = GS()->Server()->TickSpeed() * 8;
		vec2 TargetPos = pChr->GetPos() + Dir * CastDist;
		CCollision *pColl = GS()->Collision();
		if(pColl)
		{
			if(pColl->CheckPoint(TargetPos))
				TargetPos = pChr->GetPos();
			TargetPos.x = clamp(TargetPos.x, 16.0f, (float)(pColl->GetWidth() * 32 - 16));
			TargetPos.y = clamp(TargetPos.y, 16.0f, (float)(pColl->GetHeight() * 32 - 16));
		}
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, TargetPos, Radius);
		SpawnSkillAcidPool(&GS()->m_World, pPlayer->GetCID(), TargetPos, Radius, Stacks, DurationTicks, 0.8f);
		return true;
	}

	// ─── Corpse Burst (智力→尸爆范围伤害+减速) ───────────────────
	if(str_comp(Def.m_aKey, "corpse_burst") == 0 && Core() && Core()->StatusManager())
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		if(length(Dir) < 0.01f)
			Dir = vec2(0.f, -1.f);
		const vec2 Pos = pChr->GetPos();
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const int RadiusTiles = maximum(6, (Def.m_Radius > 0 ? Def.m_Radius / 32 : 6) + IntVal / 14);
		const int Dmg = maximum(10, Def.m_Damage + IntVal * 2);
		const float RadiusPx = (float)RadiusTiles * 32.f;
		const int SlowTicks = GS()->Server()->TickSpeed() + IntVal / 2;
		const int GrowTicks = GS()->Server()->TickSpeed() * 2;
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, Pos, RadiusPx);
		SpawnSkillZoneAura(&GS()->m_World, Pos, GrowTicks, SKILL_VFX_POISON, RadiusPx * 0.45f);
		new CGrowingExplosion(&GS()->m_World, Pos, Dir, pPlayer->GetCID(), RadiusTiles, GROWINGEXPLOSIONEFFECT_BOOM, false, GE_TARGET_MMO_HOSTILE, Dmg);
		ForEachHostileInRadius(&GS()->m_World, GS(), pPlayer, Pos, RadiusPx, [&](CCharacter *pTargetChr) {
			Core()->StatusManager()->ApplyStatus(pTargetChr, "frost", 1, SlowTicks, 0.60f);
			return true;
		});
		GS()->m_World.CreateSound(Pos, SOUND_GRENADE_EXPLODE);
		return true;
	}

	// ─── Smoke Hook (智力→烟钩牵引+毒燃) ─────────────────────────
	if(str_comp(Def.m_aKey, "smoke_hook") == 0)
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		if(length(Dir) < 0.01f)
			Dir = vec2(1.f, 0.f);
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const int DexVal = pPlayer->GetStat(AttributeIdentifier::DEX);
		const float MaxDist = (float)maximum(320, Def.m_Distance + DexVal * 4);
		const int HookDmg = maximum(3, Def.m_Damage + IntVal * 2);
		const vec2 Start = pChr->GetPos();
		CCollision *pColl = GS()->Collision();
		if(!pColl)
			return false;
		CCharacter *pHit = FindNearestSkillHostile(GS(), pPlayer, Start, MaxDist, Dir, 0.5f, pColl);
		const int HitCID = SkillHostileCID(pHit);
		if(HitCID < 0)
			return false;
		const vec2 TargetPos = pHit->GetPos();
		vec2 HookDir = normalize(TargetPos - Start);
		if(length(HookDir) < 0.01f)
			HookDir = Dir;
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, Start, 48.f);
		SpawnSkillFollowAura(&GS()->m_World, pPlayer->GetCID(), GS()->Server()->TickSpeed() / 2, SKILL_VFX_POISON, 40.f);
		SpawnSkillMagicBolt(&GS()->m_World, pPlayer->GetCID(), Start + HookDir * 24.f, HookDir, HookDmg, SKILL_BOLT_SMOKE_HOOK, HitCID);
		GS()->m_World.CreateSound(Start, SOUND_HOOK_LOOP);
		return true;
	}

	// ─── Web Snare (智慧→蛛网缚足) ───────────────────────────────
	if(str_comp(Def.m_aKey, "web_snare") == 0 && Core() && Core()->StatusManager())
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		if(length(Dir) < 0.01f)
			Dir = vec2(1.f, 0.f);
		const int WisVal = pPlayer->GetStat(AttributeIdentifier::WIS);
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const float MaxDist = (float)maximum(256, Def.m_Distance + WisVal * 4);
		const int SnareTicks = GS()->Server()->TickSpeed() * 3 / 2 + WisVal * 2;
		const int ApplyDmg = maximum(1, Def.m_Damage + IntVal);
		const vec2 Start = pChr->GetPos();
		CCollision *pColl = GS()->Collision();
		if(!pColl)
			return false;
		CCharacter *pTargetChr = FindNearestSkillHostile(GS(), pPlayer, Start, MaxDist, Dir, 0.5f, pColl);
		if(!pTargetChr)
			return false;
		const int HitCID = SkillHostileCID(pTargetChr);
		Core()->StatusManager()->ApplyStatus(pTargetChr, "frost", 1, SnareTicks, 0.30f);
		if(ApplyDmg > 0)
			pTargetChr->TakeDamage(vec2(0.f, 0.f), Start, ApplyDmg, pPlayer->GetCID(), WEAPON_GAME);
		SpawnSkillCastFeedback(&GS()->m_World, Def.m_aKey, pTargetChr->GetPos(), 56.f);
		SpawnSkillFollowAura(&GS()->m_World, HitCID, SnareTicks, SKILL_VFX_POISON, 48.f);
		SpawnSkillHitBurst(&GS()->m_World, pTargetChr->GetPos(), 48.f, SKILL_VFX_POISON);
		GS()->m_World.CreateSound(pTargetChr->GetPos(), SOUND_HOOK_ATTACH_PLAYER);
		return true;
	}

	return false;
}

bool CSkillManager::CanUse(CPlayer *pPlayer, int SkillId) const
{
	if(!pPlayer || !GS())
		return false;
	const SSkillDescription *pDef = FindDescription(SkillId);
	if(!pDef || pDef->m_Passive)
		return false;

	const SSkillInstance *pInst = const_cast<CSkillManager *>(this)->GetInstance(pPlayer, SkillId);
	if(!pInst || !pInst->m_Learned)
		return false;

	if(pInst->m_CooldownEnd > GS()->Server()->Tick())
		return false;

	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr || !pChr->IsAlive())
		return false;

	if(pDef->m_ManaCostPct > 0)
	{
		const int MaxMana = pPlayer->GetMaxMana();
		const int ManaCost = maximum(1, MaxMana * pDef->m_ManaCostPct / 100);
		if(pChr->Mana() < ManaCost)
			return false;
	}

	return true;
}

bool CSkillManager::CastMagicWand(CPlayer *pPlayer)
{
	if(!pPlayer || !GS())
		return false;
	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr || !pChr->IsAlive())
		return false;

	const int MaxMana = pPlayer->GetMaxMana();
	const int ManaCost = maximum(2, MaxMana / 30 + 1);
	if(!pChr->TryUseMana(ManaCost))
		return false;

	vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
	if(length(Dir) < 0.01f)
		Dir = vec2(1.f, 0.f);

	const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
	const int PlayerLevel = maximum(1, pPlayer->GetStat(AttributeIdentifier::Level));
	const int Dmg = maximum(2, minimum(12 + PlayerLevel / 4, 2 + IntVal / 5));
	const vec2 SpawnPos = pChr->GetPos() + Dir * 28.f;

	SpawnSkillMagicBolt(&GS()->m_World, pPlayer->GetCID(), SpawnPos, Dir, Dmg, SKILL_BOLT_WAND, -1);
	SpawnSkillCastFlash(&GS()->m_World, SpawnPos, SKILL_VFX_ARCANE);
	SpawnSkillFollowAura(&GS()->m_World, pPlayer->GetCID(), GS()->Server()->TickSpeed() / 3, SKILL_VFX_ARCANE, 36.f);
	GS()->m_World.CreateSound(SpawnPos, SOUND_SFX_WEAPON_PULSE);
	return true;
}

bool CSkillManager::Use(CPlayer *pPlayer, int SkillId, bool NotifyFailure)
{
	if(!pPlayer || !GS())
		return false;
	const SSkillDescription *pDef = FindDescription(SkillId);
	if(!pDef || pDef->m_Passive)
		return false;

	SSkillInstance *pInst = GetInstance(pPlayer, SkillId);
	if(!pInst || !pInst->m_Learned)
	{
		if(NotifyFailure)
			GS()->SendChatLoc(pPlayer->GetCID(), "skill.not_learned", "尚未学习该魔法。");
		return false;
	}

	const int CID = pPlayer->GetCID();
	const int Tick = GS()->Server()->Tick();
	if(pInst->m_CooldownEnd > Tick)
	{
		if(NotifyFailure)
			GS()->SendChatLocF(CID, "skill.cooldown", "魔法冷却中（%d 秒）",
				(pInst->m_CooldownEnd - Tick + GS()->Server()->TickSpeed() - 1) / GS()->Server()->TickSpeed());
		return false;
	}

	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr || !pChr->IsAlive())
		return false;

	if(pDef->m_ManaCostPct > 0)
	{
		const int MaxMana = pPlayer->GetMaxMana();
		const int ManaCost = maximum(1, MaxMana * pDef->m_ManaCostPct / 100);
		if(!pChr->TryUseMana(ManaCost))
		{
			if(NotifyFailure)
				GS()->SendChatLoc(CID, "skill.not_enough_mana", "法力不足，无法释放。");
			return false;
		}
	}

	if(!ExecuteSkill(pPlayer, *pDef))
		return false;

	PlayInteractionSound(GS()->m_World, pPlayer, SOUND_SFX_SKILL);

	const int SkillLevel = GetSkillLevel(pPlayer, SkillId);
	float CdMul = SkillLevelCooldownMul(SkillLevel);
	if(Core() && Core()->TraitManager())
		CdMul *= Core()->TraitManager()->GetSkillCdMul(pPlayer);
	pInst->m_CooldownEnd = Tick + maximum(1, (int)(pDef->m_CooldownTicks * CdMul + 0.5f));
	RecordSkillUse(pPlayer, SkillId);
	char aKey[48];
	str_format(aKey, sizeof(aKey), "skill.%s", pDef->m_aKey);
	GS()->SendChatLocF(CID, "skill.used", "已释放魔法：%s", GS()->Loc(CID, aKey, pDef->m_aKey));
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
	CVoteWrapper V(ClientID, GS(), pVote);

	V.GroupTitle(GS()->Loc(ClientID, "skill.menu.title", "魔法"));

	for(int Cat = 0; Cat < SKILL_CAT_NUM; Cat++)
	{
		bool ShownHeader = false;
		for(int i = 0; i < m_NumSkills; i++)
		{
			const SSkillDescription &Def = m_aSkills[i];
			if(Def.m_Category != Cat)
				continue;

			if(!ShownHeader)
			{
				V.GroupLine();
				V.Info(SkillCategoryLabel((ESkillCategory)Cat));
				ShownHeader = true;
			}

			char aKey[48];
			str_format(aKey, sizeof(aKey), "skill.%s", Def.m_aKey);
			const char *pName = GS()->Loc(ClientID, aKey, Def.m_aKey);
			SSkillInstance *pInst = pP ? GetInstance(pP, Def.m_Id) : nullptr;

			char aLine[VOTE_DESC_LENGTH];
			if(pInst && pInst->m_Learned)
			{
				const int Lvl = maximum(1, pInst->m_Level);
				str_format(aLine, sizeof(aLine), "%s ✓ Lv%d", pName, Lvl);
			}
			else
				str_copy(aLine, pName, sizeof(aLine));

			char aCmd[48];
			str_format(aCmd, sizeof(aCmd), "ccv_menuskillsel %d", Def.m_Id);
			V.Option(aCmd, aLine);
		}
	}
	V.Footer();
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
	CVoteWrapper V(ClientID, GS(), pVote);

	V.GroupTitle(GS()->Loc(ClientID, aKey, pDef->m_aKey));
	{
		char aCat[64];
		str_format(aCat, sizeof(aCat), "类型：%s", SkillCategoryLabel(pDef->m_Category));
		V.Info(aCat);
	}
	V.Info(GS()->Loc(ClientID, aDescKey, pDef->m_aKey));

	if(!pDef->m_Passive && pInst && pInst->m_Learned)
	{
		char aMana[64];
		str_format(aMana, sizeof(aMana), GS()->Loc(ClientID, "skill.mana_pct", "法力消耗：%d%%"), pDef->m_ManaCostPct);
		V.Info(aMana);
		const int Lvl = maximum(1, pInst->m_Level);
		const int MaxLvl = maximum(1, pDef->m_MaxLevel);
		char aLevel[64];
		str_format(aLevel, sizeof(aLevel), GS()->Loc(ClientID, "skill.level", "等级：%d / %d"), Lvl, MaxLvl);
		V.Info(aLevel);
		if(Lvl < MaxLvl)
		{
			const int Required = SkillUsesRequiredForLevel(Lvl, pDef->m_UsesPerLevel);
			char aProg[64];
			str_format(aProg, sizeof(aProg), GS()->Loc(ClientID, "skill.level_progress", "熟练度：%d / %d"),
				pInst->m_UseCount, Required);
			V.Info(aProg);
		}
	}

	if(pInst && pInst->m_Learned)
	{
		if(!pDef->m_Passive)
		{
			V.GroupLine();
			char aBind[128];
			str_format(aBind, sizeof(aBind), GS()->Loc(ClientID, "skill.bind_hint", "绑定：bind 'F1' say \"/use_skill %d\""), SkillId);
			V.Info(aBind);
			char aEmo[128];
			str_format(aEmo, sizeof(aEmo), GS()->Loc(ClientID, "skill.emote_current", "表情触发：%s"), SkillEmoticonName(pInst->m_EmoticonBind));
			V.Info(aEmo);

			char aCmdEmo[48];
			str_format(aCmdEmo, sizeof(aCmdEmo), "ccv_skillemote %d", SkillId);
			V.Option(aCmdEmo, GS()->Loc(ClientID, "skill.change_emote", "切换表情绑定"));

			for(int si = 0; si < 3; si++)
			{
				bool IsBound = (pP->m_aSkillSlots[si] == SkillId);
				char aSlotBind[96];
				if(IsBound)
				{
					char aSkillName[64];
					str_copy(aSkillName, GS()->Loc(ClientID, aKey, pDef->m_aKey), sizeof(aSkillName));
					str_format(aSlotBind, sizeof(aSlotBind), GS()->Loc(ClientID, "skill.slot_bound", "已绑定 [%s] 到键盘 %d"), aSkillName, si + 3);
				}
				else
					str_format(aSlotBind, sizeof(aSlotBind), GS()->Loc(ClientID, "skill.slot_bind", "绑定到键盘 %d"), si + 3);
				char aSlotCmd[48];
				str_format(aSlotCmd, sizeof(aSlotCmd), "ccv_skillslot %d %d", SkillId, si);
				V.Option(aSlotCmd, aSlotBind);
			}

			if(pInst->m_CooldownEnd <= Tick)
			{
				char aUseCmd[48];
				str_format(aUseCmd, sizeof(aUseCmd), "ccv_skilluse %d", SkillId);
				V.Option(aUseCmd, GS()->Loc(ClientID, "skill.cast", "立即释放"));
			}
		}
	}
	else if(!pDef->m_AutoLearn)
	{
		V.GroupLine();
		char aLearn[128];
		str_format(aLearn, sizeof(aLearn), GS()->Loc(ClientID, "skill.learn_btn", "学习（需僵尸之心 ×%d）"), pDef->m_LearnCostHearts);
		char aLearnCmd[48];
		str_format(aLearnCmd, sizeof(aLearnCmd), "ccv_skilllearn %d", SkillId);
		V.Option(aLearnCmd, aLearn);
	}
	else
	{
		V.Info(GS()->Loc(ClientID, "skill.auto_learn", "登录后自动习得"));
	}

	V.Footer();
}

bool CSkillManager::OnVoteMenuPage(int ClientID, int Page)
{
	if(!GS() || !Core() || !Core()->VoteMenuManager())
		return false;

	if(Page == PAGE_SKILLS)
	{
		Core()->VoteMenuManager()->SetVoteLastPage(PAGE_MENU);
		BuildSkillsListPage(ClientID);
		return true;
	}

	if(Page == PAGE_SKILL_SELECT)
	{
		SPlayerVote *pVote = Core()->VoteMenuManager()->GetPlayerVote(ClientID);
		if(!pVote)
			return false;
		Core()->VoteMenuManager()->SetVoteLastPage(pVote->m_LastPage >= 0 ? pVote->m_LastPage : PAGE_SKILLS);
		BuildSkillDetailPage(ClientID, pVote->m_SkillId);
		return true;
	}

	return false;
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

static void ComVoteSkillSlot(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = static_cast<CCommandManager::SCommandContext *>(pUser);
	CGameContext *pGame = static_cast<CGameContext *>(pCtx->m_pContext);
	if(!pGame || !pGame->Core())
		return;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP)
		return;
	const int SkillId = pResult->GetInteger(0);
	const int SlotIdx = pResult->GetInteger(1);
	if(SlotIdx < 0 || SlotIdx >= 3)
		return;

	// If skill is already bound to this slot, unbind it
	if(pP->m_aSkillSlots[SlotIdx] == SkillId)
	{
		pP->m_aSkillSlots[SlotIdx] = -1;
		pGame->SendChatLoc(pP->GetCID(), "skill.slot_unbound", "已取消魔法槽位绑定。");
	}
	else
	{
		// Remove this skill from any other slot it might be in (one skill max)
		for(int si = 0; si < 3; si++)
		{
			if(pP->m_aSkillSlots[si] == SkillId)
				pP->m_aSkillSlots[si] = -1;
		}
		pP->m_aSkillSlots[SlotIdx] = SkillId;

		const SSkillDescription *pDef = pGame->Core()->SkillManager()->FindDescription(SkillId);
		char aSkillName[64] = "?";
		if(pDef)
		{
			char aKey[48];
			str_format(aKey, sizeof(aKey), "skill.%s", pDef->m_aKey);
			str_copy(aSkillName, pGame->Loc(pP->GetCID(), aKey, pDef->m_aKey), sizeof(aSkillName));
		}
		pGame->SendChatLocF(pP->GetCID(), "skill.slot_bound", "已绑定 [%s] 到键盘 %d", aSkillName, SlotIdx + 3);
	}

	// Refresh vote page
	SPlayerVote *pV = pGame->Core()->VoteMenuManager()->GetPlayerVote(pCtx->m_ClientID);
	pV->m_SkillId = SkillId;
	pV->m_Page = PAGE_SKILL_SELECT;
	pGame->Core()->VoteMenuManager()->ClearVotes(pCtx->m_ClientID);
}

void CSkillManager::RegisterChatCommands(CCommandManager *pMgr)
{
	if(!pMgr || !GS())
		return;
	// use_skill/skill 仅通过投票菜单 ccv_skilluse 等
}

void CSkillManager::RegisterVoteCommands(CCommandManager *pMgr)
{
	if(!pMgr || !GS())
		return;
	pMgr->AddVoteCommand("menuskillsel", "", "i", ComVoteSkillSel, GS());
	pMgr->AddVoteCommand("skilllearn", "", "i", ComVoteSkillLearn, GS());
	pMgr->AddVoteCommand("skillemote", "", "i", ComVoteSkillEmote, GS());
	pMgr->AddVoteCommand("skilluse", "", "i", ComVoteSkillUse, GS());
	pMgr->AddVoteCommand("skillslot", "", "ii", ComVoteSkillSlot, GS());
}
