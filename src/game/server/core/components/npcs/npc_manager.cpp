#include <engine/shared/jsonparser.h>

#include <game/server/core/components/npcs/npc_manager.h>
#include <game/server/core/components/quests/quest_manager.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

CNpcManager::CNpcManager()
{
	m_NumNpcs = 0;
	m_PendingSpawnDefIdx = -1;
	mem_zero(m_aNpcs, sizeof(m_aNpcs));
}

int CNpcManager::ParseEmote(const char *pName)
{
	if(!pName || !pName[0])
		return EMOTE_NORMAL;
	if(str_comp_nocase(pName, "happy") == 0)
		return EMOTE_HAPPY;
	if(str_comp_nocase(pName, "pain") == 0)
		return EMOTE_PAIN;
	if(str_comp_nocase(pName, "surprise") == 0)
		return EMOTE_SURPRISE;
	if(str_comp_nocase(pName, "angry") == 0)
		return EMOTE_ANGRY;
	if(str_comp_nocase(pName, "blink") == 0)
		return EMOTE_BLINK;
	return EMOTE_NORMAL;
}

int CNpcManager::NpcSlotForDef(int DefIdx)
{
	return NPC_SLOT_FIRST + DefIdx;
}

void CNpcManager::LoadNpcs()
{
	m_NumNpcs = 0;
	if(!Storage())
		return;

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseFile("server_content/npcs.json", Storage());
	if(!pRoot)
	{
		dbg_msg("npc", "npcs.json: %s", Parser.Error());
		return;
	}

	const json_value &Arr = (*pRoot)["npcs"];
	if(Arr.type != json_array)
		return;

	for(unsigned i = 0; i < Arr.u.array.length && m_NumNpcs < MAX_NPC_DEFS; i++)
	{
		const json_value &N = Arr[(int)i];
		if(N.type != json_object || N["id"].type != json_string)
			continue;

		SNpcDef &Def = m_aNpcs[m_NumNpcs++];
		mem_zero(&Def, sizeof(Def));
		str_copy(Def.m_aId, N["id"].u.string.ptr, sizeof(Def.m_aId));
		if(N["world"].type == json_integer)
			Def.m_World = (int)N["world"].u.integer;
		if(N["x"].type == json_integer)
			Def.m_X = (float)N["x"].u.integer;
		if(N["y"].type == json_integer)
			Def.m_Y = (float)N["y"].u.integer;
		if(N["radius"].type == json_integer)
			Def.m_Radius = (float)N["radius"].u.integer;
		else
			Def.m_Radius = 96.f;

		str_format(Def.m_aNameKey, sizeof(Def.m_aNameKey), "npc.%s.name", Def.m_aId);
		if(N["name_key"].type == json_string)
			str_copy(Def.m_aNameKey, N["name_key"].u.string.ptr, sizeof(Def.m_aNameKey));

		str_copy(Def.m_aSkinBody, "standard", sizeof(Def.m_aSkinBody));
		if(N["skin"].type == json_string)
			str_copy(Def.m_aSkinBody, N["skin"].u.string.ptr, sizeof(Def.m_aSkinBody));
		else if(N["body"].type == json_string)
			str_copy(Def.m_aSkinBody, N["body"].u.string.ptr, sizeof(Def.m_aSkinBody));

		str_copy(Def.m_aSkinDecoration, "uniban", sizeof(Def.m_aSkinDecoration));
		if(N["decoration"].type == json_string)
			str_copy(Def.m_aSkinDecoration, N["decoration"].u.string.ptr, sizeof(Def.m_aSkinDecoration));

		if(N["emote"].type == json_string)
			Def.m_Emote = ParseEmote(N["emote"].u.string.ptr);
		else
			Def.m_Emote = EMOTE_HAPPY;

		Def.m_Static = N["static"].type != json_boolean || N["static"].u.boolean != 0;
		Def.m_ClientID = -1;
	}
	dbg_msg("npc", "loaded %d npcs", m_NumNpcs);
}

void CNpcManager::OnInitWorld(const char *pWhereLocalWorld)
{
	(void)pWhereLocalWorld;
	DespawnAll();
	LoadNpcs();
}

void CNpcManager::OnPostInit()
{
	SpawnWorldNpcs();
}

void CNpcManager::OnShutdown()
{
	DespawnAll();
}

