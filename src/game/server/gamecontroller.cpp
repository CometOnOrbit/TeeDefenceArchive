/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <game/mapitems.h>
#include <game/version.h>
#include <generated/server_data.h>

#include <base/math.h>

#include "entities/character.h"
#include "entities/electro.h"
#include "entities/laser.h"
#include "entities/lightning.h"
#include "entities/mmo/hammer_lamp_bolt.h"
#include "entities/mmo/homing_grenade.h"
#include "entities/mmo/magnetic_pulse.h"
#include "entities/mmo/mmo_weapon_common.h"
#include "entities/mmo/tesla_chain.h"
#include "entities/mmo/tracked_plasma.h"
#include "entities/mmo/wall_pusher.h"
#include "entities/pickup.h"
#include "entities/projectile.h"
#include "entities/turret.h"
#include "account.h"
#include "core/components/accounts/account_manager.h"
#include "core/components/bots/defence_bot_manager.h"
#include "core/components/craft/craft_manager.h"
#include "core/components/npcs/npc_manager.h"
#include "core/components/dialogs/dialog_manager.h"
#include "core/components/vote/vote_menu_manager.h"
#include "core/components/profession/profession_manager.h"
#include "core/tworld_controller.h"
#include "entities/CKs.h"
#include "item_system.h"
#include "gamecontext.h"
#include "botengine.h"
#include <game/server/core/components/content/trait_manager.h>
#include <game/server/core/components/quests/quest_manager.h>
#include <game/server/core/components/guilds/guild_manager.h>
#include <game/server/core/components/profession/profession_manager.h>
#include <game/server/core/components/meta/achievement_manager.h>
#include <game/server/core/components/mmo/mmo_manager.h>
#include <game/server/core/components/mmo/mmo_item.h>
#include <game/server/global_state.h>

#include <game/server/core/components/meta/duties_manager.h>
#include <game/server/core/components/meta/durability_manager.h>
#include <game/server/core/components/skills/skill_manager.h>
#include <game/server/core/components/content/content_types.h>
#include <game/server/core/components/content/effect_registry.h>
#include <game/server/core/components/content/enemy_registry.h>
#include <game/server/core/tworld_controller.h>

#include "gamecontroller.h"
#include "player.h"
#include "core/components/dungeon/dungeon_manager.h"

static bool IsZombiePlayer(const CPlayer *pPlayer)
{
	return pPlayer && pPlayer->IsDummy() && pPlayer->GetZomb() != ZOMB_NONE;
}

static int GetMMOWeaponEnchant(CPlayer *pPl, int ItemID)
{
	if(!pPl || ItemID <= 0)
		return 0;
	for(size_t i = 0; i < pPl->m_MMOInventory.size(); i++)
	{
		const CMMOItem &It = pPl->m_MMOInventory[i];
		if(It.GetID() == ItemID)
			return It.GetEnchant();
	}
	return 0;
}

static const CMMOItemDescription *GetActiveMMOWeaponDef(CCharacter *pChr, CPlayer *pPl, int *pEnchant)
{
	if(!pChr || !pPl || pPl->IsDummy())
		return nullptr;
	const int ItemID = pChr->GetActiveWeaponItemID();
	if(ItemID <= 0)
		return nullptr;
	const CMMOItemDescription *pDef = CMMOItemDescription::Get(ItemID);
	if(!pDef)
		return nullptr;
	if(pEnchant)
		*pEnchant = GetMMOWeaponEnchant(pPl, ItemID);
	return pDef;
}

static vec2 ApplyWeaponSpread(vec2 Dir, int SpreadDeg)
{
	if(SpreadDeg <= 0 || length(Dir) < 0.001f)
		return Dir;
	const float SpreadRad = SpreadDeg * pi / 180.f;
	const float Jitter = (random_int() % 2001 - 1000) / 1000.f * SpreadRad;
	const float a = angle(Dir) + Jitter;
	return vec2(cosf(a), sinf(a));
}

static float WeaponProjSpeedMul(const SMMOWeaponProfile *pProf)
{
	return pProf ? (float)pProf->m_ProjSpeedPercent / 100.f : 1.f;
}

static int WeaponExtraSpread(const SMMOWeaponProfile *pProf)
{
	return pProf ? pProf->m_SpreadDegrees : 0;
}

static float WeaponProjLifeMul(const SMMOWeaponProfile *pProf)
{
	return pProf ? (float)pProf->m_ProjRangePercent / 100.f : 1.f;
}

static float WeaponFanSpreadRadians(const SMMOWeaponProfile *pProf)
{
	if(!pProf || pProf->m_FanSpreadRad <= 0)
		return 0.1f;
	return (float)pProf->m_FanSpreadRad / 100.f;
}

static float WeaponLaserReach(const CTuningParams *pTune, const SMMOWeaponProfile *pProf)
{
	float Reach = pTune->m_LaserReach;
	if(pProf && pProf->m_LaserReachPercent != 100)
		Reach *= (float)pProf->m_LaserReachPercent / 100.f;
	return Reach;
}

static vec2 WeaponFanDirection(vec2 BaseDir, int Index, int Count, float SpreadRad)
{
	const float Center = (Count - 1) * 0.5f;
	const float a = angle(BaseDir) + (Index - Center) * SpreadRad;
	return vec2(cosf(a), sinf(a));
}

static int RollWeaponDamage(int BaseDmg, const SMMOWeaponProfile *pProf)
{
	int Dmg = BaseDmg;
	if(pProf && pProf->m_DamageMulPercent != 100)
		Dmg = Dmg * pProf->m_DamageMulPercent / 100;
	if(pProf && pProf->m_CritChance > 0 && (random_int() % 100) < pProf->m_CritChance)
		Dmg *= 2;
	return maximum(1, Dmg);
}

static void ApplyWeaponRecoil(CCharacter *pChr, vec2 Dir, const SMMOWeaponProfile *pProf)
{
	if(!pChr || !pProf || pProf->m_RecoilPercent <= 0 || length(Dir) < 0.001f)
		return;
	pChr->GetCore()->m_Vel -= normalize(Dir) * (2.5f * (float)pProf->m_RecoilPercent / 100.f);
}

static void ApplyWeaponLifesteal(CCharacter *pChr, int Damage, const SMMOWeaponProfile *pProf)
{
	if(!pChr || !pProf || pProf->m_LifestealPercent <= 0 || Damage <= 0)
		return;
	pChr->IncreaseHealth(maximum(1, Damage * pProf->m_LifestealPercent / 100));
}

static int EffectiveAttackSpeedPercent(CPlayer *pPl, const CMMOItemDescription *pWeapon, int Enchant)
{
	int Spd = 100;
	if(pPl)
	{
		const int FromPlayer = pPl->GetStat(AttributeIdentifier::AttackSPD);
		if(FromPlayer > 0)
			Spd = FromPlayer;
	}
	if(pWeapon)
		Spd = Spd * pWeapon->GetAttackSpeedPercent(Enchant) / 100;
	return clamp(Spd, 50, 600);
}

static bool WeaponProfileExplosive(const SMMOWeaponProfile *pProf, bool DefaultExplosive)
{
	if(pProf && pProf->m_Explosive)
		return true;
	return DefaultExplosive;
}

static void SpawnDirectionalProjectile(CGameWorld *pWorld, int WeaponType, int ClientID, vec2 Pos, vec2 Dir,
	int LifeTicks, int Damage, bool Explosive, float Force, int SoundImpact, const SMMOWeaponProfile *pProf)
{
	new CProjectile(pWorld, WeaponType, ClientID, Pos, Dir, LifeTicks,
		Damage, Explosive, Force, SoundImpact, WeaponType,
		WeaponProjSpeedMul(pProf), WeaponProjLifeMul(pProf),
		pProf ? pProf->m_Pierce : 0, pProf ? pProf->m_LifestealPercent : 0);
}

CGameController::CGameController(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
	m_pConfig = m_pGameServer->Config();
	m_pServer = m_pGameServer->Server();

	m_GameStartTick = Server()->Tick();
	m_RealPlayerNum = 0;
}



CGameController::~CGameController()
{
}

void CGameController::PreTick()
{
}

int CGameController::GetDummyTeam() const
{
	return TEAM_BLUE;
}

void CGameController::OnBotPlayerCreated(CPlayer *pPlayer)
{
	if(GameServer()->Core() && GameServer()->Core()->NpcManager() &&
		GameServer()->Core()->NpcManager()->OnBotPlayerCreated(pPlayer))
		return;
}

