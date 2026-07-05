/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "account.h"
#include "core/tworld_controller.h"
#include "entities/character.h"
#include <engine/shared/config.h>
#include <game/collision.h>

#include "entities/turret.h"
#include "gamecontext.h"
#include "gamecontroller.h"
#include "core/components/mmo/mmo_manager.h"
#include "core/components/mmo/mmo_types.h"
#include "core/components/mmo/mmo_item.h"
#include "core/components/dialogs/dialog_manager.h"
#include "core/components/skills/skill_defs.h"
#include "core/components/skills/skill_data.h"
#include "core/components/skills/skill_manager.h"
#include "data_center.h"
#include "gameworld.h"
#include "player.h"
#include "mmo_exp.h"

MACRO_ALLOC_POOL_ID_IMPL(CPlayer, MAX_CLIENTS)

IServer *CPlayer::Server() const { return m_pGameServer->Server(); }

CPlayer::CPlayer(CGameContext *pGameServer, int ClientID, bool Dummy, bool AsSpec)
{
	m_pGameServer = pGameServer;
	m_RespawnTick = Server()->Tick();
	m_DieTick = Server()->Tick();
	m_ScoreStartTick = Server()->Tick();
	m_pCharacter = 0;
	m_pTurret = nullptr;
	m_pTurretPreview = nullptr;
	m_TurretPlacing = false;
	m_TurretPlaceLastFire = false;
	m_TurretPlaceFailMsgTick = 0;
	m_TurretPlacePos = vec2(0.0f, 0.0f);
	TurretAmmo_DefaultMix(&m_TurretAmmoMix);
	mem_zero(m_aTurretAmmoDebt, sizeof(m_aTurretAmmoDebt));
	str_copy(m_aLanguage, "zh-cn", sizeof(m_aLanguage));
	m_ClientID = ClientID;
	m_Team = AsSpec ? TEAM_SPECTATORS : (Dummy ? GameServer()->m_pController->GetDummyTeam() : GameServer()->m_pController->GetStartTeam());
	m_SpecMode = SPEC_FREEVIEW;
	m_SpectatorID = -1;
	m_ActiveSpecSwitch = 0;
	m_LastActionTick = Server()->Tick();
	m_TeamChangeTick = Server()->Tick();
	m_NextLoginHintTick = 0;
	m_InactivityTickCounter = 0;
	m_Dummy = Dummy;
	m_IsGuest = false;
	m_IsReadyToPlay = true;
	m_AccountId = -1;
	m_PendingChangeWorldID = -1;
	m_HasPendingChangeWorldPos = false;
	m_PendingChangeWorldPos = vec2(0.0f, 0.0f);
	m_DefencePendingExp = 0;
	m_aFriends.clear();
	m_aFriendsDirty = false;
	m_Zomb = ZOMB_NONE;
	m_QuestNpcDefIdx = -1;
	m_pLastInput = new CNetObj_PlayerInput({0});
	m_LastInputInit = false;
	m_LastDialogTick = 0;
	mem_zero(m_aZombSub, sizeof(m_aZombSub));
	m_ZombVisible = true;
	m_ZamerDetonating = false;
	m_ZombAiLowSpeedTicks = 0;
	m_ZombAiJumpCooldown = 0;
	m_ZombAiLastMoveDir = 1;
	m_ZombAiHookCooldown = 0;
	m_ZombAiHumanScanTick = -1000000;
	m_ZombAiCachedHumanPos = vec2(0.0f, 0.0f);
	m_ZombAiCachedHumanDist = 1.0e12f;
	m_ZombAiCachedHasHuman = false;
	m_ZombAiCachedHumanCid = -1;
	m_ZombAiPathGoal = vec2(1.0e9f, 1.0e9f);
	m_ZombAiMcJumpTried = false;
	mem_zero(&m_ZombAiLastInp, sizeof(m_ZombAiLastInp));
	m_ZombNavLen = 0;
	m_ZombNavIndex = 0;
	ResetAccData();
	m_RespawnDisabled = GameServer()->m_pController->GetStartRespawnState();
	m_DeadSpecMode = false;
	m_Spawning = false;
	mem_zero(&m_Latency, sizeof(m_Latency));
	m_ViewPos = vec2(0.0f, 0.0f);
	m_PlayerFlags = 0;
	m_IsReadyToEnter = false;
	m_Vote = 0;
	m_VotePos = 0;
	m_Score = 0;
	mem_zero(&m_TeeInfos, sizeof(m_TeeInfos));
	mem_zero(m_aActLatency, sizeof(m_aActLatency));
	m_LatestActivity.m_TargetX = 0;
	m_LatestActivity.m_TargetY = 0;
	m_ZombNavNextRebuildTick = 0;
	m_ZombNavCachedGoalTX = 0;
	m_ZombNavCachedGoalTY = 0;

	// MMO defaults (must set before m_aStats copy below)
	m_MMOLevel = 0;
	m_MMOExp = 0;
	m_MMOGold = 0;
	m_MMOSkillPoints = 0;
	m_MMOReputation = 0;
	m_MMOAttack = 5;
	m_MMODefense = 3;
	m_MMODirty = false;

	// Initialize m_aStats from old MMO fields for backward compatibility
	m_aStats[AttributeIdentifier::Level] = m_MMOLevel;
	m_aStats[AttributeIdentifier::Experience] = m_MMOExp;
	m_aStats[AttributeIdentifier::Gold] = m_MMOGold;
	m_aStats[AttributeIdentifier::SkillPoints] = m_MMOSkillPoints;
	m_aStats[AttributeIdentifier::Reputation] = m_MMOReputation;
	m_aStats[AttributeIdentifier::Attack] = m_MMOAttack;
	m_aStats[AttributeIdentifier::Defense] = m_MMODefense;

	// Skill slots 3/4/5: empty by default
	m_aSkillSlots[0] = -1;
	m_aSkillSlots[1] = -1;
	m_aSkillSlots[2] = -1;

	m_aItemQuickSlots[0] = -1;
	m_aItemQuickSlots[1] = -1;
	m_aItemQuickSlots[2] = -1;
	m_aItemQuickSlots[3] = -1;
	InitWeaponLoadouts();
}

CPlayer::~CPlayer()
{
	delete m_pLastInput;
	m_pLastInput = nullptr;
	delete m_pTurret;
	m_pTurret = nullptr;
	delete m_pTurretPreview;
	m_pTurretPreview = nullptr;
	delete m_pCharacter;
	m_pCharacter = 0;
}

void CPlayer::ResetAccData()
{
	mem_zero(&m_AccData, sizeof(m_AccData));
	str_copy(m_AccData.m_aLanguage, "zh-cn", sizeof(m_AccData.m_aLanguage));
	for(int i = 0; i < NUM_ITEM; i++)
		str_copy(m_AccData.m_aItems[i].m_aExtra, "{\"Extra\":{\"Cards\":[],\"Parts\":[]}}", sizeof(m_AccData.m_aItems[i].m_aExtra));
}

void CPlayer::ClearAccount()
{
	m_AccountId = -1;
	ResetAccData();
}

void CPlayer::SetZombSub(int i, int Type)
{
	if(i >= 0 && i < NUM_ZOMB_SUB)
		m_aZombSub[i] = Type;
}

bool CPlayer::HasZombType(int Type) const
{
	if(m_Zomb == Type)
		return true;
	for(int i = 0; i < NUM_ZOMB_SUB; i++)
		if(m_aZombSub[i] == Type)
			return true;
	return false;
}

void CPlayer::InitQuestNpc(int DefIdx)
{
	m_RespawnDisabled = true;
	m_Spawning = false;
	m_DeadSpecMode = false;
	m_Zomb = ZOMB_NONE;
	m_QuestNpcDefIdx = DefIdx;
	mem_zero(m_aZombSub, sizeof(m_aZombSub));
	m_ZombVisible = true;
}

float CPlayer::GetActiveDistance() const
{
	if(m_pMMOBotData)
	{
		const SMMOMobDef *pDef = CDataCenter::FindMobDef(m_pMMOBotData->m_DefID);
		if(pDef && pDef->m_ActiveRadius > 1.f)
			return pDef->m_ActiveRadius;
	}
	return (float)GameServer()->Config()->m_SvMapDistanceActiveBot;
}

bool CPlayer::IsSnappingInactiveForClient(int ClientID) const
{
	if(ClientID < 0 || ClientID >= MAX_HUMAN_CLIENTS)
		return true;
	if(!GameServer()->m_apPlayers[ClientID])
		return true;
	if(m_pQuestMobInfo && !m_pQuestMobInfo->m_ActiveForClient[ClientID])
		return true;
	if(m_pMMOBotData && m_pMMOBotData->m_IsQuestMob && m_pMMOBotData->m_NumActiveForClients > 0 &&
		!m_pMMOBotData->m_aActiveForClients[ClientID])
		return true;
	return false;
}

