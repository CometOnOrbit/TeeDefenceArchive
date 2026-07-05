#include <engine/shared/jsonparser.h>

#include <game/server/core/components/npcs/npc_manager.h>
#include <game/server/core/components/npcs/npc_service.h>
#include <game/server/core/components/vote/vote_menu_manager.h>
#include <game/server/core/components/dialogs/dialog_manager.h>
#include <game/server/core/components/quests/quest_manager.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/interaction_sound.h>
#include <generated/server_data.h>
#include <game/server/player.h>

#include <cinttypes>

CNpcManager::CNpcManager()
{
	m_NumNpcs = 0;
	m_NumSpawns = 0;
	m_PendingSpawnIdx = -1;
	mem_zero(m_aNpcs, sizeof(m_aNpcs));
	mem_zero(m_aSpawns, sizeof(m_aSpawns));
	mem_zero(m_aAutoTalkCooldownTick, sizeof(m_aAutoTalkCooldownTick));
	for(int i = 0; i < MAX_CLIENTS; i++)
		m_aAutoTalkNpcSpawnIdx[i] = -1;
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

int CNpcManager::NpcSlotForSpawn(int SpawnIdx)
{
	return NPC_SLOT_FIRST + SpawnIdx;
}

// ══════════════════════════════════════════════════════════════
//  JSON value → string serializer (since json-parser has no output)
// ══════════════════════════════════════════════════════════════

static void SerializeJsonValue(const json_value &Val, char *pBuf, int BufSize)
{
	int Off = str_length(pBuf);
	char *p = pBuf + Off;
	int Rem = BufSize - Off;

	auto Append = [&](const char *pStr)
	{
		const int Len = str_length(pStr);
		if(Len < Rem)
		{
			mem_copy(p, pStr, Len);
			p += Len;
			Rem -= Len;
			*p = 0;
		}
	};

	switch(Val.type)
	{
	case json_none:
		Append("null");
		break;
	case json_null:
		Append("null");
		break;
	case json_string:
	{
		Append("\"");
		const char *pStr = (const char *)Val;
		while(*pStr && Rem > 1)
		{
			if(*pStr == '"' || *pStr == '\\')
			{
				*p++ = '\\';
				Rem--;
			}
			*p++ = *pStr++;
			Rem--;
		}
		Append("\"");
		break;
	}
	case json_integer:
	{
		str_format(p, Rem, "%" PRId64, (json_int_t)Val);
		break;
	}
	case json_double:
		str_format(p, Rem, "%.6g", (double)Val);
		break;
	case json_boolean:
		Append((bool)Val ? "true" : "false");
		break;
	case json_array:
	{
		Append("[");
		for(unsigned i = 0; i < Val.u.array.length; i++)
		{
			if(i > 0) Append(",");
			SerializeJsonValue(Val[(int)i], pBuf, BufSize);
			p = pBuf + str_length(pBuf);
			Rem = BufSize - str_length(pBuf);
		}
		Append("]");
		break;
	}
	case json_object:
	{
		Append("{");
		for(unsigned i = 0; i < Val.u.object.length; i++)
		{
			if(i > 0) Append(",");
			Append("\"");
			Append(Val.u.object.values[i].name);
			Append("\":");
			SerializeJsonValue(*Val.u.object.values[i].value, pBuf, BufSize);
			p = pBuf + str_length(pBuf);
			Rem = BufSize - str_length(pBuf);
		}
		Append("}");
		break;
	}
	}

	*p = 0;
}

// ══════════════════════════════════════════════════════════════
//  Load NPC identities from npcs.json (no position data)
// ══════════════════════════════════════════════════════════════

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

		Def.m_ClientID = -1;
	}
	dbg_msg("npc", "loaded %d npc identities", m_NumNpcs);
}

// ══════════════════════════════════════════════════════════════
//  Load spawn definitions from spawns.json
// ══════════════════════════════════════════════════════════════

