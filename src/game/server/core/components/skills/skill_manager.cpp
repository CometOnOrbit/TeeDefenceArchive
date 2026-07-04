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
			GS()->SendChatLoc(pPlayer->GetCID(), "skill.learn.need_hearts", "僵尸之心不足，无法学习魔法。");
			return false;
		}
		pPlayer->m_AccData.m_aItems[ITEM_ZOMBIEHEART].m_Num -= pDef->m_LearnCostHearts;
		if(GS()->Accounts() && GS()->Accounts()->IsEnabled() && pPlayer->GetAccountId() >= 0)
			GS()->Accounts()->RequestSaveItems(pPlayer->GetCID());
	}

	pInst->m_Learned = true;

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
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			CPlayer *pAlly = GS()->m_apPlayers[i];
			if(!pAlly || pAlly->IsDummy() || !pAlly->GetCharacter() || !pAlly->GetCharacter()->IsAlive())
				continue;
			if(distance(Pos, pAlly->GetCharacter()->GetPos()) <= Radius)
			{
				pAlly->GetCharacter()->IncreaseHealth(Heal);
				// Apply damage boost status (type 'burn' with negative effect sign = buff)
				// Use a custom status "atk_boost" that ProcessStatus interprets
				Core()->StatusManager()->ApplyStatus(pAlly->GetCharacter(), "atk_boost", 1, BuffTicks, 1.0f, 2);
			}
		}
		GS()->m_World.CreateSound(Pos, SOUND_GRENADE_EXPLODE);
		return true;
	}

	// ─── Frost Nova (智力→伤害/冻结时长) ────────────────────
	if(str_comp(Def.m_aKey, "frost_nova") == 0 && Core() && Core()->StatusManager())
	{
		const vec2 Pos = pChr->GetPos();
		const float Radius = (float)maximum(96, Def.m_Radius);
		// INT: 每点智力 +int*3 冰伤, 冻结时间+2tick
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const int SlowTicks = GS()->Server()->TickSpeed() * 3 + IntVal * 2;
		const int FreezeDamage = 8 + IntVal * 3;
		// Affect ALL enemies (not just zombies)
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			CPlayer *pTarget = GS()->m_apPlayers[i];
			if(!pTarget || !pTarget->GetCharacter() || !pTarget->GetCharacter()->IsAlive())
				continue;
			if(pTarget == pPlayer)
				continue;
			// Skip allies (same team for now, or non-hostile bots)
			if(!pTarget->IsDummy() && pTarget->GetTeam() == pPlayer->GetTeam())
				continue;
			if(distance(Pos, pTarget->GetCharacter()->GetPos()) <= Radius)
			{
				CCharacter *pTargetChr = pTarget->GetCharacter();
				// Check if already frozen → shatter (bonus damage)
				if(Core()->StatusManager()->GetSlowTicks(pTargetChr) > 0)
				{
					// Shatter: deal bonus damage + remove frost
					pTargetChr->TakeDamage(vec2(0, 0), Pos, FreezeDamage, pPlayer->GetCID(), WEAPON_GAME);
					GS()->m_World.CreateSound(pTargetChr->GetPos(), SOUND_GRENADE_EXPLODE);
				}
				else
				{
					Core()->StatusManager()->ApplyStatus(pTargetChr, "frost", 1, SlowTicks, 0.55f);
				}
			}
		}
		GS()->m_World.CreateSound(Pos, SOUND_WEAPON_SPAWN);
		return true;
	}

	// ─── Arcane Shield (智力→吸收量) ────────────────────────
	if(str_comp(Def.m_aKey, "arcane_shield") == 0 && Core() && Core()->StatusManager())
	{
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const int Shield = maximum(4, Def.m_Heal + IntVal * 2);
		Core()->StatusManager()->ApplyStatus(pChr, "shield", 1, GS()->Server()->TickSpeed() * 6, 0.f, Shield);
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
		vec2 NewPos = pChr->GetPos() + Dir * Distance;
		// Clamp to map bounds
		CCollision *pColl = GS()->Collision();
		if(pColl)
		{
			NewPos.x = clamp(NewPos.x, 16.0f, (float)(pColl->GetWidth() * 32 - 16));
			NewPos.y = clamp(NewPos.y, 16.0f, (float)(pColl->GetHeight() * 32 - 16));
		}
		pChr->SetCharacterPos(NewPos);
		GS()->m_World.CreateSound(NewPos, SOUND_NINJA_FIRE);
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
		GS()->m_World.CreateSound(pChr->GetPos(), SOUND_PICKUP_HEALTH);
		return true;
	}

	// ─── Iron Will (体质→持续时间) ───────────────────────────
	if(str_comp(Def.m_aKey, "iron_will") == 0)
	{
		const int ConVal = pPlayer->GetStat(AttributeIdentifier::CON);
		const int DurationTicks = GS()->Server()->TickSpeed() * (3 + ConVal / 10);
		pChr->m_IronWillTicks = DurationTicks;
		// Slow: apply frost status with weak slow, or just reduce velocity
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
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			CPlayer *pTarget = GS()->m_apPlayers[i];
			if(!pTarget || !pTarget->GetCharacter() || !pTarget->GetCharacter()->IsAlive())
				continue;
			if(pTarget == pPlayer)
				continue;
			if(!pTarget->IsDummy() && pTarget->GetTeam() == pPlayer->GetTeam())
				continue;
			const vec2 TargetPos = pTarget->GetCharacter()->GetPos();
			const float Dist = distance(Pos, TargetPos);
			if(Dist <= Radius)
			{
				vec2 KnockDir = normalize(TargetPos - Pos);
				if(length(KnockDir) < 0.01f)
					KnockDir = vec2(1.f, 0.f);
				pTarget->GetCharacter()->TakeDamage(KnockDir * (float)KnockbackForce * (1.0f - Dist / Radius),
					Pos, BaseDmg, pPlayer->GetCID(), WEAPON_HAMMER);
			}
		}
		GS()->m_World.CreateSound(Pos, SOUND_HAMMER_FIRE);
		return true;
	}

	// ─── Entangle (智力→定身时长) ────────────────────────────
	if(str_comp(Def.m_aKey, "entangle") == 0)
	{
		vec2 Dir = normalize(vec2((float)pChr->LatestInput().m_TargetX, (float)pChr->LatestInput().m_TargetY));
		if(length(Dir) < 0.01f)
			Dir = vec2(1.f, 0.f);
		const int IntVal = pPlayer->GetStat(AttributeIdentifier::INT);
		const float MaxDist = (float)maximum(128, Def.m_Distance + IntVal * 3);
		const vec2 Start = pChr->GetPos();
		const vec2 End = Start + Dir * MaxDist;
		vec2 HitPos = End;
		vec2 ColPos = End;
		int HitCID = -1;

		// Raycast: find first character in line
		CCollision *pColl = GS()->Collision();
		if(!pColl)
			return false;

		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			CPlayer *pTarget = GS()->m_apPlayers[i];
			if(!pTarget || !pTarget->GetCharacter() || !pTarget->GetCharacter()->IsAlive())
				continue;
			if(pTarget == pPlayer)
				continue;
			if(!pTarget->IsDummy() && pTarget->GetTeam() == pPlayer->GetTeam())
				continue;
			const vec2 TargetPos = pTarget->GetCharacter()->GetPos();
			const float Dist = distance(Start, TargetPos);
			if(Dist > MaxDist)
				continue;
			// Simple line-of-sight check: is target in the cone?
			vec2 ToTarget = normalize(TargetPos - Start);
			if(dot(Dir, ToTarget) > 0.707f) // ~45 degree cone
			{
				// Check line of sight (no solid tiles between)
				if(!pColl->FastIntersectLine(Start, TargetPos, &HitPos, &ColPos))
				{
					if(HitCID < 0 || Dist < distance(Start, GS()->m_apPlayers[HitCID]->GetCharacter()->GetPos()))
					{
						HitCID = i;
						HitPos = TargetPos;
					}
				}
			}
		}

		if(HitCID >= 0)
		{
			CCharacter *pTargetChr = GS()->m_apPlayers[HitCID]->GetCharacter();
			// Root: apply strong frost with 0.05x slow (effectively roots)
			const int RootTicks = GS()->Server()->TickSpeed() * (2 + IntVal / 5);
			Core()->StatusManager()->ApplyStatus(pTargetChr, "frost", 1, RootTicks, 0.05f);
			GS()->m_World.CreateSound(HitPos, SOUND_HOOK_ATTACH_PLAYER);
		}
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
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			CPlayer *pAlly = GS()->m_apPlayers[i];
			if(!pAlly || !pAlly->GetCharacter() || !pAlly->GetCharacter()->IsAlive())
				continue;
			if(distance(Pos, pAlly->GetCharacter()->GetPos()) <= Radius)
			{
				pAlly->GetCharacter()->IncreaseHealth(Heal);
				// Purge negative statuses by clearing the status slots
				// (status manager doesn't have a purge API, so apply atk_boost as placeholder)
				Core()->StatusManager()->ApplyStatus(pAlly->GetCharacter(), "atk_boost", 1, DefBuffTicks, 1.0f, 2);
			}
		}
		GS()->m_World.CreateSound(Pos, SOUND_PICKUP_HEALTH);
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
		GS()->SendChatLoc(pPlayer->GetCID(), "skill.not_learned", "尚未学习该魔法。");
		return false;
	}

	const int CID = pPlayer->GetCID();
	const int Tick = GS()->Server()->Tick();
	if(pInst->m_CooldownEnd > Tick)
	{
		GS()->SendChatLocF(CID, "skill.cooldown", "魔法冷却中（%d 秒）",
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

	for(int i = 0; i < m_NumSkills; i++)
	{
		const SSkillDescription &Def = m_aSkills[i];
		char aKey[48];
		str_format(aKey, sizeof(aKey), "skill.%s", Def.m_aKey);
		const char *pName = GS()->Loc(ClientID, aKey, Def.m_aKey);
		SSkillInstance *pInst = pP ? GetInstance(pP, Def.m_Id) : nullptr;

		char aLine[VOTE_DESC_LENGTH];
		if(pInst && pInst->m_Learned)
			str_format(aLine, sizeof(aLine), "%s ✓", pName);
		else
			str_copy(aLine, pName, sizeof(aLine));

		char aCmd[48];
		str_format(aCmd, sizeof(aCmd), "ccv_menuskillsel %d", Def.m_Id);
		V.Option(aCmd, aLine);
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
	V.Info(GS()->Loc(ClientID, aDescKey, pDef->m_aKey));

	if(!pDef->m_Passive && pInst && pInst->m_Learned)
	{
		char aMana[64];
		str_format(aMana, sizeof(aMana), GS()->Loc(ClientID, "skill.mana_pct", "法力消耗：%d%%"), pDef->m_ManaCostPct);
		V.Info(aMana);
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
	pMgr->AddCommand("skillslot", "", "ii", ComVoteSkillSlot, GS());
}