bool CPlayer::IsVisibleForClient(int ClientID) const
{
	if(m_ClientID < MAX_HUMAN_CLIENTS)
		return true;
	if(!GameServer()->m_World.IsBotActive(m_ClientID))
		return false;
	return !IsSnappingInactiveForClient(ClientID);
}

int CPlayer::GetStat(AttributeIdentifier ID) const
{
	auto it = m_aStats.find(ID);
	if(it != m_aStats.end())
		return it->second;
	return 0;
}

void CPlayer::SetStat(AttributeIdentifier ID, int Value)
{
	const int OldValue = GetStat(ID);
	if(OldValue == Value)
		return;

	m_aStats[ID] = Value;
	// Sync old fields for backward compatibility
	switch(ID)
	{
	case AttributeIdentifier::Level:
		m_MMOLevel = Value;
		if(m_pCharacter && !IsDummy())
		{
			const int NewMax = GetMaxHealth();
			const int OldMax = m_pCharacter->GetMaxHealth();
			if(NewMax > OldMax)
				m_pCharacter->AddMaxHealth(NewMax - OldMax);
		}
		break;
	case AttributeIdentifier::Experience: m_MMOExp = Value; break;
	case AttributeIdentifier::Gold: m_MMOGold = Value; break;
	case AttributeIdentifier::SkillPoints: m_MMOSkillPoints = Value; break;
	case AttributeIdentifier::Reputation: m_MMOReputation = Value; break;
	case AttributeIdentifier::Attack: m_MMOAttack = Value; break;
	case AttributeIdentifier::Defense: m_MMODefense = Value; break;
	default: break;
	}

	if(!IsDummy() && (ID == AttributeIdentifier::Level || ID == AttributeIdentifier::Gold))
		GameServer()->MarkUpdatedBroadcast(m_ClientID);
}

int CPlayer::GetStoryFlag(const char *pKey) const
{
	auto it = m_StoryFlags.find(pKey);
	if(it == m_StoryFlags.end()) return 0;
	return it->second;
}

void CPlayer::SetStoryFlag(const char *pKey, int Value)
{
	if(Value == 0)
		m_StoryFlags.erase(pKey);
	else
		m_StoryFlags[pKey] = Value;
	m_MMODirty = true;
}

void CPlayer::SyncStatsFromFields()
{
	m_aStats[AttributeIdentifier::Level] = m_MMOLevel;
	m_aStats[AttributeIdentifier::Experience] = m_MMOExp;
	m_aStats[AttributeIdentifier::Gold] = m_MMOGold;
	m_aStats[AttributeIdentifier::SkillPoints] = m_MMOSkillPoints;
	m_aStats[AttributeIdentifier::Reputation] = m_MMOReputation;
	// TRPG六维 — default 3 if not yet loaded from DB
	if(m_aStats.find(AttributeIdentifier::STR) == m_aStats.end()) m_aStats[AttributeIdentifier::STR] = 3;
	if(m_aStats.find(AttributeIdentifier::DEX) == m_aStats.end()) m_aStats[AttributeIdentifier::DEX] = 3;
	if(m_aStats.find(AttributeIdentifier::CON) == m_aStats.end()) m_aStats[AttributeIdentifier::CON] = 3;
	if(m_aStats.find(AttributeIdentifier::INT) == m_aStats.end()) m_aStats[AttributeIdentifier::INT] = 3;
	if(m_aStats.find(AttributeIdentifier::WIS) == m_aStats.end()) m_aStats[AttributeIdentifier::WIS] = 3;
	if(m_aStats.find(AttributeIdentifier::CHA) == m_aStats.end()) m_aStats[AttributeIdentifier::CHA] = 3;
}

void CPlayer::InitZombie(int Zomb)
{
	m_QuestNpcDefIdx = -1;
	m_RespawnDisabled = false;
	m_Spawning = false;
	m_DeadSpecMode = false;
	m_Zomb = Zomb;
	mem_zero(m_aZombSub, sizeof(m_aZombSub));
	m_ZombVisible = Zomb != ZOMB_ZINVIS;
	m_ZombAiLowSpeedTicks = 0;
	m_ZombAiJumpCooldown = 0;
	m_ZombAiLastMoveDir = 1;
	m_ZombAiHookCooldown = 0;
	m_ZombAiHumanScanTick = -1000000;
	m_ZombAiCachedHumanPos = vec2(0.0f, 0.0f);
	m_ZombAiCachedHumanDist = 1.0e12f;
	m_ZombAiCachedHasHuman = false;
	m_ZombAiCachedHumanCid = -1;
	m_ZombAiPathGoal = vec2(1.0e9f, 1.0e9f);
	m_ZombAiMcJumpTried = false;
	mem_zero(&m_ZombAiLastInp, sizeof(m_ZombAiLastInp));
	m_ZombNavLen = 0;
	m_ZombNavIndex = 0;
	switch(Zomb)
	{
	case ZOMB_ZABY:
		Server()->SetClientName(GetCID(), "Zaby");
		break;
	case ZOMB_ZOOMER:
		Server()->SetClientName(GetCID(), "Zoomer");
		break;
	case ZOMB_ZOOKER:
		Server()->SetClientName(GetCID(), "Zooker");
		break;
	case ZOMB_ZAMER:
		Server()->SetClientName(GetCID(), "Zamer");
		break;
	case ZOMB_ZUNNER:
		Server()->SetClientName(GetCID(), "Zunner");
		break;
	case ZOMB_ZASTER:
		Server()->SetClientName(GetCID(), "Zaster");
		break;
	case ZOMB_ZOTTER:
		Server()->SetClientName(GetCID(), "Zotter");
		break;
	case ZOMB_ZENADE:
		Server()->SetClientName(GetCID(), "Zenade");
		break;
	case ZOMB_FLOMBIE:
		Server()->SetClientName(GetCID(), "Flombie");
		break;
	case ZOMB_ZINJA:
		Server()->SetClientName(GetCID(), "Zinja");
		break;
	case ZOMB_ZELE:
		Server()->SetClientName(GetCID(), "Zele");
		break;
	case ZOMB_ZINVIS:
		Server()->SetClientName(GetCID(), "Zinvis");
		break;
	case ZOMB_ZEATER:
		Server()->SetClientName(GetCID(), "Zeater");
		break;
	case ZOMB_ZSHIELD:
		Server()->SetClientName(GetCID(), "Zshield");
		break;
	case ZOMB_ZHEALER:
		Server()->SetClientName(GetCID(), "Zhealer");
		break;
	case ZOMB_ZSPLITTER:
		Server()->SetClientName(GetCID(), "Zsplitter");
		break;
	case ZOMB_SPIDER_BOSS:
		Server()->SetClientName(GetCID(), "Spider");
		break;
	default:
		break;
	}
}

bool CPlayer::PressTab() const
{
	return (m_PlayerFlags & PLAYERFLAG_SCOREBOARD) != 0;
}

void CPlayer::SetTurretAmmoMatPct(int MatSlot, int Pct)
{
	if(MatSlot < 0 || MatSlot >= NUM_TURRET_AMMO_MATS)
		return;
	m_TurretAmmoMix.m_aPct[MatSlot] = clamp(Pct, 0, 100);
	TurretAmmo_NormalizeMix(&m_TurretAmmoMix);
	TurretAmmo_ClearDebt(this);
}

void CPlayer::SetLanguage(const char *pLang)
{
	if(!pLang || !pLang[0])
		return;

	char aNorm[64];
	str_copy(aNorm, pLang, sizeof(aNorm));
	str_utf8_trim_whitespaces_right(aNorm);

	const char *pStored = "zh-cn";
	if(str_comp_nocase(aNorm, "en") == 0 || str_comp_nocase(aNorm, "english") == 0)
		pStored = "en";
	else if(str_comp_nocase(aNorm, "zh") == 0 || str_comp_nocase(aNorm, "zh-cn") == 0 || str_comp_nocase(aNorm, "zh_cn") == 0 ||
		str_comp_nocase(aNorm, "cn") == 0 || str_comp_nocase(aNorm, "chinese") == 0 || str_comp_nocase(aNorm, "中文") == 0)
		pStored = "zh-cn";

	str_copy(m_aLanguage, pStored, sizeof(m_aLanguage));
	str_copy(m_AccData.m_aLanguage, pStored, sizeof(m_AccData.m_aLanguage));
}

const char *CPlayer::GetExtra(int ItemType) const
{
	const int Id = (ItemType >= 0 && ItemType < NUM_ITYPE) ? m_AccData.m_Holding[ItemType] : 0;
	return GetExtraForItem(Id);
}

const char *CPlayer::GetExtraForItem(int ItemID) const
{
	if(ItemID < 0 || ItemID >= NUM_ITEM)
		return "";
	return m_AccData.m_aItems[ItemID].m_aExtra;
}