void CNpcManager::DespawnAll()
{
	if(!GS() || !Server())
		return;

	for(int i = 0; i < m_NumNpcs; i++)
	{
		const int CID = m_aNpcs[i].m_ClientID;
		if(CID < 0)
			continue;
		if(GS()->m_apPlayers[CID])
			GS()->m_apPlayers[CID]->OnDisconnect();
		Server()->DummyRemove(CID);
		m_aNpcs[i].m_ClientID = -1;
	}
	m_PendingSpawnDefIdx = -1;
}

void CNpcManager::SpawnWorldNpcs()
{
	if(!GS())
		return;

	const int World = GS()->GetWorldID();
	for(int i = 0; i < m_NumNpcs; i++)
	{
		if(m_aNpcs[i].m_World != World)
			continue;
		SpawnNpc(i);
	}
}

bool CNpcManager::SpawnNpc(int DefIdx)
{
	if(DefIdx < 0 || DefIdx >= m_NumNpcs || !GS() || !Server())
		return false;
	if(m_aNpcs[DefIdx].m_ClientID >= 0)
		return true;

	const int Slot = NpcSlotForDef(DefIdx);
	if(Slot < MAX_HUMAN_CLIENTS || Slot >= MAX_CLIENTS)
		return false;
	if(GS()->m_apPlayers[Slot] || !Server()->IsClientSlotEmpty(Slot))
		return false;

	m_PendingSpawnDefIdx = DefIdx;
	Server()->DummyJoin(Slot, m_aNpcs[DefIdx].m_aId);
	return m_aNpcs[DefIdx].m_ClientID >= 0;
}

void CNpcManager::ApplySkin(CPlayer *pPlayer, const SNpcDef &Def) const
{
	if(!pPlayer)
		return;

	for(int p = 0; p < NUM_SKINPARTS; p++)
	{
		pPlayer->m_TeeInfos.m_aUseCustomColors[p] = 0;
		pPlayer->m_TeeInfos.m_aSkinPartColors[p] = 0xFF000000;
	}
	str_copy(pPlayer->m_TeeInfos.m_aaSkinPartNames[0], "standard", MAX_SKIN_ARRAY_SIZE);
	str_copy(pPlayer->m_TeeInfos.m_aaSkinPartNames[1], Def.m_aSkinBody, MAX_SKIN_ARRAY_SIZE);
	str_copy(pPlayer->m_TeeInfos.m_aaSkinPartNames[2], Def.m_aSkinDecoration, MAX_SKIN_ARRAY_SIZE);
	str_copy(pPlayer->m_TeeInfos.m_aaSkinPartNames[3], "standard", MAX_SKIN_ARRAY_SIZE);
	str_copy(pPlayer->m_TeeInfos.m_aaSkinPartNames[4], "standard", MAX_SKIN_ARRAY_SIZE);
	str_copy(pPlayer->m_TeeInfos.m_aaSkinPartNames[5], "standard", MAX_SKIN_ARRAY_SIZE);
}

void CNpcManager::SpawnCharacterAt(CPlayer *pPlayer, const SNpcDef &Def)
{
	if(!pPlayer || !GS())
		return;

	const vec2 Pos = vec2(Def.m_X, Def.m_Y);
	pPlayer->SpawnAt(Pos);
	if(pPlayer->GetCharacter())
	{
		pPlayer->GetCharacter()->SetHealthDirect(100);
		pPlayer->GetCharacter()->SetEmote(Def.m_Emote, -1);
	}
}

bool CNpcManager::OnBotPlayerCreated(CPlayer *pPlayer)
{
	if(!pPlayer || m_PendingSpawnDefIdx < 0 || m_PendingSpawnDefIdx >= m_NumNpcs)
		return false;

	const int DefIdx = m_PendingSpawnDefIdx;
	m_PendingSpawnDefIdx = -1;

	SNpcDef &Def = m_aNpcs[DefIdx];
	pPlayer->InitQuestNpc(DefIdx);
	pPlayer->SetTeam(TEAM_RED, false);
	ApplySkin(pPlayer, Def);
	SpawnCharacterAt(pPlayer, Def);
	Def.m_ClientID = pPlayer->GetCID();
	return true;
}