void CNpcManager::LoadSpawns()
{
	m_NumSpawns = 0;
	if(!Storage())
		return;

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseFile("server_content/spawns.json", Storage());
	if(!pRoot)
	{
		dbg_msg("npc", "spawns.json: %s", Parser.Error());
		return;
	}

	const json_value &Arr = (*pRoot)["spawns"];
	if(Arr.type != json_array)
		return;

	for(unsigned i = 0; i < Arr.u.array.length && m_NumSpawns < MAX_SPAWN_DEFS; i++)
	{
		const json_value &S = Arr[(int)i];
		if(S.type != json_object || S["entry_id"].type != json_string)
			continue;

		SSpawnDef &Spawn = m_aSpawns[m_NumSpawns++];
		mem_zero(&Spawn, sizeof(Spawn));
		str_copy(Spawn.m_aId, S["entry_id"].u.string.ptr, sizeof(Spawn.m_aId));

		if(S["npc_id"].type == json_string)
			str_copy(Spawn.m_aNpcId, S["npc_id"].u.string.ptr, sizeof(Spawn.m_aNpcId));

		if(S["world"].type == json_integer)
			Spawn.m_World = (int)S["world"].u.integer;

		if(S["x"].type == json_integer)
			Spawn.m_X = (float)S["x"].u.integer;
		if(S["y"].type == json_integer)
			Spawn.m_Y = (float)S["y"].u.integer;

		if(S["radius"].type == json_integer)
			Spawn.m_Radius = (float)S["radius"].u.integer;
		else
			Spawn.m_Radius = 96.f;

		Spawn.m_Static = (S["static"].type != json_boolean) || (S["static"].u.boolean != 0);

		// dialog_tag is optional
		if(S["dialog_tag"].type == json_string)
			str_copy(Spawn.m_aDialogTag, S["dialog_tag"].u.string.ptr, sizeof(Spawn.m_aDialogTag));

		// conditions as a JSON string (stored for runtime evaluation)
		// Note: json_none = key doesn't exist, json_null = key exists but is null
		if(S["conditions"].type != json_none && S["conditions"].type != json_null)
		{
			Spawn.m_aConditions[0] = 0;
			SerializeJsonValue(S["conditions"], Spawn.m_aConditions, sizeof(Spawn.m_aConditions));
		}

		Spawn.m_ClientID = -1;
	}
	dbg_msg("npc", "loaded %d spawn points", m_NumSpawns);
}

// ══════════════════════════════════════════════════════════════
//  Condition evaluation for spawns
// ══════════════════════════════════════════════════════════════