bool CPlayer::PendingChangeWorld()
{
	if(m_PendingChangeWorldID < 0)
		return false;

	const int OldWorldID = Server()->GetClientWorldID(m_ClientID);
	const int WorldID = m_PendingChangeWorldID;
	if(m_HasPendingChangeWorldPos)
		Server()->SetChangeWorldSpawnPos(m_ClientID, m_PendingChangeWorldPos);
	m_PendingChangeWorldID = -1;
	m_HasPendingChangeWorldPos = false;

	Server()->ChangeWorld(m_ClientID, WorldID);

	// Flush Defence → RPG reward bridge when leaving Defence world
	if(m_DefencePendingExp > 0)
	{
		const CWorldDetail *pOldDetail = Server()->GetWorldDetail(OldWorldID);
		if(pOldDetail && pOldDetail->GetType() == WorldType::Defence)
		{
			int Exp = m_DefencePendingExp;
			m_DefencePendingExp = 0;
			AddMMOExperience(Exp);

			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "🎁 塔防结算: 获得 %d 经验值！", Exp);
			GameServer()->SendChat(m_ClientID, CHAT_ALL, -1, aBuf);
		}
	}

	return true;
}

void CPlayer::ChangeWorld(int WorldID, vec2 *pPos)
{
	if(WorldID < 0 || WorldID >= Server()->GetNumWorlds())
		return;
	m_PendingChangeWorldID = WorldID;
	if(pPos)
	{
		m_HasPendingChangeWorldPos = true;
		m_PendingChangeWorldPos = *pPos;
	}
	else
	{
		m_HasPendingChangeWorldPos = false;
	}
}

int CPlayer::GetCurrentWorldID() const
{
	return Server()->GetClientWorldID(m_ClientID);
}

void CPlayer::Tick()
{
	if(!IsDummy() && !Server()->ClientIngame(m_ClientID))
		return;

	if(PendingChangeWorld())
		return;

	// MotdMenu tick (MRPG-style input tracking)
	if(m_pMotdMenu)
		m_pMotdMenu->Tick();

	Server()->SetClientScore(m_ClientID, m_Score);

	// do latency stuff
	{
		IServer::CClientInfo Info;
		if(Server()->GetClientInfo(m_ClientID, &Info))
		{
			m_Latency.m_Accum += Info.m_Latency;
			m_Latency.m_AccumMax = maximum(m_Latency.m_AccumMax, Info.m_Latency);
			m_Latency.m_AccumMin = minimum(m_Latency.m_AccumMin, Info.m_Latency);
		}
		// each second
		if(Server()->Tick() % Server()->TickSpeed() == 0)
		{
			m_Latency.m_Avg = m_Latency.m_Accum / Server()->TickSpeed();
			m_Latency.m_Max = m_Latency.m_AccumMax;
			m_Latency.m_Min = m_Latency.m_AccumMin;
			m_Latency.m_Accum = 0;
			m_Latency.m_AccumMin = 1000;
			m_Latency.m_AccumMax = 0;
		}
	}

	if(m_pCharacter && !m_pCharacter->IsAlive())
	{
		delete m_pCharacter;
		m_pCharacter = 0;
	}

	if(!m_pCharacter && m_Team == TEAM_SPECTATORS && m_SpecMode == SPEC_FREEVIEW)
		m_ViewPos -= vec2(clamp(m_ViewPos.x - m_LatestActivity.m_TargetX, -500.0f, 500.0f), clamp(m_ViewPos.y - m_LatestActivity.m_TargetY, -400.0f, 400.0f));

	if(!m_pCharacter && m_DieTick + Server()->TickSpeed() * 3 <= Server()->Tick() && !m_DeadSpecMode && !m_RespawnDisabled)
		Respawn();

	if(m_pCharacter)
	{
		if(m_pCharacter->IsAlive())
			m_ViewPos = m_pCharacter->GetPos();
	}
	else if(m_Spawning && m_RespawnTick <= Server()->Tick())
		TryRespawn();

	if(!m_DeadSpecMode && m_LastActionTick != Server()->Tick())
		++m_InactivityTickCounter;

	// Refresh HUD overlay every ~3s (BroadcastTick also refreshes on stat changes via MarkUpdatedBroadcast)
	if(!IsDummy() && m_pCharacter && m_pCharacter->IsAlive() && Server()->Tick() % (Server()->TickSpeed() * 3) == 0)
		GameServer()->MarkUpdatedBroadcast(m_ClientID);
}

void CPlayer::PostTick()
{
	// update latency value
	if(m_PlayerFlags & PLAYERFLAG_SCOREBOARD)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
				m_aActLatency[i] = GameServer()->m_apPlayers[i]->m_Latency.m_Min;
		}
	}

	// update view pos for spectators and dead players
	if((m_Team == TEAM_SPECTATORS || m_DeadSpecMode) && m_SpecMode != SPEC_FREEVIEW)
	{
		if(GameServer()->m_apPlayers[m_SpectatorID])
			m_ViewPos = GameServer()->m_apPlayers[m_SpectatorID]->m_ViewPos;
	}
}

void CPlayer::Snap(int SnappingClient)
{
	if(m_ClientID >= MAX_HUMAN_CLIENTS)
	{
		if(SnappingClient >= 0)
		{
			if(!IsVisibleForClient(SnappingClient))
				return;
		}
	}
	else if(!IsDummy() && !Server()->ClientIngame(m_ClientID))
		return;

	int SnapID = m_ClientID;
	if(SnappingClient >= 0 && m_ClientID >= MAX_HUMAN_CLIENTS && !Server()->Translate(SnapID, SnappingClient))
		return;

	CNetObj_PlayerInfo *pPlayerInfo = static_cast<CNetObj_PlayerInfo *>(Server()->SnapNewItem(NETOBJTYPE_PLAYERINFO, SnapID, sizeof(CNetObj_PlayerInfo)));
	if(!pPlayerInfo)
		return;

	pPlayerInfo->m_PlayerFlags = m_PlayerFlags & PLAYERFLAG_CHATTING;
	if(Server()->IsAuthed(m_ClientID))
		pPlayerInfo->m_PlayerFlags |= PLAYERFLAG_ADMIN;
	if(m_IsReadyToPlay)
		pPlayerInfo->m_PlayerFlags |= PLAYERFLAG_READY;
	if(m_RespawnDisabled && (!GetCharacter() || !GetCharacter()->IsAlive()))
		pPlayerInfo->m_PlayerFlags |= PLAYERFLAG_DEAD;
	if(SnappingClient != -1 && (m_Team == TEAM_SPECTATORS || m_DeadSpecMode) && (SnappingClient == m_SpectatorID))
		pPlayerInfo->m_PlayerFlags |= PLAYERFLAG_WATCHING;

	pPlayerInfo->m_Latency = SnappingClient == -1 ? m_Latency.m_Min : GameServer()->m_apPlayers[SnappingClient]->m_aActLatency[m_ClientID];
	pPlayerInfo->m_Score = m_Score;

	const bool ZombieBot = IsDummy() && m_Zomb != ZOMB_NONE;
	const bool MMOBot = m_pMMOBotData != nullptr;
	const bool MappedSlot = SnapID != m_ClientID;
	if((MappedSlot || ZombieBot || MMOBot) && !IsQuestNpc())
	{
		CNetObj_PlayerInfoExtra *pPlayerInfoExtra = static_cast<CNetObj_PlayerInfoExtra *>(Server()->SnapNewItem(NETOBJTYPE_PLAYERINFOEXTRA, SnapID, sizeof(CNetObj_PlayerInfoExtra)));
		if(pPlayerInfoExtra)
		{
			pPlayerInfoExtra->m_RealClientID = m_ClientID;
			pPlayerInfoExtra->m_PlayerFlagsExtra = (ZombieBot || MMOBot) ? PLAYERFLAGEXTRA_HIDDEN_IN_BOARD : 0;
		}
	}

	if(m_ClientID == SnappingClient && (m_Team == TEAM_SPECTATORS || m_DeadSpecMode))
	{
		CNetObj_SpectatorInfo *pSpectatorInfo = static_cast<CNetObj_SpectatorInfo *>(Server()->SnapNewItem(NETOBJTYPE_SPECTATORINFO, SnapID, sizeof(CNetObj_SpectatorInfo)));
		if(!pSpectatorInfo)
			return;

		pSpectatorInfo->m_SpecMode = m_SpecMode;
		pSpectatorInfo->m_SpectatorID = m_SpectatorID;
		pSpectatorInfo->m_X = m_ViewPos.x;
		pSpectatorInfo->m_Y = m_ViewPos.y;
	}

	// demo recording
	if(SnappingClient == -1)
	{
		CNetObj_De_ClientInfo *pClientInfo = static_cast<CNetObj_De_ClientInfo *>(Server()->SnapNewItem(NETOBJTYPE_DE_CLIENTINFO, m_ClientID, sizeof(CNetObj_De_ClientInfo)));
		if(!pClientInfo)
			return;

		pClientInfo->m_Local = 0;
		pClientInfo->m_Team = m_Team;
		StrToInts(pClientInfo->m_aName, 4, Server()->ClientName(m_ClientID));
		StrToInts(pClientInfo->m_aClan, 3, Server()->ClientClan(m_ClientID));
		pClientInfo->m_Country = Server()->ClientCountry(m_ClientID);

		for(int p = 0; p < NUM_SKINPARTS; p++)
		{
			StrToInts(pClientInfo->m_aaSkinPartNames[p], 6, m_TeeInfos.m_aaSkinPartNames[p]);
			pClientInfo->m_aUseCustomColors[p] = m_TeeInfos.m_aUseCustomColors[p];
			pClientInfo->m_aSkinPartColors[p] = m_TeeInfos.m_aSkinPartColors[p];
		}
	}
}