void CNpcManager::OnTick()
{
	if(!GS())
		return;

	for(int i = 0; i < m_NumNpcs; i++)
	{
		SNpcDef &Def = m_aNpcs[i];
		if(Def.m_ClientID < 0)
			continue;

		CPlayer *pP = GS()->m_apPlayers[Def.m_ClientID];
		if(!pP || !pP->IsQuestNpc() || !pP->GetCharacter() || !pP->GetCharacter()->IsAlive())
			continue;

		if(Def.m_Static)
			pP->GetCharacter()->SyncSpiderBody(vec2(Def.m_X, Def.m_Y));
	}
}

bool CNpcManager::IsQuestNpc(const CPlayer *pPlayer) const
{
	return pPlayer && pPlayer->IsDummy() && pPlayer->GetQuestNpcDefIdx() >= 0;
}

const SNpcDef *CNpcManager::GetNpc(int Index) const
{
	if(Index < 0 || Index >= m_NumNpcs)
		return nullptr;
	return &m_aNpcs[Index];
}

const SNpcDef *CNpcManager::FindNpc(const char *pNpcId) const
{
	if(!pNpcId)
		return nullptr;
	for(int i = 0; i < m_NumNpcs; i++)
	{
		if(str_comp(m_aNpcs[i].m_aId, pNpcId) == 0)
			return &m_aNpcs[i];
	}
	return nullptr;
}

const SNpcDef *CNpcManager::FindNpcNear(CPlayer *pPlayer, vec2 Pos) const
{
	if(!pPlayer || !Server())
		return nullptr;

	const int World = Server()->GetClientWorldID(pPlayer->GetCID());
	float BestDist = 1.0e9f;
	const SNpcDef *pBest = nullptr;

	for(int i = 0; i < m_NumNpcs; i++)
	{
		const SNpcDef &N = m_aNpcs[i];
		if(N.m_World != World)
			continue;
		const float Dist = distance(Pos, vec2(N.m_X, N.m_Y));
		if(Dist <= N.m_Radius && Dist < BestDist)
		{
			BestDist = Dist;
			pBest = &N;
		}
	}
	return pBest;
}

const char *CNpcManager::NpcIdForClient(int ClientID) const
{
	for(int i = 0; i < m_NumNpcs; i++)
	{
		if(m_aNpcs[i].m_ClientID == ClientID)
			return m_aNpcs[i].m_aId;
	}
	return nullptr;
}

bool CNpcManager::TryHammerTalk(CCharacter *pChr, vec2 ProjStartPos)
{
	if(!pChr || !pChr->GetPlayer() || pChr->GetPlayer()->IsDummy() || !GS() || !Core() || !Core()->QuestManager())
		return false;

	CPlayer *pTalker = pChr->GetPlayer();
	const vec2 ChrPos = pChr->GetPos();
	const char *pNpcId = nullptr;
	float BestDist = (float)NPC_HAMMER_RANGE + 1.f;

	for(CGameWorld::TypeRange r = GS()->m_World.DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
	{
		CCharacter *pTarget = static_cast<CCharacter *>(r.front());
		if(!pTarget || pTarget == pChr || !pTarget->GetPlayer() || !IsQuestNpc(pTarget->GetPlayer()))
			continue;

		const float Dist = distance(pTarget->GetPos(), ChrPos);
		if(Dist > (float)NPC_HAMMER_RANGE || Dist >= BestDist)
			continue;

		if(GS()->Collision()->IntersectLine(ProjStartPos, pTarget->GetPos(), nullptr, nullptr))
			continue;

		pNpcId = NpcIdForClient(pTarget->GetPlayer()->GetCID());
		if(!pNpcId)
			continue;
		BestDist = Dist;
	}

	if(!pNpcId)
		return false;

	Core()->QuestManager()->TryTalkNpc(pTalker, pNpcId);

	const SNpcDef *pDef = FindNpc(pNpcId);
	if(pDef)
	{
		char aName[64];
		str_copy(aName, GS()->Loc(pTalker->GetCID(), pDef->m_aNameKey, pDef->m_aId), sizeof(aName));
		GS()->SendChatLocF(pTalker->GetCID(), "npc.talk.begin", u8"你开始与「%s」交谈。", aName);
	}

	GS()->m_World.CreateHammerHit(ProjStartPos);
	GS()->m_World.CreateSound(ChrPos, SOUND_TEE_CRY);
	return true;
}