// activity
void CGameController::DoActivityCheck()
{
	return; // nope.

	/*
	if(Config()->m_SvInactiveKickTime == 0)
		return;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(GameServer()->m_apPlayers[i] && !GameServer()->m_apPlayers[i]->IsDummy() && (GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS || Config()->m_SvInactiveKick > 0) &&
			!Server()->IsAuthed(i) && (GameServer()->m_apPlayers[i]->m_InactivityTickCounter > Config()->m_SvInactiveKickTime * Server()->TickSpeed() * 60))
		{
			if(GameServer()->m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS)
			{
				if(Config()->m_SvInactiveKickSpec)
				{
					char aReason[128];
					str_copy(aReason, GameServer()->Loc(i, "kick.inactivity", "Kicked for inactivity"), sizeof(aReason));
					Server()->Kick(i, aReason);
				}
			}
			else
			{
				switch(Config()->m_SvInactiveKick)
				{
					case 1:
					{
						// move player to spectator
						DoTeamChange(GameServer()->m_apPlayers[i], TEAM_SPECTATORS);
					}
					break;
					case 2:
					{
						// move player to spectator if the reserved slots aren't filled yet, kick him otherwise
						int Spectators = 0;
						for(int j = 0; j < MAX_CLIENTS; ++j)
							if(GameServer()->m_apPlayers[j] && GameServer()->m_apPlayers[j]->GetTeam() == TEAM_SPECTATORS)
								++Spectators;
						if(Spectators >= Config()->m_SvMaxClients - GameServer()->GetMaxPlayerSlots())
						{
							char aReason[128];
							str_copy(aReason, GameServer()->Loc(i, "kick.inactivity", "Kicked for inactivity"), sizeof(aReason));
							Server()->Kick(i, aReason);
						}
						else
							DoTeamChange(GameServer()->m_apPlayers[i], TEAM_SPECTATORS);
					}
					break;
					case 3:
					{
						// kick the player
						{
							char aReason[128];
							str_copy(aReason, GameServer()->Loc(i, "kick.inactivity", "Kicked for inactivity"), sizeof(aReason));
							Server()->Kick(i, aReason);
						}
					}
				}
			}
		}
	}
		*/
}

bool CGameController::GetPlayersReadyState(int WithoutID)
{
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(i == WithoutID)
			continue; // skip
		if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS && !GameServer()->m_apPlayers[i]->m_IsReadyToPlay)
			return false;
	}

	return true;
}

void CGameController::SetPlayersReadyState(bool ReadyState)
{
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS && (ReadyState || !GameServer()->m_apPlayers[i]->m_DeadSpecMode))
			GameServer()->m_apPlayers[i]->m_IsReadyToPlay = ReadyState;
	}
}

bool CGameController::CanCharacterPickup(CCharacter *pChr) const
{
	if(!pChr || !pChr->GetPlayer())
		return false;
	if(IsZombiePlayer(pChr->GetPlayer()))
		return false;
	return true;
}

// event
int CGameController::OnCharacterDeath(CCharacter *pVictim, CPlayer *pKiller, int Weapon)
{
	// update spectator modes for dead players in survival
	for(int i = 0; i < MAX_CLIENTS; ++i)
		if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->m_DeadSpecMode)
			GameServer()->m_apPlayers[i]->UpdateDeadSpecMode();
	// do scoreing
	if(!pKiller || Weapon == WEAPON_GAME)
		return 0;
	if(pKiller == pVictim->GetPlayer())
		pVictim->GetPlayer()->m_Score--; // suicide or world
	else
		pKiller->m_Score++; // normal kill
	if(Weapon == WEAPON_SELF)
		pVictim->GetPlayer()->m_RespawnTick = Server()->Tick() + Server()->TickSpeed() * 3.0f;

	if(TWorldController *pCore = GameServer()->Core())
		pCore->Events().EmitCharacterDeath(pVictim->GetPlayer(), pKiller, Weapon);

	return 0;
}

void CGameController::OnCharacterSpawn(CCharacter *pChr)
{
	// vanilla baseline
	pChr->IncreaseHealth(10);
	pChr->GiveWeapon(WEAPON_HAMMER, -1);
	pChr->GiveWeapon(WEAPON_GUN, 10);

	CPlayer *pPlayer = pChr->GetPlayer();
	pChr->SetHealthDirect(Config()->m_SvPlayerMaxHealth);

	// Apply max health bonus from cards equipped on armor (helm/chest/legs)
	if(!pPlayer->IsDummy())
	{
		CItemHelper *pH = GameServer()->ItemHelper();
		if(pH)
		{
			const int ArmorTypes[] = {ITYPE_HELMET, ITYPE_CHEST, ITYPE_LEGS};
			int TotalStacks = 0;
			for(int a = 0; a < 3; a++)
			{
				const int ItemId = pPlayer->GetHolding(ArmorTypes[a]);
				if(ItemId <= 0)
					continue;
				const char *pExtra = pPlayer->GetExtraForItem(ItemId);
				TotalStacks += pH->GetEffectStacksFromExtra(pExtra, ITEM_CARD_MAX_HEALTH, "max_health_bonus");
			}
			if(TotalStacks > 0)
			{
				const int Bonus = TotalStacks * 2; // health_per_stack
				pChr->AddMaxHealth(Bonus);
			}
		}
	}
	pChr->SetMaxHealth(pChr->GetHealth());

	// Magazine parts on pickaxe/axe/sword increase spawn ammo
	if(!pPlayer->IsDummy() && Config()->m_SvContentFramework && GameServer()->Core() && GameServer()->Core()->EffectRegistry())
	{
		CPlayer *pPl = pPlayer;
		int AmmoBonus = 0;
		const int ToolTypes[] = {ITYPE_PICKAXE, ITYPE_AXE, ITYPE_SWORD};
		for(int t = 0; t < 3; t++)
		{
			const int HoldId = pPl->GetHolding(ToolTypes[t]);
			if(HoldId <= 0)
				continue;
			const char *pExtra = pPl->GetExtraForItem(HoldId);
			if(!pExtra || !pExtra[0])
				continue;
			CEffectContext Ctx = {};
			Ctx.m_pPlayer = pPl;
			Ctx.m_pExtraJson = pExtra;
			GameServer()->Core()->EffectRegistry()->Apply(TRIGGER_WEAPON_FIRE, Ctx);
			AmmoBonus += Ctx.m_OutAmmoBonus;
		}
		if(AmmoBonus > 0)
		{
			for(int w = WEAPON_GUN; w <= WEAPON_LASER; w++)
				pChr->AddWeaponAmmo(w, AmmoBonus);
		}
	}
}

bool CGameController::OnEntity(int Index, vec2 Pos)
{
	int Type = -1;
	int CkItem = -1;

	switch(Index)
	{
	case ENTITY_LOG:
		CkItem = ITEM_LOG;
		break;
	case ENTITY_COAL:
		CkItem = ITEM_COAL;
		break;
	case ENTITY_COPPER:
		CkItem = ITEM_COPPER;
		break;
	case ENTITY_IRON:
		CkItem = ITEM_IRON;
		break;
	case ENTITY_GOLD:
		CkItem = ITEM_GOLD;
		break;
	case ENTITY_DIAMOND:
		CkItem = ITEM_DIAMOND;
		break;
	case ENTITY_ENERGY:
		CkItem = ITEM_ENEGRY;
		break;
	default:
		break;
	}

	if(CkItem != -1)
	{
		new CKs(&GameServer()->m_World, CkItem, Pos);
		return true;
	}

	switch(Index)
	{
		case ENTITY_SPAWN:
			m_alSpawnPoints[0].add(Pos);
			break;
		case ENTITY_SPAWN_RED:
			m_alSpawnPoints[1].add(Pos);
			break;
		case ENTITY_SPAWN_BLUE:
			m_alSpawnPoints[2].add(Pos);
			break;
		case ENTITY_ARMOR_1:
			Type = PICKUP_ARMOR;
			break;
		case ENTITY_HEALTH_1:
			Type = PICKUP_HEALTH;
			break;
		case ENTITY_WEAPON_SHOTGUN:
			Type = PICKUP_SHOTGUN;
			break;
		case ENTITY_WEAPON_GRENADE:
			Type = PICKUP_GRENADE;
			break;
		case ENTITY_WEAPON_LASER:
			Type = PICKUP_LASER;
			break;
		case ENTITY_POWERUP_NINJA:
			Type = PICKUP_NINJA;
	}

	if(Type != -1)
	{
		new CPickup(&GameServer()->m_World, Type, Pos);
		return true;
	}

	return false;
}