void CPlayer::SnapPlayerInfoOnly(int SnappingClient, CGameContext *pSnappingCtx)
{
	if(m_ClientID >= MAX_HUMAN_CLIENTS)
	{
		if(SnappingClient >= 0 && !IsVisibleForClient(SnappingClient))
			return;
	}
	else if(!IsDummy() && !Server()->ClientIngame(m_ClientID))
		return;

	int SnapID = m_ClientID;
	if(SnappingClient >= 0 && m_ClientID >= MAX_HUMAN_CLIENTS && !Server()->Translate(SnapID, SnappingClient))
		return;

	CNetObj_PlayerInfo *pPlayerInfo = static_cast<CNetObj_PlayerInfo *>(Server()->SnapNewItem(NETOBJTYPE_PLAYERINFO, SnapID, sizeof(CNetObj_PlayerInfo)));
	if(!pPlayerInfo)
		return;

	pPlayerInfo->m_PlayerFlags = m_PlayerFlags & PLAYERFLAG_CHATTING;
	if(Server()->IsAuthed(m_ClientID))
		pPlayerInfo->m_PlayerFlags |= PLAYERFLAG_ADMIN;
	if(m_IsReadyToPlay)
		pPlayerInfo->m_PlayerFlags |= PLAYERFLAG_READY;
	// Cross-world players are always shown as dead/spectator
	pPlayerInfo->m_PlayerFlags |= PLAYERFLAG_DEAD;
	if(m_RespawnDisabled && (!GetCharacter() || !GetCharacter()->IsAlive()))
		pPlayerInfo->m_PlayerFlags |= PLAYERFLAG_DEAD;
	if(SnappingClient != -1 && (m_Team == TEAM_SPECTATORS || m_DeadSpecMode) && (SnappingClient == m_SpectatorID))
		pPlayerInfo->m_PlayerFlags |= PLAYERFLAG_WATCHING;

	if(SnappingClient == -1)
		pPlayerInfo->m_Latency = m_Latency.m_Min;
	else if(pSnappingCtx && pSnappingCtx->m_apPlayers[SnappingClient])
		pPlayerInfo->m_Latency = pSnappingCtx->m_apPlayers[SnappingClient]->m_aActLatency[m_ClientID];
	else
		pPlayerInfo->m_Latency = m_Latency.m_Min;
	pPlayerInfo->m_Score = m_Score;

	const bool ZombieBot = IsDummy() && m_Zomb != ZOMB_NONE;
	const bool MMOBot = m_pMMOBotData != nullptr;
	const bool MappedSlot = SnapID != m_ClientID;
	if((MappedSlot || ZombieBot || MMOBot) && !IsQuestNpc())
	{
		CNetObj_PlayerInfoExtra *pPlayerInfoExtra = static_cast<CNetObj_PlayerInfoExtra *>(Server()->SnapNewItem(NETOBJTYPE_PLAYERINFOEXTRA, SnapID, sizeof(CNetObj_PlayerInfoExtra)));
		if(pPlayerInfoExtra)
		{
			pPlayerInfoExtra->m_RealClientID = m_ClientID;
			pPlayerInfoExtra->m_PlayerFlagsExtra = (ZombieBot || MMOBot) ? PLAYERFLAGEXTRA_HIDDEN_IN_BOARD : 0;
		}
	}
}

void CPlayer::OnDisconnect()
{
	DestroyTurret();

	m_pGameServer->Accounts()->OnClientDisconnect(m_ClientID);

	KillCharacter();

	if(m_Team != TEAM_SPECTATORS)
	{
		// update spectator modes
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->m_SpecMode == SPEC_PLAYER && GameServer()->m_apPlayers[i]->m_SpectatorID == m_ClientID)
			{
				if(GameServer()->m_apPlayers[i]->m_DeadSpecMode)
					GameServer()->m_apPlayers[i]->UpdateDeadSpecMode();
				else
				{
					GameServer()->m_apPlayers[i]->m_SpecMode = SPEC_FREEVIEW;
					GameServer()->m_apPlayers[i]->m_SpectatorID = -1;
				}
			}
		}
	}
}

void CPlayer::OnPredictedInput(CNetObj_PlayerInput *NewInput)
{
	// skip the input if chat is active
	if((m_PlayerFlags & PLAYERFLAG_CHATTING) && (NewInput->m_PlayerFlags & PLAYERFLAG_CHATTING))
		return;

	if(m_pCharacter)
	{
		if(m_TurretPlacing)
		{
			CNetObj_PlayerInput AimOnly = *NewInput;
			AimOnly.m_Fire = 0;
			m_pCharacter->OnPredictedInput(&AimOnly);
			UpdateTurretPlaceFromAim();
		}
		else
			m_pCharacter->OnPredictedInput(NewInput);
	}
}

void CPlayer::OnDirectInput(CNetObj_PlayerInput *NewInput)
{
	// Initialize last input on first call
	if(!m_LastInputInit)
	{
		*m_pLastInput = *NewInput;
		m_LastInputInit = true;
	}

	// Parse event keys (MRPG-style)
	Server()->Input()->ParseInputClickedKeys(m_ClientID, NewInput, m_pLastInput);

	// Character input event tracking
	if(m_pCharacter && !(NewInput->m_PlayerFlags & PLAYERFLAG_CHATTING))
	{
		const int ActiveWeapon = m_pCharacter->GetActiveWeapon();
		Server()->Input()->ProcessCharacterInput(m_ClientID, ActiveWeapon, NewInput, m_pLastInput);
	}

	// ── Chat state handling ──
	if(NewInput->m_PlayerFlags & PLAYERFLAG_CHATTING)
	{
		if(!(m_PlayerFlags & PLAYERFLAG_CHATTING))
		{
			if(m_pCharacter)
				m_pCharacter->ResetInput();
		}
		m_PlayerFlags = NewInput->m_PlayerFlags;
		*m_pLastInput = *NewInput;
		// MRPG: hide HUD overlay while typing
		GameServer()->AddBroadcast(m_ClientID, "", CGameContext::BROADCAST_PRIORITY_OVERLAY_HIDDEN, 100);
		return;
	}

	m_PlayerFlags = NewInput->m_PlayerFlags;

	if(m_TurretPlacing && m_pCharacter && m_pCharacter->IsAlive())
	{
		CNetObj_PlayerInput AimOnly = *NewInput;
		AimOnly.m_Fire = 0;
		m_pCharacter->OnDirectInput(&AimOnly);
		UpdateTurretPlaceFromAim();
		const bool FireNow = (NewInput->m_Fire & 1) != 0;
		const bool FireEdge = FireNow && !m_TurretPlaceLastFire;
		m_TurretPlaceLastFire = FireNow;
		if(FireEdge)
		{
			if(ConfirmTurretPlace())
			{
				GameServer()->SendChatLoc(m_ClientID, "vote.turret.deploy_ok", "Turret deployed.");
				GameServer()->m_World.CreateSound(m_pCharacter->GetPos(), SOUND_PICKUP_ARMOR, CmaskOne(m_ClientID));
			}
			else if(Server()->Tick() - m_TurretPlaceFailMsgTick >= Server()->TickSpeed())
			{
				GameServer()->SendChatLoc(m_ClientID, "vote.turret.place_invalid", "Cannot deploy: invalid spot or out of range.");
				m_TurretPlaceFailMsgTick = Server()->Tick();
			}
		}
	}
	else if(m_pCharacter)
		m_pCharacter->OnDirectInput(NewInput);

	if(!m_pCharacter && m_Team != TEAM_SPECTATORS && (NewInput->m_Fire & 1))
		Respawn();

	if(!m_pCharacter && m_Team == TEAM_SPECTATORS && (NewInput->m_Fire & 1))
	{
		if(!m_ActiveSpecSwitch)
		{
			m_ActiveSpecSwitch = true;
			if(m_SpecMode == SPEC_FREEVIEW)
			{
				CCharacter *pChar = (CCharacter *) GameServer()->m_World.ClosestEntity(m_ViewPos, 6.0f * 32, CGameWorld::ENTTYPE_CHARACTER, 0);
				if(pChar)
				{
					m_SpecMode = SPEC_PLAYER;
					m_SpectatorID = pChar->GetCID();
				}
			}
			else
			{
				m_SpecMode = SPEC_FREEVIEW;
				m_SpectatorID = -1;
			}
		}
	}
	else if(m_ActiveSpecSwitch)
		m_ActiveSpecSwitch = false;

	// check for activity
	if(NewInput->m_Direction || m_LatestActivity.m_TargetX != NewInput->m_TargetX ||
		m_LatestActivity.m_TargetY != NewInput->m_TargetY || NewInput->m_Jump ||
		NewInput->m_Fire & 1 || NewInput->m_Hook)
	{
		m_LatestActivity.m_TargetX = NewInput->m_TargetX;
		m_LatestActivity.m_TargetY = NewInput->m_TargetY;
		m_LastActionTick = Server()->Tick();
		m_InactivityTickCounter = 0;
	}

	// Save last input for next tick's key event detection
	*m_pLastInput = *NewInput;
}