bool CNpcManager::EvaluateSpawnConditions(const char *pConditionsJson, CPlayer *pPlayer)
{
	if(!pConditionsJson || !pConditionsJson[0] || !pPlayer)
		return true; // no conditions = always match

	CJsonParser Parser;
	json_value *pCond = Parser.ParseString(pConditionsJson, "spawn_conditions");
	if(!pCond)
		return true; // invalid JSON = permissive

	bool Result = false;

	// OR-of-ANDs: outer array = OR, inner objects = AND
	if(pCond->type == json_array)
	{
		for(unsigned i = 0; i < pCond->u.array.length; i++)
		{
			const json_value &Group = (*pCond)[(int)i];
			if(Group.type != json_object)
				continue;

			bool AllMatch = true;
			for(unsigned k = 0; k < Group.u.object.length; k++)
			{
				const char *pKey = Group.u.object.values[k].name;
				const json_value &Val = *Group.u.object.values[k].value;

				if(str_comp(pKey, "quest_active") == 0 && Val.type == json_integer)
				{
					const int QuestID = (int)(json_int_t)Val;
					CPlayerQuest *pQ = CQuestManager::FindPlayerQuest(pPlayer->GetCID(), QuestID);
					if(!pQ || !pQ->IsAccepted())
					{
						AllMatch = false;
						break;
					}
				}
				else if(str_comp(pKey, "quest_done") == 0 && Val.type == json_integer)
				{
					const int QuestID = (int)(json_int_t)Val;
					CPlayerQuest *pQ = CQuestManager::FindPlayerQuest(pPlayer->GetCID(), QuestID);
					if(!pQ || !pQ->IsCompleted())
					{
						AllMatch = false;
						break;
					}
				}
				else if(str_comp(pKey, "flag") == 0 && Val.type == json_string)
				{
					const char *pFlag = (const char *)Val;
					if(!pPlayer->HasStoryFlag(pFlag))
					{
						AllMatch = false;
						break;
					}
				}
				else if(str_comp(pKey, "not_flag") == 0 && Val.type == json_string)
				{
					const char *pFlag = (const char *)Val;
					if(pPlayer->HasStoryFlag(pFlag))
					{
						AllMatch = false;
						break;
					}
				}
				else if(str_comp(pKey, "reputation_ge") == 0 && Val.type == json_integer)
				{
					if(pPlayer->GetStat(AttributeIdentifier::Reputation) < (int)(json_int_t)Val)
					{
						AllMatch = false;
						break;
					}
				}
				else if(str_comp(pKey, "reputation_lt") == 0 && Val.type == json_integer)
				{
					if(pPlayer->GetStat(AttributeIdentifier::Reputation) >= (int)(json_int_t)Val)
					{
						AllMatch = false;
						break;
					}
				}
				else if(str_comp(pKey, "level_ge") == 0 && Val.type == json_integer)
				{
					if(pPlayer->GetStat(AttributeIdentifier::Level) < (int)(json_int_t)Val)
					{
						AllMatch = false;
						break;
					}
				}
				else if(str_comp(pKey, "level_eq") == 0 && Val.type == json_integer)
				{
					if(pPlayer->GetStat(AttributeIdentifier::Level) != (int)(json_int_t)Val)
					{
						AllMatch = false;
						break;
					}
				}
				else
				{
					// unknown condition — permissive
				}
			}
			if(AllMatch)
			{
				Result = true;
				break;
			}
		}
	}
	else if(pCond->type == json_object)
	{
		// Single object = one AND group
		bool AllMatch = true;
		for(unsigned k = 0; k < pCond->u.object.length; k++)
		{
			const char *pKey = pCond->u.object.values[k].name;
			const json_value &Val = *pCond->u.object.values[k].value;

			if(str_comp(pKey, "quest_active") == 0 && Val.type == json_integer)
			{
				const int QuestID = (int)(json_int_t)Val;
				CPlayerQuest *pQ = CQuestManager::FindPlayerQuest(pPlayer->GetCID(), QuestID);
				if(!pQ || !pQ->IsAccepted())
				{
					AllMatch = false;
					break;
				}
			}
			else if(str_comp(pKey, "quest_done") == 0 && Val.type == json_integer)
			{
				const int QuestID = (int)(json_int_t)Val;
				CPlayerQuest *pQ = CQuestManager::FindPlayerQuest(pPlayer->GetCID(), QuestID);
				if(!pQ || !pQ->IsCompleted())
				{
					AllMatch = false;
					break;
				}
			}
			else if(str_comp(pKey, "flag") == 0 && Val.type == json_string)
			{
				const char *pFlag = (const char *)Val;
				if(!pPlayer->HasStoryFlag(pFlag))
				{
					AllMatch = false;
					break;
				}
			}
			else if(str_comp(pKey, "not_flag") == 0 && Val.type == json_string)
			{
				const char *pFlag = (const char *)Val;
				if(pPlayer->HasStoryFlag(pFlag))
				{
					AllMatch = false;
					break;
				}
			}
			else if(str_comp(pKey, "reputation_ge") == 0 && Val.type == json_integer)
			{
				if(pPlayer->GetStat(AttributeIdentifier::Reputation) < (int)(json_int_t)Val)
				{
					AllMatch = false;
					break;
				}
			}
			else if(str_comp(pKey, "reputation_lt") == 0 && Val.type == json_integer)
			{
				if(pPlayer->GetStat(AttributeIdentifier::Reputation) >= (int)(json_int_t)Val)
				{
					AllMatch = false;
					break;
				}
			}
			else if(str_comp(pKey, "level_ge") == 0 && Val.type == json_integer)
			{
				if(pPlayer->GetStat(AttributeIdentifier::Level) < (int)(json_int_t)Val)
				{
					AllMatch = false;
					break;
				}
			}
			else if(str_comp(pKey, "level_eq") == 0 && Val.type == json_integer)
			{
				if(pPlayer->GetStat(AttributeIdentifier::Level) != (int)(json_int_t)Val)
				{
					AllMatch = false;
					break;
				}
			}
			else
			{
				// unknown — permissive
			}
		}
		if(AllMatch)
			Result = true;
	}

	delete pCond;
	return Result;
}

// ══════════════════════════════════════════════════════════════
//  World lifecycle
// ══════════════════════════════════════════════════════════════

void CNpcManager::OnInitWorld(const char *pWhereLocalWorld)
{
	(void)pWhereLocalWorld;
	DespawnAll();

	// NPC identities are loaded once. Spawns depend on the current world.
	LoadNpcs();
	LoadSpawns();
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

	for(int i = 0; i < m_NumSpawns; i++)
	{
		const int CID = m_aSpawns[i].m_ClientID;
		if(CID < 0)
			continue;
		if(GS()->m_apPlayers[CID])
			GS()->m_apPlayers[CID]->OnDisconnect();
		Server()->DummyRemove(CID);
		m_aSpawns[i].m_ClientID = -1;
	}
	m_PendingSpawnIdx = -1;
}