bool CGameController::OnExtraTile(int Index, vec2 Pos)
{
	/*
		Example: hook game-layer tile indices between TILE_UNHOOKABLE and ENTITY_OFFSET.

		int Flag = -1;
		switch(Index)
		{
		case TILE_START: Flag = COLFLAG_START; break;
		case TILE_FINISH: Flag = COLFLAG_FINISH; break;
		}
		if(Flag == -1)
			return false;
		GameServer()->Collision()->SetFlagFor(Pos, Flag);
		return true;
	*/

	(void)Index;
	(void)Pos;
	return false;
}

void CGameController::HandleCharacterTiles(class CCharacter *pChr, vec2 LastPos, vec2 NewPos)
{
	(void)pChr;
	(void)LastPos;
	(void)NewPos;
}

void CGameController::OnPlayerConnect(CPlayer *pPlayer)
{
	const int ClientID = pPlayer->GetCID();

	if(!pPlayer->IsDummy() && !pPlayer->IsQuestNpc())
	{
		GameServer()->EnforceSpectatorUntilLogin(pPlayer);

		if(pPlayer->GetAccountId() < 0)
		{
			GameServer()->SendChatLoc(ClientID, "login.hint", "本服务器需要 MySQL 账号 — 使用 /register 或 /login");
			GameServer()->SendBroadcastLoc(ClientID, "login.broadcast", "旁观者模式 — 输入 /register 用户名 密码 或 /login 用户名 密码 加入游戏");
			pPlayer->m_NextLoginHintTick = Server()->Tick() + Server()->TickSpeed() * 20;
			GameServer()->SendChatAllLocF("game.join_spectator", "%s 以旁观者身份加入 — 请 /register 或 /login", Server()->ClientName(ClientID));
		}
		else
		{
			GameServer()->EnterGame(ClientID);
			GameServer()->SendCommunityInfo(ClientID);
		}
	}

	if(pPlayer->GetAccountId() >= 0)
		pPlayer->Respawn();

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' team=%d", ClientID, Server()->ClientName(ClientID), pPlayer->GetTeam());
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	SendGameInfo(ClientID);
}

void CGameController::OnPlayerDisconnect(CPlayer *pPlayer)
{
	pPlayer->OnDisconnect();

	int ClientID = pPlayer->GetCID();
	if(Server()->ClientIngame(ClientID))
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "leave player='%d:%s'", ClientID, Server()->ClientName(ClientID));
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "game", aBuf);
	}

	if(!pPlayer->IsDummy() && pPlayer->GetTeam() != TEAM_SPECTATORS)
	{
		--m_RealPlayerNum;
	}
}

void CGameController::OnPlayerInfoChange(CPlayer *pPlayer)
{
}

void CGameController::OnPlayerReadyChange(CPlayer *pPlayer)
{
	// change players ready state
	pPlayer->m_IsReadyToPlay ^= 1;
}

// general
void CGameController::Snap(int SnappingClient)
{
	CNetObj_GameData *pGameData = static_cast<CNetObj_GameData *>(Server()->SnapNewItem(NETOBJTYPE_GAMEDATA, 0, sizeof(CNetObj_GameData)));
	if(!pGameData)
		return;

	pGameData->m_GameStartTick = Server()->GetOffsetGameTime();
	pGameData->m_GameStateFlags = 0;
	pGameData->m_GameStateEndTick = 0;

	CNetObj_GameDataPrediction *pGameDataPrediction = static_cast<CNetObj_GameDataPrediction *>(Server()->SnapNewItem(NETOBJTYPE_GAMEDATAPREDICTION, 0, sizeof(CNetObj_GameDataPrediction)));
	if(!pGameDataPrediction)
		return;

	pGameDataPrediction->m_PredictionFlags = GAMEPREDICTIONFLAG_EVENT | GAMEPREDICTIONFLAG_INPUT;

	// demo recording
	if(SnappingClient == -1)
	{
		CNetObj_De_GameInfo *pGameInfo = static_cast<CNetObj_De_GameInfo *>(Server()->SnapNewItem(NETOBJTYPE_DE_GAMEINFO, 0, sizeof(CNetObj_De_GameInfo)));
		if(!pGameInfo)
			return;

		pGameInfo->m_GameFlags = 0;
		pGameInfo->m_TimeLimit = 0;
		pGameInfo->m_ScoreLimit = 0;
		pGameInfo->m_MatchNum = 0;
		pGameInfo->m_MatchCurrent = 1;
	}
}

void CGameController::TickLoginReminders()
{
	if(!GameServer()->Accounts() || !GameServer()->Accounts()->IsEnabled())
		return;

	const int Now = Server()->Tick();
	for(int i = 0; i < MAX_HUMAN_CLIENTS; i++)
	{
		CPlayer *pP = GameServer()->m_apPlayers[i];
		if(!pP || pP->IsDummy() || !Server()->ClientIngame(i))
			continue;
		if(pP->GetAccountId() >= 0)
			continue;

		GameServer()->EnforceSpectatorUntilLogin(pP);

		if(Now < pP->m_NextLoginHintTick)
			continue;

		GameServer()->SendChatLoc(i, "login.hint", "本服务器需要 MySQL 账号 — 使用 /register 或 /login");
		GameServer()->SendBroadcastLoc(i, "login.broadcast", "旁观者模式 — 输入 /register 用户名 密码 或 /login 用户名 密码 加入游戏");
		pP->m_NextLoginHintTick = Now + Server()->TickSpeed() * 25;
	}
}

void CGameController::Tick()
{
	TickLoginReminders();
	DoActivityCheck();
}

bool CGameController::IsFriendlyFire(int ClientID1, int ClientID2, int Damage) const
{
	(void)Damage;
	if(ClientID1 == ClientID2)
		return false;

	CPlayer *p1 = GameServer()->m_apPlayers[ClientID1];
	CPlayer *p2 = GameServer()->m_apPlayers[ClientID2];
	if(!p1 || !p2)
		return false;

	return p1->GetTeam() == p2->GetTeam();
}

bool CGameController::IsFriendlyTeamFire(int Team1, int Team2, int Damage) const
{
	return Team1 == Team2;
}

int CGameController::GetPlayerCheckTeam(CPlayer *pPlayer) const
{
	if(!pPlayer)
		return TEAM_RED;
	return pPlayer->GetTeam();
}

void CGameController::SendGameInfo(int ClientID)
{
	/* Bot slots have no net connection; SendPackMsg would assert in CNetServer::Send. */
	if(ClientID >= 0 && ClientID < MAX_CLIENTS)
	{
		CPlayer *pP = GameServer()->m_apPlayers[ClientID];
		if(pP && pP->IsDummy())
			return;
	}

	CNetMsg_Sv_GameInfo GameInfoMsg;
	GameInfoMsg.m_GameFlags = 0;
	GameInfoMsg.m_TimeLimit = 0;
	GameInfoMsg.m_ScoreLimit = 0;
	GameInfoMsg.m_MatchNum = 0;
	GameInfoMsg.m_MatchCurrent = 1;
	Server()->SendPackMsg(&GameInfoMsg, MSGFLAG_VITAL | MSGFLAG_NORECORD, ClientID);
}

// spawn
bool CGameController::CanSpawn(int Team, vec2 *pOutPos) const
{
	if(Team == TEAM_SPECTATORS)
		return false;

	CSpawnEval Eval;
	mem_zero(&Eval, sizeof(Eval));
	if(Team == TEAM_RED)
		EvaluateSpawnType(&Eval, 1);
	else
	{
		Eval.m_RandomSpawn = true;
		EvaluateSpawnType(&Eval, 0);
		EvaluateSpawnType(&Eval, 2);
	}
	*pOutPos = Eval.m_Pos;
	return Eval.m_Got;
}