bool CPlayer::ParseVoteOptionResult(int Vote)
{
	const bool IsVoteNo = Vote == -1 || Vote == 0;
	const bool IsVoteYes = Vote == 1;

	if(IsVoteYes)
		return false;

	if(!IsVoteNo)
		return false;

	CDialogManager *pDM = GameServer()->Core() ? GameServer()->Core()->DialogManager() : nullptr;
	if(!pDM)
		return false;

	if(pDM->HasActiveDialog(m_ClientID))
	{
		if(m_LastDialogTick && m_LastDialogTick > Server()->Tick())
			return true;

		m_LastDialogTick = Server()->Tick() + Server()->TickSpeed() / 4;
		if(GetCharacter())
			GameServer()->m_World.CreateSound(GetCharacter()->GetPos(), SOUND_PICKUP_ARMOR, CmaskOne(m_ClientID));
		return pDM->HandleDialogCommand(this, "next");
	}

	return pDM->HandleVoteInput(this, Vote);
}

CCharacter *CPlayer::GetCharacter()
{
	if(m_pCharacter && m_pCharacter->IsAlive())
		return m_pCharacter;
	return 0;
}

static vec2 TurretOffsetFromSurface(vec2 Hit, vec2 Before)
{
	vec2 Normal = Before - Hit;
	if(length(Normal) < 0.5f)
		Normal = vec2(0.0f, -1.0f);
	else
		Normal = normalize(Normal);
	return Before + Normal * 22.0f;
}

static vec2 TurretAimToWorldPos(CGameContext *pGame, CCharacter *pChr)
{
	const CNetObj_PlayerInput &In = pChr->LatestInput();
	vec2 Aim((float)In.m_TargetX, (float)In.m_TargetY);
	const vec2 From = pChr->GetPos();
	CCollision *pCol = pGame->Collision();

	if(length(Aim) < 1.0f)
		return From;

	vec2 Dir = normalize(Aim);
	const float MaxDist = 520.0f;
	const vec2 To = From + Dir * MaxDist;

	vec2 Hit, Before;
	if(pCol->IntersectLine(From, To, &Hit, &Before))
		return TurretOffsetFromSurface(Hit, Before);

	return To;
}

static bool TurretHasMountSurface(CCollision *pCol, vec2 Pos)
{
	const float Probe = 26.0f;
	static const vec2 s_aDirs[] = {
		vec2(0.0f, Probe), vec2(0.0f, -Probe), vec2(Probe, 0.0f), vec2(-Probe, 0.0f)};
	for(unsigned i = 0; i < sizeof(s_aDirs) / sizeof(s_aDirs[0]); i++)
	{
		if(pCol->CheckPoint(Pos + s_aDirs[i]))
			return true;
	}
	return false;
}

static bool TurretPlacementValid(CGameContext *pGame, vec2 Pos, int OwnerCid)
{
	if(!pGame)
		return false;
	CCollision *pCol = pGame->Collision();
	if(!pCol)
		return false;

	CPlayer *pOwner = pGame->m_apPlayers[OwnerCid];
	CCharacter *pChr = pOwner ? pOwner->GetCharacter() : nullptr;
	if(!pChr)
		return false;
	if(distance(Pos, pChr->GetPos()) > 520.0f)
		return false;

	if(pCol->CheckPoint(Pos))
		return false;

	if(!TurretHasMountSurface(pCol, Pos))
		return false;

	// Torso clearance only — avoids counting the ground under the feet as a blocker.
	const vec2 TorsoPos(Pos.x, Pos.y - 14.0f);
	const vec2 TorsoBox(22.0f, 18.0f);
	if(pCol->TestBox(TorsoPos, TorsoBox))
		return false;

	for(CGameWorld::TypeRange r = pGame->m_World.DoTypeRange(CGameWorld::ENTTYPE_TURRET); !r.empty(); r.pop_front())
	{
		CTurret *pT = static_cast<CTurret *>(r.front());
		if(!pT || pT->GetOwner() == OwnerCid)
			continue;
		if(distance(Pos, pT->GetPos()) < 96.0f)
			return false;
	}

	const float TowerBlock = 72.0f;
	for(CGameWorld::TypeRange r = pGame->m_World.DoTypeRange(CGameWorld::ENTTYPE_TOWERMAIN); !r.empty(); r.pop_front())
	{
		CEntity *pEnt = r.front();
		if(pEnt && distance(Pos, pEnt->GetPos()) < TowerBlock)
			return false;
	}

	return true;
}

bool CPlayer::IsTurretPlaceValid() const
{
	return m_TurretPlacing && TurretPlacementValid(m_pGameServer, m_TurretPlacePos, m_ClientID);
}

void CPlayer::CancelTurretPlace()
{
	m_TurretPlacing = false;
	m_TurretPlaceLastFire = false;
	delete m_pTurretPreview;
	m_pTurretPreview = nullptr;
}

bool CPlayer::BeginTurretPlace()
{
	CCharacter *pChr = GetCharacter();
	if(!pChr || GetHolding(ITYPE_TURRET) <= 0)
		return false;

	CancelTurretPlace();
	m_TurretPlacing = true;
	m_TurretPlaceLastFire = false;
	m_TurretPlacePos = TurretAimToWorldPos(m_pGameServer, pChr);
	const int TurretItem = GetHolding(ITYPE_TURRET);
	const bool Valid = TurretPlacementValid(m_pGameServer, m_TurretPlacePos, m_ClientID);
	m_pTurretPreview = new CTurretPreview(&GameServer()->m_World, m_TurretPlacePos, m_ClientID, TurretItem, Valid);
	return m_pTurretPreview != nullptr;
}

void CPlayer::UpdateTurretPlaceFromAim()
{
	if(!m_TurretPlacing || !m_pTurretPreview)
		return;
	CCharacter *pChr = GetCharacter();
	if(!pChr)
	{
		CancelTurretPlace();
		return;
	}
	m_TurretPlacePos = TurretAimToWorldPos(m_pGameServer, pChr);
	const bool Valid = TurretPlacementValid(m_pGameServer, m_TurretPlacePos, m_ClientID);
	m_pTurretPreview->SetPreviewPos(m_TurretPlacePos);
	m_pTurretPreview->SetValid(Valid);
}

bool CPlayer::ConfirmTurretPlace()
{
	if(!m_TurretPlacing || !IsTurretPlaceValid())
		return false;
	const vec2 Pos = m_TurretPlacePos;
	CancelTurretPlace();
	return CreateTurret(Pos);
}

bool CPlayer::CreateTurret(vec2 Pos)
{
	CCharacter *pChr = GetCharacter();
	if(!pChr)
		return false;
	const int TurretItem = GetHolding(ITYPE_TURRET);
	if(TurretItem <= 0)
		return false;
	if(length(Pos) < 1.0f)
		Pos = pChr->GetPos();
	if(!TurretPlacementValid(m_pGameServer, Pos, m_ClientID))
		return false;
	CancelTurretPlace();
	SyncDeployedTurretRef();
	DestroyTurret();
	m_pTurret = new CTurret(&GameServer()->m_World, Pos, m_ClientID, TurretItem);
	GameServer()->ClearVotes(GetCID());
	return m_pTurret != nullptr;
}

CTurret *CPlayer::SyncDeployedTurretRef()
{
	if(m_pTurret)
	{
		for(CGameWorld::TypeRange r = GameServer()->m_World.DoTypeRange(CGameWorld::ENTTYPE_TURRET); !r.empty(); r.pop_front())
		{
			if(r.front() == m_pTurret)
				return m_pTurret;
		}
		m_pTurret = nullptr;
	}

	for(CGameWorld::TypeRange r = GameServer()->m_World.DoTypeRange(CGameWorld::ENTTYPE_TURRET); !r.empty(); r.pop_front())
	{
		CTurret *pT = static_cast<CTurret *>(r.front());
		if(pT && pT->GetOwner() == m_ClientID)
		{
			m_pTurret = pT;
			return m_pTurret;
		}
	}
	return nullptr;
}