void CNpcManager::SpawnWorldNpcs()
{
	if(!GS())
		return;

	const int World = GS()->GetWorldID();
	for(int i = 0; i < m_NumSpawns; i++)
	{
		SSpawnDef &Spawn = m_aSpawns[i];
		if(Spawn.m_World != World)
			continue;

		// Check if there's an NPC def that matches this spawn
		const SNpcDef *pDef = FindNpc(Spawn.m_aNpcId);
		if(!pDef)
		{
			dbg_msg("npc", "spawn '%s' references unknown npc '%s'", Spawn.m_aId, Spawn.m_aNpcId);
			continue;
		}

		// If no conditions or conditions empty, always spawn
		if(!Spawn.m_aConditions[0])
		{
			SpawnSpawn(i);
			continue;
		}

		// Conditions present — check against every player in this world
		bool AnyPlayerMatches = false;
		for(int c = 0; c < MAX_CLIENTS; c++)
		{
			CPlayer *pP = GS()->m_apPlayers[c];
			if(!pP || pP->IsDummy())
				continue;
			const int PlayerWorld = Server()->GetClientWorldID(c);
			if(PlayerWorld != World)
				continue;

			if(EvaluateSpawnConditions(Spawn.m_aConditions, pP))
			{
				AnyPlayerMatches = true;
				break;
			}
		}

		if(AnyPlayerMatches)
		{
			SpawnSpawn(i);
		}
	}
}