float CGameController::EvaluateSpawnPos(CSpawnEval *pEval, vec2 Pos) const
{
	float Score = 0.0f;

	for(CGameWorld::TypeRange r = GameServer()->m_World.DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
	{
		CCharacter *pChr = static_cast<CCharacter *>(r.front());
		if(!pChr || !pChr->GetPlayer())
			continue;
		// team mates are not as dangerous as enemies
		float Scoremod = 1.0f;
		if(pEval->m_FriendlyTeam != -1 && pChr->GetPlayer()->GetTeam() == pEval->m_FriendlyTeam)
			Scoremod = 0.5f;

		float d = distance(Pos, pChr->GetPos());
		Score += Scoremod * (d == 0 ? 1000000000.0f : 1.0f / d);
	}

	return Score;
}

void CGameController::EvaluateSpawnType(CSpawnEval *pEval, int Type) const
{
	// get spawn point
	for(int i = 0; i < m_alSpawnPoints[Type].size(); i++)
	{
		// check if the position is occupado
		array<CEntity *> lpEnts;
		lpEnts.hint_size(8);
		int Num = GameServer()->m_World.FindEntities(m_alSpawnPoints[Type][i], 64, lpEnts, CGameWorld::ENTTYPE_CHARACTER);
		vec2 Positions[13] = {
			vec2(0.0f, 0.0f), vec2(-32.0f, 0.0f), vec2(32.0f, 0.0f), vec2(0.0f, -32.0f), vec2(0.0f, 32.0f),
			vec2(-64.0f, 0.0f), vec2(64.0f, 0.0f), vec2(-32.0f, -32.0f), vec2(32.0f, -32.0f),
			vec2(-32.0f, 32.0f), vec2(32.0f, 32.0f), vec2(-96.0f, 0.0f), vec2(96.0f, 0.0f)};
		int Result = -1;
		for(int Index = 0; Index < 13 && Result == -1; ++Index)
		{
			Result = Index;
			for(int c = 0; c < Num; ++c)
				if(GameServer()->Collision()->CheckPoint(m_alSpawnPoints[Type][i] + Positions[Index]) ||
					distance(lpEnts[c]->GetPos(), m_alSpawnPoints[Type][i] + Positions[Index]) <= lpEnts[c]->GetProximityRadius())
				{
					Result = -1;
					break;
				}
		}
		if(Result == -1)
			continue; // try next spawn point

		vec2 P = TdSnapSpawnToGround(m_alSpawnPoints[Type][i] + Positions[Result], CCharacterCore::PHYS_SIZE);
		float S = pEval->m_RandomSpawn ? (Result + random_float()) : EvaluateSpawnPos(pEval, P);
		if(!pEval->m_Got || pEval->m_Score > S)
		{
			pEval->m_Got = true;
			pEval->m_Score = S;
			pEval->m_Pos = P;
		}
	}
}

vec2 CGameController::TdSnapSpawnToGround(vec2 Pos, float PhysSize) const
{
	CCollision *pCol = GameServer()->Collision();
	if(!pCol)
		return Pos;

	const float Half = PhysSize * 0.5f;
	auto MakeStandPos = [&](vec2 Before) {
		return vec2(Pos.x, Before.y - Half - 2.0f);
	};
	auto IsValidStand = [&](vec2 Stand) {
		return !pCol->CheckPoint(Stand) && !pCol->CheckPoint(Stand.x, Stand.y + Half + 2.0f);
	};

	vec2 Col, Before;
	if(pCol->IntersectLine(Pos, Pos + vec2(0.0f, 2048.0f), &Col, &Before))
	{
		const vec2 Stand = MakeStandPos(Before);
		if(!pCol->CheckPoint(Stand) && pCol->CheckPoint(Stand.x, Stand.y + Half + 2.0f))
			return Stand;
	}

	if(pCol->IntersectLine(Pos, Pos + vec2(0.0f, -512.0f), &Col, &Before))
	{
		const vec2 Stand = MakeStandPos(Before);
		if(IsValidStand(Stand) && pCol->CheckPoint(Stand.x, Stand.y + Half + 2.0f))
			return Stand;
	}

	const float Offsets[3] = {0.0f, -16.0f, 16.0f};
	for(int o = 0; o < 3; o++)
	{
		vec2 Probe = Pos;
		for(int Step = 0; Step < 256; Step++)
		{
			Probe.y += 8.0f;
			if(pCol->CheckPoint(Probe.x + Offsets[o], Probe.y + Half + 2.0f) && !pCol->CheckPoint(Probe))
				return Probe;
		}
	}

	return Pos;
}

bool CGameController::GetStartRespawnState() const
{
	return false;
}

// team
bool CGameController::CanChangeTeam(CPlayer *pPlayer, int JoinTeam) const
{
	if(!pPlayer->IsDummy() && JoinTeam == TEAM_BLUE)
		return false;
	if(GameServer()->RequiresLoginToPlay(pPlayer) && pPlayer->GetAccountId() < 0 && JoinTeam != TEAM_SPECTATORS)
		return false;
	return true;
}

bool CGameController::CanJoinTeam(int Team, int NotThisID) const
{
	if(Team != TEAM_SPECTATORS && NotThisID >= 0 && NotThisID < MAX_CLIENTS)
	{
		CPlayer *pP = GameServer()->m_apPlayers[NotThisID];
		if(GameServer()->RequiresLoginToPlay(pP) && pP->GetAccountId() < 0)
			return false;
	}
	return true;
}

int CGameController::ClampTeam(int Team) const
{
	if(Team < TEAM_RED)
		return TEAM_SPECTATORS;
	return TEAM_RED;
}

void CGameController::DoTeamChange(CPlayer *pPlayer, int Team, bool DoChatMsg)
{
	Team = ClampTeam(Team);
	if(Team == pPlayer->GetTeam())
		return;

	int OldTeam = pPlayer->GetTeam();
	pPlayer->SetTeam(Team);

	int ClientID = pPlayer->GetCID();

	// notify clients
	CNetMsg_Sv_Team Msg;
	Msg.m_ClientID = ClientID;
	Msg.m_Team = Team;
	Msg.m_Silent = DoChatMsg ? 0 : 1;
	Msg.m_CooldownTick = pPlayer->m_TeamChangeTick;
	if(ClientID < VANILLA_MAX_CLIENTS)
	{
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);
	}
	else
	{
		for(int i = 0; i < MAX_HUMAN_CLIENTS; i++)
		{
			if(!Server()->ClientIngame(i))
				continue;
			const int DisplayID = GameServer()->ClientDisplaySlot(i, ClientID);
			if(DisplayID < 0)
				continue;
			Msg.m_ClientID = DisplayID;
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, i);
			Msg.m_ClientID = ClientID;
		}
	}

	if(!IsZombiePlayer(pPlayer))
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' team=%d->%d", ClientID, Server()->ClientName(ClientID), OldTeam, Team);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	}

	// update effected game settings
	if(OldTeam != TEAM_SPECTATORS)
	{
		--m_RealPlayerNum;
	}
	if(Team != TEAM_SPECTATORS)
	{
		++m_RealPlayerNum;
	}
	OnPlayerInfoChange(pPlayer);
	GameServer()->OnClientTeamChange(ClientID);

	// reset inactivity counter when joining the game
	if(OldTeam == TEAM_SPECTATORS)
		pPlayer->m_InactivityTickCounter = 0;
}

int CGameController::GetStartTeam()
{
	return TEAM_RED;
}

void CGameController::Com_About(IConsole::IResult *pResult, void *pContext)
{
	CCommandManager::SCommandContext *pCmdContext = static_cast<CCommandManager::SCommandContext *>(pContext);
	CGameController *pSelf = static_cast<CGameController *>(pCmdContext->m_pContext);
	int ClientID = pCmdContext->m_ClientID;
	char aBuf[128];
	pSelf->GameServer()->LocFormat(aBuf, sizeof(aBuf), ClientID, "cmd.about.message", "%s v%s by CometOnOrbit", MOD_NAME, MOD_VERSION);
	pSelf->GameServer()->SendChatTo(ClientID, aBuf);
}

void CGameController::Com_Community(IConsole::IResult *pResult, void *pContext)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCmdContext = static_cast<CCommandManager::SCommandContext *>(pContext);
	CGameController *pSelf = static_cast<CGameController *>(pCmdContext->m_pContext);
	pSelf->GameServer()->SendCommunityInfo(pCmdContext->m_ClientID);
}