void CPlayer::DestroyTurret()
{
	CancelTurretPlace();
	TurretAmmo_ClearDebt(this);

	CTurret *apOwned[8];
	int NumOwned = 0;
	for(CGameWorld::TypeRange r = GameServer()->m_World.DoTypeRange(CGameWorld::ENTTYPE_TURRET); !r.empty(); r.pop_front())
	{
		CTurret *pT = static_cast<CTurret *>(r.front());
		if(!pT || pT->GetOwner() != m_ClientID)
			continue;
		if(NumOwned < (int)(sizeof(apOwned) / sizeof(apOwned[0])))
			apOwned[NumOwned++] = pT;
	}

	m_pTurret = nullptr;
	for(int i = 0; i < NumOwned; i++)
		delete apOwned[i];
}

bool CPlayer::RepairDeployedTurret()
{
	if(!SyncDeployedTurretRef() || !m_pTurret->IsBroken())
		return false;

	CCharacter *pChr = GetCharacter();
	if(!pChr || !pChr->IsAlive())
		return false;

	if(distance(pChr->GetPos(), m_pTurret->GetPos()) > 520.0f)
		return false;

	if(!TurretRepair_Consume(m_pGameServer, this, m_pTurret->GetItemDefId()))
		return false;

	m_pTurret->Repair();
	return true;
}

bool CPlayer::RecallTurret()
{
	if(m_TurretPlacing)
		CancelTurretPlace();
	if(!SyncDeployedTurretRef())
		return false;

	CCharacter *pChr = GetCharacter();
	if(!pChr || !pChr->IsAlive())
		return false;

	DestroyTurret();
	return true;
}

void CPlayer::KillCharacter(int Weapon)
{
	DestroyTurret();
	if(m_pCharacter)
	{
		m_pCharacter->Die(m_ClientID, Weapon);
		delete m_pCharacter;
		m_pCharacter = 0;
	}
}

void CPlayer::ForbidRespawn()
{
	m_RespawnDisabled = true;
	m_Spawning = false;
}

void CPlayer::Respawn()
{
	if(m_RespawnDisabled && m_Team != TEAM_SPECTATORS)
	{
		// enable spectate mode for dead players
		m_DeadSpecMode = true;
		m_IsReadyToPlay = true;
		m_SpecMode = SPEC_PLAYER;
		UpdateDeadSpecMode();
		return;
	}

	m_DeadSpecMode = false;

	if(m_Team != TEAM_SPECTATORS)
		m_Spawning = true;
}

bool CPlayer::SetSpectatorID(int SpecMode, int SpectatorID)
{
	if((SpecMode == m_SpecMode && SpecMode != SPEC_PLAYER) ||
		(m_SpecMode == SPEC_PLAYER && SpecMode == SPEC_PLAYER && (SpectatorID == -1 || m_SpectatorID == SpectatorID || m_ClientID == SpectatorID)))
	{
		return false;
	}

	if(m_Team == TEAM_SPECTATORS)
	{
		// check for freeview or if wanted player is playing
		if(SpecMode != SPEC_PLAYER || (SpecMode == SPEC_PLAYER && GameServer()->m_apPlayers[SpectatorID] && GameServer()->m_apPlayers[SpectatorID]->GetTeam() != TEAM_SPECTATORS))
		{
			if(SpecMode == SPEC_FLAGRED || SpecMode == SPEC_FLAGBLUE)
			{
				return false;
			}
			m_SpecMode = SpecMode;
			m_SpectatorID = SpectatorID;
			return true;
		}
	}
	else if(m_DeadSpecMode)
	{
		// check if wanted player can be followed
		if(SpecMode == SPEC_PLAYER && GameServer()->m_apPlayers[SpectatorID] && DeadCanFollow(GameServer()->m_apPlayers[SpectatorID]))
		{
			m_SpecMode = SpecMode;
			m_SpectatorID = SpectatorID;
			return true;
		}
	}

	return false;
}

bool CPlayer::DeadCanFollow(CPlayer *pPlayer) const
{
	// check if wanted player is in the same team and alive
	return (!pPlayer->m_RespawnDisabled || (pPlayer->GetCharacter() && pPlayer->GetCharacter()->IsAlive())) && pPlayer->GetTeam() == m_Team;
}

void CPlayer::UpdateDeadSpecMode()
{
	// check if actual spectator id is valid
	if(m_SpectatorID != -1 && GameServer()->m_apPlayers[m_SpectatorID] && DeadCanFollow(GameServer()->m_apPlayers[m_SpectatorID]))
		return;

	// find player to follow
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(GameServer()->m_apPlayers[i] && DeadCanFollow(GameServer()->m_apPlayers[i]))
		{
			m_SpectatorID = i;
			return;
		}
	}

	// no one available to follow -> turn spectator mode off
	m_DeadSpecMode = false;
}

void CPlayer::SetTeam(int Team, bool DoChatMsg)
{
	KillCharacter();

	m_Team = Team;
	m_LastActionTick = Server()->Tick();
	m_SpecMode = SPEC_FREEVIEW;
	m_SpectatorID = -1;
	m_DeadSpecMode = false;

	// we got to wait 0.5 secs before respawning
	m_RespawnTick = Server()->Tick() + Server()->TickSpeed() / 2;

	if(Team == TEAM_SPECTATORS)
	{
		// update spectator modes
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->m_SpecMode == SPEC_PLAYER && GameServer()->m_apPlayers[i]->m_SpectatorID == m_ClientID)
			{
				if(GameServer()->m_apPlayers[i]->m_DeadSpecMode)
					GameServer()->m_apPlayers[i]->UpdateDeadSpecMode();
				else
				{
					GameServer()->m_apPlayers[i]->m_SpecMode = SPEC_FREEVIEW;
					GameServer()->m_apPlayers[i]->m_SpectatorID = -1;
				}
			}
		}
	}
}

void CPlayer::TryRespawn()
{
	if(m_RespawnDisabled)
		return;

	vec2 SpawnPos;

	if(!GameServer()->m_pController->CanSpawn(m_Team, &SpawnPos))
		return;

	SpawnAt(SpawnPos);
}

void CPlayer::SpawnAt(vec2 Pos)
{
	if(m_pCharacter)
		KillCharacter(WEAPON_GAME);

	m_Spawning = false;
	m_pCharacter = new(m_ClientID) CCharacter(&GameServer()->m_World);
	m_pCharacter->Spawn(this, Pos);
	GameServer()->m_World.CreatePlayerSpawn(Pos);
	if(IsDummy() && GameServer()->Core())
		GameServer()->Core()->OnCharacterSpawn(this);
}

int CPlayer::GetBaseMaxHealth() const
{
	const SProfessionDef *pDef = CProfessionData::GetDef(m_Profession);
	if(!pDef) return 10;
	return pDef->m_BaseHP + (int)(pDef->m_HPPerLevel * m_MMOLevel);
}

float CPlayer::GetBaseAttack() const
{
	const SProfessionDef *pDef = CProfessionData::GetDef(m_Profession);
	if(!pDef) return 1.0f;
	return 1.0f + pDef->m_AttackPerLevel * m_MMOLevel;
}

float CPlayer::GetBaseDefense() const
{
	const SProfessionDef *pDef = CProfessionData::GetDef(m_Profession);
	if(!pDef) return 0.0f;
	return pDef->m_DefensePerLevel * m_MMOLevel;
}

// TRPG六维 → 有效战斗属性
int CPlayer::GetEffectiveMeleeAttack() const
{
	return GetStat(AttributeIdentifier::STR) * 2;
}

int CPlayer::GetEffectiveRangedAttack() const
{
	return GetStat(AttributeIdentifier::DEX) * 2;
}

int CPlayer::GetEffectiveDefense() const
{
	return GetStat(AttributeIdentifier::CON) * 2;
}

int CPlayer::GetMMOItemEnchant(int ItemID) const
{
	if(ItemID <= 0)
		return 0;
	for(size_t i = 0; i < m_MMOInventory.size(); i++)
	{
		if(m_MMOInventory[i].GetID() == ItemID)
			return m_MMOInventory[i].GetEnchant();
	}
	return 0;
}

int CPlayer::GetMMOEquippedAttributeSum(AttributeIdentifier ID) const
{
	int Sum = 0;
	int aSeen[512] = {0};
	auto AddItem = [&](int ItemID)
	{
		if(ItemID <= 0 || ItemID >= (int)(sizeof(aSeen) / sizeof(aSeen[0])) || aSeen[ItemID])
			return;
		aSeen[ItemID] = 1;
		const CMMOItemDescription *pDef = CMMOItemDescription::Get(ItemID);
		if(!pDef)
			return;
		Sum += pDef->GetAttributeValue(ID, GetMMOItemEnchant(ItemID));
	};

	for(const auto &Slot : m_EquippedSlots.getSlots())
		AddItem(Slot.second);
	for(int i = 0; i < MMO_WEAPON_LOADOUT_SIZE; i++)
	{
		AddItem(m_aMeleeLoadout[i]);
		AddItem(m_aRangedLoadout[i]);
	}
	return Sum;
}

int CPlayer::GetMMOMaxAmmo() const
{
	return maximum(1, 10 + GetMMOEquippedAttributeSum(AttributeIdentifier::Ammo));
}