bool CNpcManager::SpawnSpawn(int SpawnIdx)
{
	if(SpawnIdx < 0 || SpawnIdx >= m_NumSpawns || !GS() || !Server())
		return false;
	if(m_aSpawns[SpawnIdx].m_ClientID >= 0)
		return true;

	// Find the NPC def for this spawn
	const SNpcDef *pDef = FindNpc(m_aSpawns[SpawnIdx].m_aNpcId);
	if(!pDef)
		return false;

	const int Slot = NpcSlotForSpawn(SpawnIdx);
	if(Slot < MAX_HUMAN_CLIENTS || Slot >= MAX_CLIENTS)
		return false;
	if(GS()->m_apPlayers[Slot] || !Server()->IsClientSlotEmpty(Slot))
		return false;

	m_PendingSpawnIdx = SpawnIdx;
	Server()->DummyJoin(Slot, pDef->m_aId, GS()->GetWorldID());

	// Override m_Dummy to false — NPCs should NOT be dummies
	// (Dummy flag causes silent ClientInfo → client hides NPC skin)
	// After DummyJoin: OnClientEnter → BroadcastClientInfo → Silent is already true for
	// ClientInfo at this point, but no humans are connected yet so nothing is sent.
	// When humans connect later, OnClientEnter(human) sends NPC info with Silent=false.
	if(GS()->m_apPlayers[Slot])
	{
		GS()->m_apPlayers[Slot]->SetDummy(false);
		GS()->BroadcastClientInfo(Slot, true);
	}

	return m_aSpawns[SpawnIdx].m_ClientID >= 0;
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

void CNpcManager::SpawnCharacterAt(CPlayer *pPlayer, const SSpawnDef &Def)
{
	if(!pPlayer || !GS())
		return;

	const vec2 Pos = vec2(Def.m_X, Def.m_Y);
	pPlayer->SpawnAt(Pos);
	if(pPlayer->GetCharacter())
	{
		pPlayer->GetCharacter()->SetHealthDirect(100);
	}
}

bool CNpcManager::OnBotPlayerCreated(CPlayer *pPlayer)
{
	if(!pPlayer || m_PendingSpawnIdx < 0 || m_PendingSpawnIdx >= m_NumSpawns)
		return false;

	const int SpawnIdx = m_PendingSpawnIdx;
	m_PendingSpawnIdx = -1;

	SSpawnDef &Spawn = m_aSpawns[SpawnIdx];
	const SNpcDef *pDef = FindNpc(Spawn.m_aNpcId);
	if(!pDef)
		return false;

	pPlayer->InitQuestNpc(SpawnIdx);
	pPlayer->SetTeam(TEAM_RED, false);
	ApplySkin(pPlayer, *pDef);
	SpawnCharacterAt(pPlayer, Spawn);
	Spawn.m_ClientID = pPlayer->GetCID();

	// Apply emote from the NPC def
	if(pPlayer->GetCharacter())
	{
		pPlayer->GetCharacter()->SetEmote(pDef->m_Emote, -1);
	}

	dbg_msg("npc", "spawned '%s' (%s) at world=%d (%.0f,%.0f)",
		Spawn.m_aId, pDef->m_aId, Spawn.m_World, Spawn.m_X, Spawn.m_Y);
	return true;
}

void CNpcManager::OnTick()
{
	if(!GS())
		return;

	// NPC 位置同步
	for(int i = 0; i < m_NumSpawns; i++)
	{
		SSpawnDef &Spawn = m_aSpawns[i];
		if(Spawn.m_ClientID < 0)
			continue;

		CPlayer *pP = GS()->m_apPlayers[Spawn.m_ClientID];
		if(!pP || !pP->IsQuestNpc() || !pP->GetCharacter() || !pP->GetCharacter()->IsAlive())
			continue;

		if(Spawn.m_Static)
			pP->GetCharacter()->SyncSpiderBody(vec2(Spawn.m_X, Spawn.m_Y));
	}

	// Close NPC-bound menus when the player walks away (MRPG-style).
	if(Core() && Core()->VoteMenuManager())
	{
		CVoteMenuManager *pVoteMgr = Core()->VoteMenuManager();
		for(int CID = 0; CID < MAX_CLIENTS; CID++)
		{
			CPlayer *pP = GS()->m_apPlayers[CID];
			if(!pP || pP->IsDummy())
				continue;
			SPlayerVote *pVote = pVoteMgr->GetPlayerVote(CID);
			if(!pVote || !IsNpcServicePage(pVote->m_Page))
				continue;
			if(EnsureNpcServiceAccess(GS(), pP, pVote))
				continue;

			ClearNpcService(pVote);
			pVote->m_Page = PAGE_MENU;
			pVoteMgr->ClearVotes(CID);
			NotifyNpcServiceDenied(GS(), CID);
		}
	}

	// (NPC auto-talk removed — use hammer to talk instead)
}

bool CNpcManager::IsQuestNpc(const CPlayer *pPlayer) const
{
	return pPlayer && pPlayer->IsQuestNpc();
}

bool CNpcManager::IsQuestNpcCharacter(CCharacter *pChr) const
{
	if(!pChr || !GS())
		return false;
	CPlayer *pPlayer = pChr->GetPlayer();
	if(!pPlayer)
		return false;
	const int CID = pChr->GetCID();
	if(CID < 0 || CID >= MAX_CLIENTS)
		return false;
	if(GS()->m_apPlayers[CID] != pPlayer)
		return false;
	return IsQuestNpc(pPlayer);
}

bool CNpcManager::IsPlayerNearNpc(CPlayer *pPlayer, const char *pNpcId, float MaxDist) const
{
	if(!pPlayer || !pNpcId || !pNpcId[0] || !GS())
		return false;
	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr || !pChr->IsAlive())
		return false;

	const vec2 PlayerPos = pChr->GetPos();
	for(int i = 0; i < m_NumSpawns; i++)
	{
		if(str_comp(m_aSpawns[i].m_aNpcId, pNpcId) != 0)
			continue;
		if(m_aSpawns[i].m_ClientID < 0)
			continue;

		CPlayer *pNpcPlayer = GS()->m_apPlayers[m_aSpawns[i].m_ClientID];
		if(!pNpcPlayer || !pNpcPlayer->GetCharacter() || !pNpcPlayer->GetCharacter()->IsAlive())
			continue;

		if(distance(PlayerPos, pNpcPlayer->GetCharacter()->GetPos()) <= MaxDist)
			return true;
	}
	return false;
}

// ══════════════════════════════════════════════════════════════
//  NPC def access (identity only)
// ══════════════════════════════════════════════════════════════

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

// ══════════════════════════════════════════════════════════════
//  Spawn access
// ══════════════════════════════════════════════════════════════

const SSpawnDef *CNpcManager::GetSpawn(int Index) const
{
	if(Index < 0 || Index >= m_NumSpawns)
		return nullptr;
	return &m_aSpawns[Index];
}

