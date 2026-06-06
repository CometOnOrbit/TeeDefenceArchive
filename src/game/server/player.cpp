/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "account.h"
#include "core/tworld_controller.h"
#include "entities/character.h"
#include <game/collision.h>

#include "entities/turret.h"
#include "gamecontext.h"
#include "gamecontroller.h"
#include "player.h"

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
	m_IsReadyToPlay = true;
	m_AccountId = -1;
	m_Zomb = ZOMB_NONE;
	mem_zero(m_aZombSub, sizeof(m_aZombSub));
	m_ZombVisible = true;
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
}

CPlayer::~CPlayer()
{
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

void CPlayer::InitZombie(int Zomb)
{
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

void CPlayer::Tick()
{
	if(!IsDummy() && !Server()->ClientIngame(m_ClientID))
		return;

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
	if(!IsDummy() && !Server()->ClientIngame(m_ClientID))
		return;

	const int SnapID = m_pGameServer->ClientSnapID(SnappingClient, m_ClientID);
	if(SnapID < 0)
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
	const bool MappedSlot = SnapID != m_ClientID;
	if(MappedSlot || ZombieBot)
	{
		CNetObj_PlayerInfoExtra *pPlayerInfoExtra = static_cast<CNetObj_PlayerInfoExtra *>(Server()->SnapNewItem(NETOBJTYPE_PLAYERINFOEXTRA, SnapID, sizeof(CNetObj_PlayerInfoExtra)));
		if(pPlayerInfoExtra)
		{
			pPlayerInfoExtra->m_RealClientID = m_ClientID;
			pPlayerInfoExtra->m_PlayerFlagsExtra = ZombieBot ? PLAYERFLAGEXTRA_HIDDEN_IN_BOARD : 0;
		}
	}

	if(m_ClientID == SnappingClient && (m_Team == TEAM_SPECTATORS || m_DeadSpecMode))
	{
		CNetObj_SpectatorInfo *pSpectatorInfo = static_cast<CNetObj_SpectatorInfo *>(Server()->SnapNewItem(NETOBJTYPE_SPECTATORINFO, SnapID, sizeof(CNetObj_SpectatorInfo)));
		if(!pSpectatorInfo)
			return;

		pSpectatorInfo->m_SpecMode = m_SpecMode;
		int SpecID = m_SpectatorID;
		if(SpecID >= 0 && SnappingClient >= 0 && !m_pGameServer->ClientUsesExtendedSlots(SnappingClient))
			SpecID = m_pGameServer->ClientDisplaySlot(SnappingClient, SpecID);
		pSpectatorInfo->m_SpectatorID = SpecID;
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
	if(NewInput->m_PlayerFlags & PLAYERFLAG_CHATTING)
	{
		// skip the input if chat is active
		if(m_PlayerFlags & PLAYERFLAG_CHATTING)
			return;

		// reset input
		if(m_pCharacter)
			m_pCharacter->ResetInput();

		m_PlayerFlags = NewInput->m_PlayerFlags;
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
	DestroyTurret();
	m_pTurret = new CTurret(&GameServer()->m_World, Pos, m_ClientID, TurretItem);
	GameServer()->ClearVotes(GetCID());
	return m_pTurret != nullptr;
}

void CPlayer::DestroyTurret()
{
	CancelTurretPlace();
	delete m_pTurret;
	m_pTurret = nullptr;
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

	m_Spawning = false;
	m_pCharacter = new(m_ClientID) CCharacter(&GameServer()->m_World);
	m_pCharacter->Spawn(this, SpawnPos);
	GameServer()->m_World.CreatePlayerSpawn(SpawnPos);
	if(IsDummy() && GameServer()->Core())
		GameServer()->Core()->OnCharacterSpawn(this);
}