int CPlayer::GetMMOAmmoRegenPercent() const
{
	const int Sum = GetMMOEquippedAttributeSum(AttributeIdentifier::AmmoRegen);
	const int Percent = Sum > 0 ? Sum : 100;
	return clamp(Percent, 50, 600);
}

bool CPlayer::UsesMMOFiniteAmmo() const
{
	if(IsDummy())
		return false;
	for(int i = 0; i < MMO_WEAPON_LOADOUT_SIZE; i++)
	{
		if(m_aRangedLoadout[i] > 0)
			return true;
	}
	return false;
}

int CPlayer::GetMaxMana() const
{
	return 50 + GetStat(AttributeIdentifier::INT) * 5 + m_MMOLevel * 3;
}

void CPlayer::MarkCombat()
{
	if(!Server())
		return;
	m_LastCombatTick = Server()->Tick();
}

bool CPlayer::TryConsumeSkillProficiency()
{
	if(!Server())
		return false;

	const int Now = Server()->Tick();
	const int Window = maximum(1, Server()->TickSpeed() * SKILL_PROFICIENCY_WINDOW_SEC);
	if(m_SkillProfWindowStart <= 0 || Now - m_SkillProfWindowStart >= Window)
	{
		m_SkillProfWindowStart = Now;
		m_SkillProfWindowCount = 0;
	}

	if(m_SkillProfWindowCount >= SKILL_PROFICIENCY_MAX_PER_WINDOW)
		return false;

	m_SkillProfWindowCount++;
	return true;
}

bool CPlayer::ShouldDeferSkillProgressSave(bool Force) const
{
	if(Force || !Server())
		return false;
	const int Debounce = maximum(1, Server()->TickSpeed() * SKILL_SAVE_DEBOUNCE_SEC);
	return m_SkillProgressSaveTick > 0 && Server()->Tick() - m_SkillProgressSaveTick < Debounce;
}

void CPlayer::MarkSkillProgressSaved()
{
	if(Server())
		m_SkillProgressSaveTick = Server()->Tick();
}

void CPlayer::AddMMOExperience(int Amount)
{
	if(Amount <= 0) return;
	SetStat(AttributeIdentifier::Experience, GetStat(AttributeIdentifier::Experience) + Amount);
	m_MMODirty = true;

	CGameContext *pGS = GameServer();
	while(GetStat(AttributeIdentifier::Experience) >= ExpForLevel(GetStat(AttributeIdentifier::Level)))
	{
		SetStat(AttributeIdentifier::Experience, GetStat(AttributeIdentifier::Experience) - ExpForLevel(GetStat(AttributeIdentifier::Level)));
		SetStat(AttributeIdentifier::Level, GetStat(AttributeIdentifier::Level) + 1);
		SetStat(AttributeIdentifier::SkillPoints, GetStat(AttributeIdentifier::SkillPoints) + 3);
		ApplyMMOSkillBonuses();

		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "🎉 升级了！现在等级 %d！（+3 技能点）", GetStat(AttributeIdentifier::Level));
		if(pGS)
			pGS->SendChat(m_ClientID, CHAT_ALL, -1, aBuf);
	}
}

void CPlayer::InitWeaponLoadouts()
{
	for(int i = 0; i < MMO_WEAPON_LOADOUT_SIZE; i++)
	{
		m_aMeleeLoadout[i] = -1;
		m_aRangedLoadout[i] = -1;
		m_aWeaponBar[i] = -1;
	}
}

int CPlayer::RangedLoadoutIndexForItemType(ItemType Type)
{
	switch(Type)
	{
	case ItemType::EquipGun: return 0;
	case ItemType::EquipShotgun: return 1;
	case ItemType::EquipGrenade: return 2;
	case ItemType::EquipLaser: return 3;
	default: return -1;
	}
}

bool CPlayer::IsMMOWeaponItemType(ItemType Type)
{
	return MMOItemTypeToWeapon(Type) >= 0;
}

bool CPlayer::IsMMOWeaponEquipped(int ItemID) const
{
	return FindMeleeLoadoutIndex(ItemID) >= 0 || FindRangedLoadoutIndex(ItemID) >= 0;
}

int CPlayer::FindMeleeLoadoutIndex(int ItemID) const
{
	if(ItemID <= 0)
		return -1;
	for(int i = 0; i < MMO_WEAPON_LOADOUT_SIZE; i++)
	{
		if(m_aMeleeLoadout[i] == ItemID)
			return i;
	}
	return -1;
}

int CPlayer::FindRangedLoadoutIndex(int ItemID) const
{
	if(ItemID <= 0)
		return -1;
	for(int i = 0; i < MMO_WEAPON_LOADOUT_SIZE; i++)
	{
		if(m_aRangedLoadout[i] == ItemID)
			return i;
	}
	return -1;
}

int CPlayer::FindWeaponBarIndex(int ItemID) const
{
	if(ItemID <= 0)
		return -1;
	for(int i = 0; i < MMO_WEAPON_LOADOUT_SIZE; i++)
	{
		if(m_aWeaponBar[i] == ItemID)
			return i;
	}
	return -1;
}

int CPlayer::FirstEmptyMeleeLoadout() const
{
	for(int i = 0; i < MMO_WEAPON_LOADOUT_SIZE; i++)
	{
		if(m_aMeleeLoadout[i] < 0)
			return i;
	}
	return -1;
}

int CPlayer::FirstEmptyWeaponBarSlot() const
{
	for(int i = 0; i < MMO_WEAPON_LOADOUT_SIZE; i++)
	{
		if(m_aWeaponBar[i] < 0)
			return i;
	}
	return -1;
}

int CPlayer::FirstNonEmptyMeleeLoadout() const
{
	for(int i = 0; i < MMO_WEAPON_LOADOUT_SIZE; i++)
	{
		if(m_aMeleeLoadout[i] > 0)
			return i;
	}
	return -1;
}

int CPlayer::FirstNonEmptyRangedLoadout() const
{
	for(int i = 0; i < MMO_WEAPON_LOADOUT_SIZE; i++)
	{
		if(m_aRangedLoadout[i] > 0)
			return i;
	}
	return -1;
}

void CPlayer::RemoveWeaponFromLoadouts(int ItemID)
{
	for(int i = 0; i < MMO_WEAPON_LOADOUT_SIZE; i++)
	{
		if(m_aMeleeLoadout[i] == ItemID)
			m_aMeleeLoadout[i] = -1;
		if(m_aRangedLoadout[i] == ItemID)
			m_aRangedLoadout[i] = -1;
		if(m_aWeaponBar[i] == ItemID)
			m_aWeaponBar[i] = -1;
	}

	const CMMOItemDescription *pDef = CMMOItemDescription::Get(ItemID);
	if(pDef && m_EquippedSlots.isEquippedItem(ItemID))
		m_EquippedSlots.unequipSlot(pDef->GetType());
}

void CPlayer::AutoFillWeaponBar(int ItemID)
{
	if(ItemID <= 0 || FindWeaponBarIndex(ItemID) >= 0)
		return;
	const int Slot = FirstEmptyWeaponBarSlot();
	if(Slot >= 0)
		m_aWeaponBar[Slot] = ItemID;
}

void CPlayer::MigrateWeaponLoadoutFromEquippedSlots()
{
	for(int i = 0; i < MMO_WEAPON_LOADOUT_SIZE; i++)
	{
		if(m_aMeleeLoadout[i] > 0 || m_aRangedLoadout[i] > 0)
			return;
	}

	static const ItemType s_aWpnTypes[] = {
		ItemType::EquipHammer, ItemType::EquipGun, ItemType::EquipShotgun,
		ItemType::EquipGrenade, ItemType::EquipLaser,
	};

	for(ItemType Type : s_aWpnTypes)
	{
		if(!m_EquippedSlots.isEquipped(Type))
			continue;
		const int ItemID = m_EquippedSlots.getSlot(Type);
		if(ItemID <= 0)
			continue;

		if(Type == ItemType::EquipHammer)
		{
			const int Slot = FirstEmptyMeleeLoadout();
			if(Slot >= 0)
				m_aMeleeLoadout[Slot] = ItemID;
		}
		else
		{
			const int RIdx = RangedLoadoutIndexForItemType(Type);
			if(RIdx >= 0)
				m_aRangedLoadout[RIdx] = ItemID;
		}
		AutoFillWeaponBar(ItemID);
	}
	EnsureWeaponBarFromLoadouts();
}

void CPlayer::EnsureWeaponBarFromLoadouts()
{
	for(int i = 0; i < MMO_WEAPON_LOADOUT_SIZE; i++)
	{
		if(m_aMeleeLoadout[i] > 0)
			AutoFillWeaponBar(m_aMeleeLoadout[i]);
		if(m_aRangedLoadout[i] > 0)
			AutoFillWeaponBar(m_aRangedLoadout[i]);
	}
}