const SSpawnDef *CNpcManager::FindSpawn(const char *pSpawnId) const
{
	if(!pSpawnId)
		return nullptr;
	for(int i = 0; i < m_NumSpawns; i++)
	{
		if(str_comp(m_aSpawns[i].m_aId, pSpawnId) == 0)
			return &m_aSpawns[i];
	}
	return nullptr;
}

const SSpawnDef *CNpcManager::FindSpawnNear(CPlayer *pPlayer, vec2 Pos) const
{
	if(!pPlayer || !Server())
		return nullptr;

	const int World = Server()->GetClientWorldID(pPlayer->GetCID());
	float BestDist = 1.0e9f;
	const SSpawnDef *pBest = nullptr;

	for(int i = 0; i < m_NumSpawns; i++)
	{
		const SSpawnDef &S = m_aSpawns[i];
		if(S.m_World != World)
			continue;
		const float Dist = distance(Pos, vec2(S.m_X, S.m_Y));
		if(Dist <= S.m_Radius && Dist < BestDist)
		{
			BestDist = Dist;
			pBest = &S;
		}
	}
	return pBest;
}

const char *CNpcManager::NpcIdForClient(int ClientID) const
{
	for(int i = 0; i < m_NumSpawns; i++)
	{
		if(m_aSpawns[i].m_ClientID == ClientID)
			return m_aSpawns[i].m_aNpcId;
	}
	return nullptr;
}

bool CNpcManager::TryHammerTalk(CCharacter *pChr, vec2 ProjStartPos)
{
	if(!pChr || !pChr->GetPlayer() || pChr->GetPlayer()->IsDummy() || !GS() || !Core())
		return false;

	CPlayer *pTalker = pChr->GetPlayer();
	const vec2 ChrPos = pChr->GetPos();
	const char *pNpcId = nullptr;
	int BestNpcCID = -1;
	float BestDist = (float)NPC_HAMMER_RANGE + 1.f;
	char aSpawnId[NPC_KEY_LEN];
	*aSpawnId = '\0';

	for(CGameWorld::TypeRange r = GS()->m_World.DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
	{
		CCharacter *pTarget = static_cast<CCharacter *>(r.front());
		if(!pTarget || pTarget == pChr || !IsQuestNpcCharacter(pTarget))
			continue;

		const float Dist = distance(pTarget->GetPos(), ChrPos);
		if(Dist > (float)NPC_HAMMER_RANGE || Dist >= BestDist)
			continue;

		if(GS()->Collision()->IntersectLine(ProjStartPos, pTarget->GetPos(), nullptr, nullptr))
			continue;

		pNpcId = NpcIdForClient(pTarget->GetPlayer()->GetCID());
		if(!pNpcId)
			continue;

		// Find the spawn position for this NPC
		for(int s = 0; s < m_NumSpawns; s++)
		{
			if(m_aSpawns[s].m_ClientID == pTarget->GetCID())
			{
				str_copy(aSpawnId, m_aSpawns[s].m_aId, sizeof(aSpawnId));
				break;
			}
		}

		BestDist = Dist;
		BestNpcCID = pTarget->GetCID();
	}

	if(!pNpcId)
		return false;

	// Get position from the matched spawn or fall back to character position
	float NpcX = 0, NpcY = 0;
	int NpcWorld = -1;
	for(int s = 0; s < m_NumSpawns; s++)
	{
		if(str_comp(m_aSpawns[s].m_aId, aSpawnId) == 0)
		{
			NpcX = m_aSpawns[s].m_X;
			NpcY = m_aSpawns[s].m_Y;
			NpcWorld = m_aSpawns[s].m_World;
			break;
		}
	}

	if(Core()->QuestManager())
		Core()->QuestManager()->TryTalkNpc(pTalker, pNpcId, NpcX, NpcY, NpcWorld);
	else if(Core()->DialogManager())
		Core()->DialogManager()->TryTalk(pTalker, pNpcId, NpcX, NpcY, NpcWorld, BestNpcCID);

	const SNpcDef *pDef = FindNpc(pNpcId);
	if(pDef)
	{
		char aName[64];
		str_copy(aName, GS()->Loc(pTalker->GetCID(), pDef->m_aNameKey, pDef->m_aId), sizeof(aName));
		GS()->SendChatLocF(pTalker->GetCID(), "npc.talk.begin", "你开始与「%s」交谈。", aName);
	}

	GS()->m_World.CreateHammerHit(ProjStartPos);
	PlayInteractionSound(GS()->m_World, pTalker, SOUND_GAME_ACCEPT);
	return true;
}