void CGameController::RegisterChatCommands(CCommandManager *pManager)
{
	pManager->AddCommand("about", "cmd.about.help", "", Com_About, this);
	pManager->AddCommand("info", "cmd.info.help", "", Com_About, this);
	pManager->AddCommand("community", "cmd.community.help", "", Com_Community, this);
	pManager->AddCommand("qq", "cmd.community.help", "", Com_Community, this);

	if(TWorldController *pCore = GameServer()->Core())
	{
		if(pCore->AccountManager())
			pCore->AccountManager()->RegisterChatCommands(pManager);
		if(pCore->DialogManager())
			pCore->DialogManager()->RegisterChatCommands(pManager);
		if(pCore->VoteMenuManager())
			pCore->VoteMenuManager()->RegisterChatCommands(pManager);
		if(pCore->CraftManager())
		{
			pCore->CraftManager()->RegisterChatCommands(pManager);
			pCore->CraftManager()->RegisterVoteCommands(pManager);
		}
		if(pCore->SkillManager())
		{
			pCore->SkillManager()->RegisterChatCommands(pManager);
			pCore->SkillManager()->RegisterVoteCommands(pManager);
		}
		if(pCore->TraitManager())
			pCore->TraitManager()->RegisterVoteCommands(pManager);
		if(pCore->QuestManager())
		{
			pCore->QuestManager()->RegisterChatCommands(pManager);
			pCore->QuestManager()->RegisterVoteCommands(pManager);
		}
		// Group/trade/friend commands available in ALL worlds
		CGlobalState::RegisterGlobalGroupCommands(GameServer(), pManager);
		CGlobalState::RegisterGlobalTradeCommands(GameServer(), pManager);
		CGlobalState::RegisterGlobalFriendCommands(GameServer(), pManager);

		if(pCore->GetMMOManager())
		{
			pCore->GetMMOManager()->RegisterMMOCommands(pManager);
			pCore->GetMMOManager()->RegisterMMOVoteCommands(pManager);
		}
		if(pCore->AchievementManager())
			pCore->AchievementManager()->RegisterVoteCommands(pManager);
		if(pCore->DutiesManager())
			pCore->DutiesManager()->RegisterVoteCommands(pManager);
		if(pCore->DurabilityManager())
			pCore->DurabilityManager()->RegisterVoteCommands(pManager);
		if(pCore->ProfessionManager())
			pCore->ProfessionManager()->RegisterChatCommands(pManager);
		if(pCore->GuildManager())
			pCore->GuildManager()->RegisterChatCommands(pManager);
		if(pCore->GetDungeonManager())
			pCore->GetDungeonManager()->RegisterChatCommands(pManager);
	}
}

bool CGameController::CanCharacterWeaponFullAuto(CCharacter *pChr, int Weapon)
{
	return Weapon == WEAPON_GRENADE || Weapon == WEAPON_SHOTGUN || Weapon == WEAPON_LASER;
}