void CPlayer::RecalcMMOStats()
{
	m_MMOAttack = m_MMOLevel * 2 + 5;  // base: 7 at level 1
	m_MMODefense = m_MMOLevel + 3;     // base: 4 at level 1
	if(m_pCharacter && UsesMMOFiniteAmmo())
		m_pCharacter->SyncMMOWeaponAmmo(GetMMOMaxAmmo());
}

void CPlayer::FormatBroadcastBasicStats(char *pBuffer, int Size, const char *pAppendStr)
{
	const char *pAppend = pAppendStr ? pAppendStr : "";

	// Only show full panel for logged-in players with a character
	if(!m_pCharacter || IsDummy())
	{
		str_format(pBuffer, Size, "%-200s", pAppend);
		return;
	}

	// HP bar
	const int HP = m_pCharacter->GetHealth();
	const int MaxHP = m_pCharacter->GetMaxHealth();
	const int HPBarLen = 10;
	const int HPFilled = (MaxHP > 0) ? (HP * HPBarLen / MaxHP) : 0;
	char aHPBar[32];
	{
		int pos = 0;
		aHPBar[pos++] = '[';
		for(int i = 0; i < HPBarLen; i++)
			aHPBar[pos++] = (i < HPFilled) ? '|' : '-';
		aHPBar[pos++] = ']';
		aHPBar[pos++] = '\0';
	}

	// MRPG HUD anchor: leading blank lines push overlay below vanilla UI
	char aPanel[1024];
	str_copy(aPanel, "\n\n\n\n\n", sizeof(aPanel));

	// Weapon HUD: current + numbered loadout slots per category
	{
		const bool MagicMode = m_pCharacter->GetActiveCategory() == CCharacter::WEAPONCAT_MAGIC;
		const int ActiveCat = m_pCharacter->GetActiveCategory();
		const int ActiveMelee = m_pCharacter->GetActiveMeleeLoadoutIdx();
		const int ActiveRanged = m_pCharacter->GetActiveRangedLoadoutIdx();
		const int ActiveItemID = m_pCharacter->GetActiveWeaponItemID();

		auto ShortItemName = [&](int ItemID, char *pBuf, int BufSize) -> const char *
		{
			if(ItemID <= 0)
			{
				str_copy(pBuf, "空", BufSize);
				return pBuf;
			}
			const CMMOItemDescription *pDef = CMMOItemDescription::Get(ItemID);
			if(!pDef)
			{
				str_copy(pBuf, "?", BufSize);
				return pBuf;
			}
			str_utf8_copy_num(pBuf, GameServer()->Loc(m_ClientID, pDef->GetNameKey(), pDef->GetName()), BufSize, 6);
			return pBuf;
		};

		char aTmp[512];
		char aNameBuf[32];
		if(MagicMode)
		{
			const int SID = m_aSkillSlots[m_pCharacter->GetActiveSkillSlot()];
			const char *pMagic = "-";
			if(SID >= 0)
			{
				CSkillManager *pSM = GameServer()->TW() ? GameServer()->TW()->SkillManager() : nullptr;
				if(pSM)
				{
					const SSkillDescription *pDesc = pSM->FindDescription(SID);
					if(pDesc)
						pMagic = pDesc->m_aName;
				}
			}
			str_format(aTmp, sizeof(aTmp), ">> 魔法 %s", pMagic);
		}
		else if(ActiveItemID > 0)
		{
			str_format(aTmp, sizeof(aTmp), ">> %s", ShortItemName(ActiveItemID, aNameBuf, sizeof(aNameBuf)));
			if(UsesMMOFiniteAmmo() && ActiveCat == CCharacter::WEAPONCAT_RANGED)
			{
				const int W = m_pCharacter->GetActiveWeapon();
				if(W >= WEAPON_GUN && W <= WEAPON_LASER)
				{
					const int CurAmmo = m_pCharacter->WeaponAmmo(W);
					if(CurAmmo >= 0)
					{
						char aAmmo[32];
						str_format(aAmmo, sizeof(aAmmo), " 弹药 %d/%d", CurAmmo, GetMMOMaxAmmo());
						str_append(aTmp, aAmmo, sizeof(aTmp));
					}
				}
			}
		}
		else
			str_copy(aTmp, ">> 空手", sizeof(aTmp));
		str_append(aPanel, aTmp, sizeof(aPanel));

		auto AppendLoadoutRow = [&](const char *pKeyLabel, const int *pLoadout, int ActiveIdx, bool Highlight)
		{
			char aN0[32], aN1[32], aN2[32], aN3[32];
			char aRow[256];
			str_format(aRow, sizeof(aRow), "\n%s%s [1]%s%s [2]%s%s [3]%s%s [4]%s%s",
				pKeyLabel, Highlight && !MagicMode ? "*" : "",
				ShortItemName(pLoadout[0], aN0, sizeof(aN0)), (!MagicMode && Highlight && ActiveIdx == 0) ? "*" : "",
				ShortItemName(pLoadout[1], aN1, sizeof(aN1)), (!MagicMode && Highlight && ActiveIdx == 1) ? "*" : "",
				ShortItemName(pLoadout[2], aN2, sizeof(aN2)), (!MagicMode && Highlight && ActiveIdx == 2) ? "*" : "",
				ShortItemName(pLoadout[3], aN3, sizeof(aN3)), (!MagicMode && Highlight && ActiveIdx == 3) ? "*" : "");
			str_append(aPanel, aRow, sizeof(aPanel));
		};

		AppendLoadoutRow("键1·近战", m_aMeleeLoadout, ActiveMelee, ActiveCat == CCharacter::WEAPONCAT_MELEE);
		AppendLoadoutRow("键2·远程", m_aRangedLoadout, ActiveRanged, ActiveCat == CCharacter::WEAPONCAT_RANGED);
	}

	// Skill slots 3/4/5 (magic bar); * marks selected slot when in magic mode
	{
		CSkillManager *pSM = GameServer()->TW() ? GameServer()->TW()->SkillManager() : nullptr;
		static const char *s_aSlotLabels[] = { "3", "4", "5" };
		const bool MagicMode = m_pCharacter->GetActiveCategory() == CCharacter::WEAPONCAT_MAGIC;
		const int ActiveSlot = m_pCharacter->GetActiveSkillSlot();
		for(int si = 0; si < 3; si++)
		{
			const int SID = m_aSkillSlots[si];
			char aTmp[64];
			const char *pMark = (MagicMode && si == ActiveSlot) ? "*" : "";
			if(SID >= 0 && pSM)
			{
				const SSkillDescription *pDesc = pSM->FindDescription(SID);
				const char *pName = pDesc ? pDesc->m_aName : "?";
				str_format(aTmp, sizeof(aTmp), "\n[%s:%s%s]", s_aSlotLabels[si], pName, pMark);
			}
			else
				str_format(aTmp, sizeof(aTmp), "\n[%s:-%s]", s_aSlotLabels[si], pMark);
			str_append(aPanel, aTmp, sizeof(aPanel));
		}
	}

	// HP line
	{
		char aTmp[64];
		str_format(aTmp, sizeof(aTmp), "\nHP %s %d/%d", aHPBar, HP, MaxHP);
		str_append(aPanel, aTmp, sizeof(aPanel));
	}

	// MMO stats
	if(m_MMOLevel > 0)
	{
		char aTmp[64];
		str_format(aTmp, sizeof(aTmp), "\nLv.%d | %d Gold", m_MMOLevel, m_MMOGold);
		str_append(aPanel, aTmp, sizeof(aPanel));
	}

	// MRPG: pad append column so status + message render together
	str_format(pBuffer, Size, "%s%-200s", aPanel, pAppend);
}

void CPlayer::ApplyMMOSkillBonuses()
{
	RecalcMMOStats();
	for(int i = 0; i < m_NumMMOSkills; i++)
	{
		const SMMOSkillState &St = m_aMMOSkills[i];
		if(St.m_SkillID < 0 || St.m_SkillID >= NUM_MMO_SKILLS) continue;
		const SMMOSkillDef &Def = g_aMMOSkillDefs[St.m_SkillID];
		int Bonus = (int)(Def.m_EffectPerLevel * St.m_Level);
		switch(Def.m_Effect)
		{
		case SKILL_EFFECT_DAMAGE_BOOST:
			m_MMOAttack += Bonus;
			break;
		case SKILL_EFFECT_DEFENSE_BOOST:
			m_MMODefense += Bonus;
			break;
		case SKILL_EFFECT_HEALTH_BOOST:
			// Health bonus is applied in GetMaxHealth()
			break;
		case SKILL_EFFECT_CRIT_CHANCE:
			// Crit bonus tracked separately if needed
			break;
		default:
			break;
		}
	}
}

int CPlayer::GetMMOSkillLevel(int SkillID) const
{
	for(int i = 0; i < m_NumMMOSkills; i++)
		if(m_aMMOSkills[i].m_SkillID == SkillID)
			return m_aMMOSkills[i].m_Level;
	return 0;
}