int CGameController::OnCharacterFireWeapon(CCharacter *pChr, vec2 Direction, int Weapon)
{
	if(!pChr)
		return 0;

	int ClientID = pChr->GetCID();
	vec2 ChrPos = pChr->GetPos();
	vec2 ProjStartPos = ChrPos + Direction * pChr->GetProximityRadius() * 0.75f;

	CPlayer *pPl = pChr->GetPlayer();
	CItemHelper *pH = GameServer()->ItemHelper();
	const char *pSx = (pPl && pH) ? pPl->GetExtraForItem(pPl->GetHolding(ITYPE_SWORD)) : nullptr;

	int ExtraDmg = 0;
	float MoreForce = 1.f;
	int Electron = 0;
	int ExplosionStacks = 0;

	CEffectContext FxCtx = {};
	FxCtx.m_pAttacker = pChr;
	FxCtx.m_pPlayer = pPl;
	FxCtx.m_pExtraJson = pSx;
	FxCtx.m_Weapon = Weapon;
	FxCtx.m_OutForceMul = 1.f;

	if(Config()->m_SvContentFramework && GameServer()->Core() && GameServer()->Core()->EffectRegistry())
	{
		GameServer()->Core()->EffectRegistry()->Apply(TRIGGER_WEAPON_FIRE, FxCtx);
		ExtraDmg = FxCtx.m_OutDamage;
		MoreForce = FxCtx.m_OutForceMul;
		Electron = FxCtx.m_ElectronStacks;
		ExplosionStacks = FxCtx.m_ExplosionStacks;

		// Aggregate effects from armor items (helm/chest/legs)
		const int ArmorTypes[] = {ITYPE_HELMET, ITYPE_CHEST, ITYPE_LEGS};
		for(int a = 0; a < 3; a++)
		{
			const int ArmorId = pPl->GetHolding(ArmorTypes[a]);
			if(ArmorId <= 0)
				continue;
			const char *pArmorExtra = pPl->GetExtraForItem(ArmorId);
			if(!pArmorExtra || !pArmorExtra[0])
				continue;
		CEffectContext ArmorCtx = {};
		ArmorCtx.m_pPlayer = pPl;
		ArmorCtx.m_pAttacker = pChr;
		ArmorCtx.m_Weapon = Weapon;
		ArmorCtx.m_pExtraJson = pArmorExtra;
		ArmorCtx.m_OutForceMul = 1.f;
		GameServer()->Core()->EffectRegistry()->Apply(TRIGGER_WEAPON_FIRE, ArmorCtx);
		ExtraDmg += ArmorCtx.m_OutDamage;
		MoreForce *= ArmorCtx.m_OutForceMul;
		Electron += ArmorCtx.m_ElectronStacks;
		ExplosionStacks += ArmorCtx.m_ExplosionStacks;
		}
	}
	if(Config()->m_SvContentLegacyCards || !Config()->m_SvContentFramework)
	{
		ExtraDmg = (pH && pSx) ? pH->GetCard(pSx, ITEM_CARD_DAMAGE_ID) * 2 : 0;
		MoreForce = (pH && pSx) ? 1.f + (float)pH->GetCard(pSx, ITEM_CARD_FORCE_ID) * 2.f : 1.f;
		Electron = (pH && pSx) ? pH->GetCard(pSx, ITEM_CARD_ELECTRON_ID) : 0;
		ExplosionStacks = (pH && pSx) ? pH->GetCard(pSx, ITEM_CARD_EXPLOSION_ID) : 0;
	}

	// MMO: TRPG六维 → 武器伤害 + 装备武器攻击/差分
	const CMMOItemDescription *pMMOWeapon = nullptr;
	int WeaponEnchant = 0;
	const SMMOWeaponProfile *pProf = nullptr;
	if(pPl && !pPl->IsDummy())
	{
		if(Weapon == WEAPON_HAMMER)
			ExtraDmg += pPl->GetEffectiveMeleeAttack();
		else
			ExtraDmg += pPl->GetEffectiveRangedAttack();

		pMMOWeapon = GetActiveMMOWeaponDef(pChr, pPl, &WeaponEnchant);
		if(pMMOWeapon)
		{
			ExtraDmg += pMMOWeapon->GetWeaponAttackBonus(WeaponEnchant);
			ExtraDmg += pMMOWeapon->GetEngineWeaponDamageBonus(Weapon, WeaponEnchant);
			if(pMMOWeapon->HasWeaponProfile())
			{
				pProf = &pMMOWeapon->GetWeaponProfile();
				if(pProf->m_ForcePercent != 100)
					MoreForce *= (float)pProf->m_ForcePercent / 100.f;
			}
		}
	}

	if(Server()->Tick() < pChr->m_RetaliationExpireTick && pChr->m_RetaliationStacks > 0)
	{
		const int WeaponDmg = g_pData->m_Weapons.m_aId[Weapon].m_Damage + ExtraDmg;
		ExtraDmg += WeaponDmg * 15 * pChr->m_RetaliationStacks / 100;
	}

	// Shadow Step: next attack bonus (doubled)
	if(pChr && pChr->m_ShadowNextCrit)
	{
		pChr->m_ShadowNextCrit = false;
		const int BaseDmg = g_pData->m_Weapons.m_aId[Weapon].m_Damage + ExtraDmg;
		ExtraDmg += BaseDmg; // effectively doubles damage
	}

	int ReloadTimer = 0;
	switch(Weapon)
	{
		case WEAPON_HAMMER:
		{
			if(GameServer()->Core() && GameServer()->Core()->NpcManager() &&
				GameServer()->Core()->NpcManager()->TryHammerTalk(pChr, ProjStartPos))
			{
				return Server()->TickSpeed() / 3;
			}

			GameServer()->m_World.CreateSound(ChrPos, SOUND_HAMMER_FIRE);

			if(pProf && pProf->m_FireStyle == EMMOFireStyle::HammerBlast)
			{
				const int HamVanilla = g_pData->m_Weapons.m_Hammer.m_pBase->m_Damage;
				const int HamPvp = RollWeaponDamage(HamVanilla + ExtraDmg, pProf);
				const float BlastRadius = (float)pProf->m_HammerBlastRadius;
				for(CGameWorld::TypeRange r = GameServer()->m_World.DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
				{
					CCharacter *pTarget = static_cast<CCharacter *>(r.front());
					if(!MMOWeaponTargetValid(GameServer(), ClientID, pTarget))
						continue;
					if(distance(pTarget->GetPos(), ChrPos) >= BlastRadius)
						continue;
					GameServer()->m_World.CreateExplosion(pTarget->GetPos(), pChr, WEAPON_HAMMER, HamPvp);
					ApplyWeaponLifesteal(pChr, HamPvp, pProf);
				}
				if(length(Direction) > 0.001f)
					pChr->GetCore()->m_Vel += normalize(Direction) * 2.5f;
				GameServer()->m_World.CreateExplosion(ChrPos, pChr, WEAPON_HAMMER, HamPvp);
				ReloadTimer = Server()->TickSpeed() / 3;
				break;
			}

			if(pProf && pProf->m_FireStyle == EMMOFireStyle::HammerLamp)
			{
				const int HamVanilla = g_pData->m_Weapons.m_Hammer.m_pBase->m_Damage;
				const int HamPvp = RollWeaponDamage(HamVanilla + ExtraDmg, pProf);
				const float LampRadius = (float)pProf->m_HammerLampRadius;
				array<CEntity *> lpEnts;
				lpEnts.hint_size(16);
				const int Num = GameServer()->m_World.FindEntities(ProjStartPos, LampRadius, lpEnts, CGameWorld::ENTTYPE_CHARACTER);
				int Spawned = 0;
				for(int i = 0; i < Num && Spawned < 16; ++i)
				{
					CCharacter *pTarget = static_cast<CCharacter *>(lpEnts[i]);
					if(!MMOWeaponTargetValid(GameServer(), ClientID, pTarget))
						continue;
					if(GameServer()->Collision()->IntersectLineWithInvisible(ProjStartPos, pTarget->GetPos(), nullptr, nullptr))
						continue;

					vec2 Dir;
					if(length(pTarget->GetPos() - ChrPos) > 0.0f)
						Dir = normalize(pTarget->GetPos() - ChrPos);
					else
						Dir = vec2(0.f, -1.f);
					const vec2 Force = vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f;

					new CMMOHammerLampBolt(&GameServer()->m_World, ClientID, pTarget->GetCID(), ProjStartPos, Force, HamPvp);
					Spawned++;
				}
				ReloadTimer = (int)(Server()->TickSpeed() * 1.4f);
				break;
			}

			if(Electron > 0)
			{
				for(int i = 0; i < 5; i++)
				{
					const float Spreading[] = {-0.185f, -0.130f, -0.050f, 0.050f, 0.130f, 0.185f};
					float a = angle(Direction);
					a += Spreading[i + 1];
					new CLightning(&GameServer()->m_World, ChrPos, vec2(cosf(a), sinf(a)), 200.f, 100.f, ClientID, ExtraDmg);
				}
			}

			array<CEntity *> lpEnts;
			lpEnts.hint_size(8);
			int Hits = 0;
			const float HammerQueryR = pChr->GetProximityRadius() * 1.5f;
			const int Num = GameServer()->m_World.FindFlagEntities(ProjStartPos, HammerQueryR, lpEnts, CGameWorld::ENTFLAG_HITABLE);
			for(int i = 0; i < Num; ++i)
			{
				CEntity *pHitEnt = lpEnts[i];
				if(pHitEnt->ObjType() == CGameWorld::ENTTYPE_TOWERMAIN || pHitEnt->ObjType() == CGameWorld::ENTTYPE_TURRET)
				{
					const bool IsZombie = pChr->GetPlayer()->IsDummy() || pChr->GetPlayer()->GetTeam() == TEAM_BLUE;
					if(!IsZombie)
					{
						// Human defender hammering turrets: recall or repair own turret
						if(pHitEnt->ObjType() != CGameWorld::ENTTYPE_TURRET)
							continue;
						CTurret *pHammerTurret = static_cast<CTurret *>(pHitEnt);
						if(pHammerTurret->GetOwner() != ClientID)
							continue;
						if(GameServer()->Collision()->IntersectLine(ProjStartPos, pHammerTurret->GetPos(), NULL, NULL))
							continue;

						if(distance(pHammerTurret->GetPos(), ProjStartPos) > 0.0f)
							GameServer()->m_World.CreateHammerHit(pHammerTurret->GetPos() - normalize(pHammerTurret->GetPos() - ProjStartPos) * pChr->GetProximityRadius() * 0.5f);
						else
							GameServer()->m_World.CreateHammerHit(ProjStartPos);

						if(pHammerTurret->IsBroken())
						{
							if(pPl->RepairDeployedTurret())
							{
								GameServer()->SendChatLoc(ClientID, "turret.hammer_repair.ok", "炮塔已修复。");
								if(GameServer()->Accounts())
									GameServer()->Accounts()->RequestSaveAccount(ClientID);
							}
							else
							{
								GameServer()->SendChatLoc(ClientID, "turret.hammer_repair.fail", "修复失败（材料不足或距离太远）。");
							}
						}
						else
						{
							pPl->RecallTurret();
							GameServer()->SendChatLoc(ClientID, "turret.hammer_recall.ok", "炮塔已收回。");
						}
						GameServer()->m_World.CreateSound(pChr->GetPos(), SOUND_PICKUP_ARMOR);
						Hits++;
						continue;
					}

					CHitableEntity *pStructure = static_cast<CHitableEntity *>(pHitEnt);
					if(GameServer()->Collision()->IntersectLine(ProjStartPos, pStructure->GetPos(), NULL, NULL))
						continue;

					if(distance(pStructure->GetPos(), ProjStartPos) > 0.0f)
						GameServer()->m_World.CreateHammerHit(pStructure->GetPos() - normalize(pStructure->GetPos() - ProjStartPos) * pChr->GetProximityRadius() * 0.5f);
					else
						GameServer()->m_World.CreateHammerHit(ProjStartPos);

					vec2 Dir;
					if(length(pStructure->GetPos() - ChrPos) > 0.0f)
						Dir = normalize(pStructure->GetPos() - ChrPos);
					else
						Dir = vec2(0.f, -1.f);

					const int HamVanilla = g_pData->m_Weapons.m_Hammer.m_pBase->m_Damage;
					pStructure->TakeHit(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f * MoreForce, Dir * -1, HamVanilla + ExtraDmg,
						pChr, Weapon);
					Hits++;
					continue;
				}

				if(pHitEnt->ObjType() == CGameWorld::ENTTYPE_SPIDERLEG)
				{
					CHitableEntity *pLeg = static_cast<CHitableEntity *>(pHitEnt);
					if(GameServer()->Collision()->IntersectLine(ProjStartPos, pLeg->GetPos(), NULL, NULL))
						continue;

					if(distance(pLeg->GetPos(), ProjStartPos) > 0.0f)
						GameServer()->m_World.CreateHammerHit(pLeg->GetPos() - normalize(pLeg->GetPos() - ProjStartPos) * pChr->GetProximityRadius() * 0.5f);
					else
						GameServer()->m_World.CreateHammerHit(ProjStartPos);

					vec2 Dir;
					if(length(pLeg->GetPos() - ChrPos) > 0.0f)
						Dir = normalize(pLeg->GetPos() - ChrPos);
					else
						Dir = vec2(0.f, -1.f);

					const int HamVanilla = g_pData->m_Weapons.m_Hammer.m_pBase->m_Damage;
					pLeg->TakeHit(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f * MoreForce, Dir * -1, HamVanilla + ExtraDmg,
						pChr, Weapon);
					Hits++;
					continue;
				}

				if(pHitEnt->ObjType() != CGameWorld::ENTTYPE_CHARACTER)
					continue;

				CCharacter *pTarget = static_cast<CCharacter *>(pHitEnt);

				if((pTarget == pChr) || GameServer()->Collision()->IntersectLine(ProjStartPos, pTarget->GetPos(), NULL, NULL))
					continue;

				if(GameServer()->Core() && GameServer()->Core()->NpcManager() &&
					GameServer()->Core()->NpcManager()->IsQuestNpcCharacter(pTarget))
					continue;

				// set his velocity to fast upward (for now)
				if(length(pTarget->GetPos() - ProjStartPos) > 0.0f)
					GameServer()->m_World.CreateHammerHit(pTarget->GetPos() - normalize(pTarget->GetPos() - ProjStartPos) * pChr->GetProximityRadius() * 0.5f);
				else
					GameServer()->m_World.CreateHammerHit(ProjStartPos);

				vec2 Dir;
				if(length(pTarget->GetPos() - ChrPos) > 0.0f)
					Dir = normalize(pTarget->GetPos() - ChrPos);
				else
					Dir = vec2(0.f, -1.f);

				const int HamVanilla = g_pData->m_Weapons.m_Hammer.m_pBase->m_Damage;
				const int HamPvp = RollWeaponDamage(HamVanilla + ExtraDmg, pProf);

				pTarget->TakeHit(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f * MoreForce, Dir * -1, HamPvp,
					pChr, Weapon);
				ApplyWeaponLifesteal(pChr, HamPvp, pProf);
				for(int e = 0; e < ExplosionStacks; e++)
				{
					const int OffX = (random_int() % 401) - 200;
					const int OffY = (random_int() % 401) - 200;
					GameServer()->m_World.CreateExplosion(vec2(ChrPos.x + OffX, ChrPos.y + OffY), pChr, WEAPON_HAMMER, maximum(1, ExtraDmg));
				}
				Hits++;
			}

			// if we Hit anything, we have to wait for the reload
			if(Hits)
				ReloadTimer = Server()->TickSpeed() / 3;
		}
		break;

		case WEAPON_GUN:
		{
			const int GunDmg = RollWeaponDamage(g_pData->m_Weapons.m_aId[WEAPON_GUN].m_Damage + ExtraDmg, pProf);
			const int LifeTicks = (int)(Server()->TickSpeed() * GameServer()->Tuning()->m_GunLifetime);
			const int FanShots = pProf ? pProf->m_FanShots : 0;
			const bool Pulse = pProf && pProf->m_Pulse && FanShots < 2;
			const float PulseReach = pProf ? (float)pProf->m_PulseReach : 400.f;
			const int HostLaser = pPl ? pPl->GetHolding(ITYPE_SWORD) : -1;
			if(FanShots >= 2)
			{
				const float FanSpread = WeaponFanSpreadRadians(pProf);
				const vec2 BaseDir = ApplyWeaponSpread(Direction, WeaponExtraSpread(pProf));
				for(int i = 0; i < FanShots; i++)
				{
					const vec2 FireDir = WeaponFanDirection(BaseDir, i, FanShots, FanSpread);
					SpawnDirectionalProjectile(&GameServer()->m_World, WEAPON_GUN, ClientID, ProjStartPos, FireDir,
						LifeTicks, GunDmg, WeaponProfileExplosive(pProf, false), MoreForce, -1, pProf);
				}
			}
			else
			{
				const int Shots = 1 + (pProf ? pProf->m_Multishot : 0);
				for(int s = 0; s < Shots; s++)
				{
					vec2 FireDir = ApplyWeaponSpread(Direction, WeaponExtraSpread(pProf));
					if(Shots > 1)
					{
						const float Offset = (s - (Shots - 1) * 0.5f) * 4.f * pi / 180.f;
						const float a = angle(FireDir) + Offset;
						FireDir = vec2(cosf(a), sinf(a));
					}
					if(Pulse)
					{
						new CLaser(&GameServer()->m_World, ChrPos, FireDir, PulseReach, ClientID, GunDmg, false, MoreForce, 0, HostLaser);
						ApplyWeaponLifesteal(pChr, GunDmg, pProf);
					}
					else
					{
						SpawnDirectionalProjectile(&GameServer()->m_World, WEAPON_GUN, ClientID, ProjStartPos, FireDir,
							LifeTicks, GunDmg, WeaponProfileExplosive(pProf, false), MoreForce, -1, pProf);
					}
				}
			}
			ApplyWeaponRecoil(pChr, Direction, pProf);
			GameServer()->m_World.CreateSound(ChrPos, Pulse ? SOUND_LASER_FIRE : SOUND_GUN_FIRE);
		}
		break;

		case WEAPON_SHOTGUN:
		{
			const int ShotgunDmg = RollWeaponDamage(g_pData->m_Weapons.m_aId[WEAPON_SHOTGUN].m_Damage + ExtraDmg, pProf);
			const int ExtraSpread = WeaponExtraSpread(pProf);
			const int LifeTicks = (int)(Server()->TickSpeed() * GameServer()->Tuning()->m_ShotgunLifetime);
			const int PelletCount = pProf ? pProf->m_ShotgunPelletCount : 0;

			if(PelletCount > 0)
			{
				const vec2 BaseDir = ApplyWeaponSpread(Direction, ExtraSpread);
				for(int i = 0; i < PelletCount; i++)
				{
					const float SpreadAngle = (0.0058945f * (9.0f * PelletCount) / 2.0f) - (0.0058945f * (9.0f * i));
					const float a = angle(BaseDir) + SpreadAngle;
					const float Speed = (float)GameServer()->Tuning()->m_ShotgunSpeeddiff + random_float() * 0.2f;
					SpawnDirectionalProjectile(&GameServer()->m_World, WEAPON_SHOTGUN, ClientID, ProjStartPos,
						vec2(cosf(a), sinf(a)) * Speed, LifeTicks, ShotgunDmg,
						WeaponProfileExplosive(pProf, false), MoreForce, -1, pProf);
				}
			}
			else
			{
				int ShotSpread = clamp(2 + (pProf ? pProf->m_ShotgunPelletsAdd : 0), 1, 4);
				for(int i = -ShotSpread; i <= ShotSpread; ++i)
				{
					float a = angle(Direction);
					if(ShotSpread > 0)
						a += (i / (float)ShotSpread) * 0.185f;
					if(ExtraSpread > 0)
					{
						const float SpreadRad = ExtraSpread * pi / 180.f;
						a += (random_int() % 2001 - 1000) / 1000.f * SpreadRad;
					}
					float v = 1 - (absolute(i) / (float)maximum(1, ShotSpread));
					float Speed = mix((float) GameServer()->Tuning()->m_ShotgunSpeeddiff, 1.0f, v);
					SpawnDirectionalProjectile(&GameServer()->m_World, WEAPON_SHOTGUN, ClientID, ProjStartPos,
						vec2(cosf(a), sinf(a)) * Speed, LifeTicks, ShotgunDmg,
						WeaponProfileExplosive(pProf, false), MoreForce, -1, pProf);
				}
			}

			ApplyWeaponRecoil(pChr, Direction, pProf);
			GameServer()->m_World.CreateSound(ChrPos, SOUND_SHOTGUN_FIRE);
		}
		break;

		case WEAPON_GRENADE:
		{
			const int GrenadeDmg = RollWeaponDamage(g_pData->m_Weapons.m_aId[WEAPON_GRENADE].m_Damage + ExtraDmg, pProf);
			if(pProf && pProf->m_FireStyle == EMMOFireStyle::HomingGrenade)
			{
				const vec2 FireDir = ApplyWeaponSpread(Direction, WeaponExtraSpread(pProf));
				new CMMOHomingGrenade(&GameServer()->m_World, ClientID, ProjStartPos, FireDir, GrenadeDmg, (float)pProf->m_HomingGrenadeSpeed);
				ApplyWeaponRecoil(pChr, Direction, pProf);
				GameServer()->m_World.CreateSound(ChrPos, SOUND_GRENADE_FIRE);
				break;
			}
			const int FanShots = pProf ? pProf->m_FanShots : 0;
			if(FanShots >= 2)
			{
				const float FanSpread = WeaponFanSpreadRadians(pProf);
				const int LifePct = (pProf && pProf->m_GrenadeSalvoLifetimePercent > 0)
					? pProf->m_GrenadeSalvoLifetimePercent : 80;
				const int LifeTicks = (int)(Server()->TickSpeed() * GameServer()->Tuning()->m_GrenadeLifetime * LifePct / 100.f);
				const vec2 BaseDir = ApplyWeaponSpread(Direction, WeaponExtraSpread(pProf));
				for(int i = 0; i < FanShots; i++)
				{
					const float Center = (FanShots - 1) * 0.5f;
					const float FanIdx = i - Center;
					const float a = angle(BaseDir) + FanIdx * FanSpread;
					const vec2 FireDir = vec2(cosf(a), sinf(a)) * (1.0f - absolute(FanIdx) * 0.1f);
					SpawnDirectionalProjectile(&GameServer()->m_World, WEAPON_GRENADE, ClientID, ProjStartPos, FireDir,
						LifeTicks, GrenadeDmg, WeaponProfileExplosive(pProf, true), MoreForce, SOUND_GRENADE_EXPLODE, pProf);
				}
			}
			else
			{
				const vec2 FireDir = ApplyWeaponSpread(Direction, WeaponExtraSpread(pProf));
				const int LifeTicks = (int)(Server()->TickSpeed() * GameServer()->Tuning()->m_GrenadeLifetime);
				SpawnDirectionalProjectile(&GameServer()->m_World, WEAPON_GRENADE, ClientID, ProjStartPos, FireDir,
					LifeTicks, GrenadeDmg, WeaponProfileExplosive(pProf, true), MoreForce, SOUND_GRENADE_EXPLODE, pProf);
			}
			ApplyWeaponRecoil(pChr, Direction, pProf);
			GameServer()->m_World.CreateSound(ChrPos, SOUND_GRENADE_FIRE);
		}
		break;

		case WEAPON_LASER:
		{
			const int LaserDmg = RollWeaponDamage(g_pData->m_Weapons.m_aId[WEAPON_LASER].m_Damage + ExtraDmg, pProf);
			const vec2 BaseDir = ApplyWeaponSpread(Direction, WeaponExtraSpread(pProf));
			if(pProf && pProf->m_FireStyle != EMMOFireStyle::Default)
			{
				bool HandledFireStyle = false;
				if(pProf->m_FireStyle == EMMOFireStyle::MagneticPulse)
				{
					new CMMOMagneticPulse(&GameServer()->m_World, ClientID, (float)pProf->m_MagneticRadius, ProjStartPos, BaseDir);
					HandledFireStyle = true;
				}
				else if(pProf->m_FireStyle == EMMOFireStyle::WallPusher)
				{
					const int LifeTicks = pProf->m_WallPusherLifeTicks > 0 ? pProf->m_WallPusherLifeTicks : Server()->TickSpeed() * 5;
					new CMMOWallPusher(&GameServer()->m_World, ClientID, ProjStartPos, BaseDir, LifeTicks, LaserDmg);
					HandledFireStyle = true;
				}
				else if(pProf->m_FireStyle == EMMOFireStyle::TeslaChain)
				{
					const float Falloff = pProf->m_TeslaDamageFalloff / 100.f;
					new CMMOTeslaChain(&GameServer()->m_World, ClientID, ProjStartPos, BaseDir, LaserDmg,
						(float)pProf->m_TeslaChainRange, pProf->m_TeslaChainTargets, Falloff);
					HandledFireStyle = true;
				}
				else if(pProf->m_FireStyle == EMMOFireStyle::TrackedPlasma)
				{
					new CMMOTrackedPlasma(&GameServer()->m_World, ClientID, ProjStartPos, BaseDir, LaserDmg,
						(float)pProf->m_TrackedPlasmaSpeedMin, (float)pProf->m_TrackedPlasmaSpeedMax);
					HandledFireStyle = true;
				}
				if(HandledFireStyle)
				{
					ApplyWeaponRecoil(pChr, Direction, pProf);
					GameServer()->m_World.CreateSound(ChrPos, SOUND_LASER_FIRE);
					break;
				}
			}
			const int FanShots = pProf ? pProf->m_FanShots : 0;
			const float LaserReach = WeaponLaserReach(GameServer()->Tuning(), pProf);
			if(Electron > 0)
			{
				vec2 Start = ChrPos + BaseDir * 50.f;
				float a = angle(BaseDir);
				vec2 To = ChrPos + vec2(cosf(a), sinf(a)) * 400.f;
				GameServer()->Collision()->IntersectLine(Start, To, 0x0, &To);
				vec2 At;
				CCharacter *pHit = GameServer()->m_World.IntersectCharacter(Start, To, 70.f, At, pChr);
				if(pHit)
				{
					To = pHit->GetPos();
					pHit->TakeHit(BaseDir, BaseDir * -1, LaserDmg, pChr, WEAPON_LASER);
					ApplyWeaponLifesteal(pChr, LaserDmg, pProf);
				}
				int Segments = distance(Start, To) / 100;
				Segments = clamp(Segments, 2, 4);
				new CElectro(&GameServer()->m_World, Start, To, vec2(cosf(a * 1.2f), sinf(a * 1.2f)) * 40.f, Segments);
			}
			const int HostLaser = pPl ? pPl->GetHolding(ITYPE_SWORD) : -1;
			if(FanShots >= 2)
			{
				const float FanSpread = WeaponFanSpreadRadians(pProf);
				for(int i = 0; i < FanShots; i++)
				{
					const vec2 FireDir = WeaponFanDirection(BaseDir, i, FanShots, FanSpread);
					new CLaser(&GameServer()->m_World, ChrPos, FireDir, LaserReach, ClientID, LaserDmg, false, MoreForce, 0, HostLaser);
				}
			}
			else
			{
				new CLaser(&GameServer()->m_World, ChrPos, BaseDir, LaserReach, ClientID, LaserDmg, false, MoreForce, 0, HostLaser);
			}
			ApplyWeaponRecoil(pChr, Direction, pProf);
			GameServer()->m_World.CreateSound(ChrPos, SOUND_LASER_FIRE);
		}
		break;

		case WEAPON_NINJA:
		{
			pChr->DoNinjaFire(Direction, g_pData->m_Weapons.m_Ninja.m_Movetime * Server()->TickSpeed() / 1000);
			GameServer()->m_World.CreateSound(ChrPos, SOUND_NINJA_FIRE);
		}
		break;
	}
	int LessReload = 0;
	if(Config()->m_SvContentFramework && GameServer()->Core() && GameServer()->Core()->EffectRegistry())
	{
		CEffectContext ReloadCtx = FxCtx;
		GameServer()->Core()->EffectRegistry()->Apply(TRIGGER_RELOAD, ReloadCtx);
		LessReload = ReloadCtx.m_OutReloadDelta;

		// Aggregate reload speed from armor items (helm/chest/legs)
		const int ArmorTypes[] = {ITYPE_HELMET, ITYPE_CHEST, ITYPE_LEGS};
		for(int a = 0; a < 3; a++)
		{
			const int ArmorId = pPl->GetHolding(ArmorTypes[a]);
			if(ArmorId <= 0)
				continue;
			const char *pArmorExtra = pPl->GetExtraForItem(ArmorId);
			if(!pArmorExtra || !pArmorExtra[0])
				continue;
		CEffectContext ArmorCtx = {};
		ArmorCtx.m_pPlayer = pPl;
		ArmorCtx.m_pAttacker = pChr;
		ArmorCtx.m_Weapon = Weapon;
		ArmorCtx.m_pExtraJson = pArmorExtra;
		GameServer()->Core()->EffectRegistry()->Apply(TRIGGER_RELOAD, ArmorCtx);
			LessReload += ArmorCtx.m_OutReloadDelta;
		}
	}
	if(Config()->m_SvContentLegacyCards || !Config()->m_SvContentFramework)
		LessReload = (pH && pSx) ? 10 * pH->GetCard(pSx, ITEM_CARD_QUICKLY_FIRE_ID) : 0;
	if(!ReloadTimer)
		ReloadTimer = maximum(0, g_pData->m_Weapons.m_aId[Weapon].m_Firedelay * Server()->TickSpeed() / 1000 - LessReload);

	if(pProf && pProf->m_ReloadPercent != 100 && ReloadTimer > 0)
		ReloadTimer = maximum(1, ReloadTimer * pProf->m_ReloadPercent / 100);

	if(pPl && !pPl->IsDummy() && ReloadTimer > 0)
	{
		const int AttackSpd = EffectiveAttackSpeedPercent(pPl, pMMOWeapon, WeaponEnchant);
		if(AttackSpd != 100)
			ReloadTimer = maximum(1, ReloadTimer * 100 / AttackSpd);
	}

	return ReloadTimer;
}

void CGameController::NotifyPlayerConnected(CPlayer *pPlayer)
{
	if(!pPlayer->IsDummy() && pPlayer->GetTeam() != TEAM_SPECTATORS)
		++m_RealPlayerNum;
}

