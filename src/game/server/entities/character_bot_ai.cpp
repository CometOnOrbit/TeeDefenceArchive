/* Adapted from Teeworlds-MRPG-0.6 character_bot.cpp — MRPG exact structure */

#include "character_bot_ai.h"
#include "ai_core/mob_ai.h"
#include "ai_core/npc_ai.h"
#include "ai_core/quest_mob_ai.h"
#include "ai_core/quest_npc_ai.h"
#include <game/collision.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/core/components/mmo/mmo_manager.h>
#include <game/server/core/components/mmo/mmo_types.h>
#include <game/server/data_center.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/core/components/quests/quest_manager.h>
#include <game/server/core/tools/path_finder.h>
#include <engine/shared/config.h>
#include <game/server/gamecontroller.h>
#include <game/server/item_system.h>
#include <game/server/core/components/content/effect_registry.h>
#include <game/server/core/components/content/status_manager.h>
#include <game/server/mmo_exp.h>
#include <game/server/core/tworld_component.h>

MACRO_ALLOC_POOL_ID_IMPL(CCharacterBotAI, MAX_CLIENTS)


// static pool state check — ms_PoolUsedCCharacterBotAI is defined by
// the MACRO_ALLOC_POOL_ID_IMPL macro just above, so it's in scope here.
bool CCharacterBotAI::PoolSlotFree(int id)
{
	if(id < 0 || id >= MAX_CLIENTS) return false;
	// ms_PoolUsedCCharacterBotAI is a file-scope static from the macro above
	extern int ms_PoolUsedCCharacterBotAI[];
	return !ms_PoolUsedCCharacterBotAI[id];
}

CCharacterBotAI::CCharacterBotAI(CGameWorld* pWorld) : CCharacter(pWorld)
{
	m_aDamageByPlayer.reserve(MAX_CLIENTS);
	m_LastStrafeChangeTick = 0;
}

bool CCharacterBotAI::Spawn(CPlayer* pPlayer, vec2 Pos)
{
	if(!CCharacter::Spawn(pPlayer, Pos))
		return false;

	// Inherit world ID from player
	m_Core.m_WorldID = pPlayer->GetCurrentWorldID();

	// Critical: tell the CPlayer about this character so CPlayer::Tick()
	// doesn't auto-Respawn() a regular CCharacter (which would overwrite
	// our core registration in m_apCharacters[] and break hook collision).
	pPlayer->m_pCharacter = this;

	// Disable auto-respawn — MMO bot lifecycle is managed by TickWorldSpawns.
	// Without this, CPlayer::Tick() respawns a regular CCharacter 3s after death,
	// overwriting our core registration and breaking the hook line mid-pull.
	pPlayer->m_RespawnDisabled = true;

	m_pBotPlayer = pPlayer;

	if(!m_pTilesHandler)
		m_pTilesHandler = new CTileHandler(GameServer()->Collision(), this);

	// Give weapons — mirror MRPG's equip-based logic
	GiveWeapon(WEAPON_HAMMER, -1);
	GiveWeapon(WEAPON_GUN, 10);
	GiveWeapon(WEAPON_SHOTGUN, 10);
	GiveWeapon(WEAPON_GRENADE, 10);
	GiveWeapon(WEAPON_LASER, 10);

	if(!m_pAI && pPlayer->m_pMMOBotData)
	{
		const SMMOBotData *pData = pPlayer->m_pMMOBotData;
		if(pData->m_IsQuestMob)
			m_pAI = std::make_unique<CQuestMobAI>(this, pPlayer->m_pQuestMobInfo);
		else if(pData->m_IsNPC)
			m_pAI = std::make_unique<CNpcAI>(this, pData->m_NpcFunction);
		else
		{
			float AR = pData->m_IsBoss ? 1500.f : 800.f;
			m_pAI = std::make_unique<CMobAI>(this, AR);
			// Pass mob definition pointer for behavior flags
			{
				const SMMOMobDef *pDef = SMMOMobDef::Get(pData->m_DefID);
				if(pDef)
					static_cast<CMobAI*>(m_pAI.get())->SetMobInfo(pDef);
			}
		}
	}

	if(m_pAI)
		m_pAI->OnSpawn();

	return true;
}

// ─── TakeHit (virtual override) — MMO damage pipeline ────────────

bool CCharacterBotAI::TakeHit(vec2 Force, vec2 Source, int Dmg, CEntity *pFrom, int Weapon)
{
	CPlayer *pMyPlayer = GetPlayer();
	SMMOBotData *pData = pMyPlayer ? pMyPlayer->m_pMMOBotData : nullptr;
	if(!pData) return CCharacter::TakeHit(Force, Source, Dmg, pFrom, Weapon);

	if(!pData->IsAlive()) return false;

	m_Core.m_Vel += Force;

	int From = GameWorld()->DamageOwnerFromEntity(pFrom);

	// MMO damage: apply attacker's attack stat vs target defense
	int AttackerMMOAttack = 10; // default base damage factor
	if(From >= 0)
	{
		CPlayer *pAttacker = GameServer()->m_apPlayers[From];
		if(pAttacker && !pAttacker->m_pMMOBotData)
			AttackerMMOAttack = maximum(1, pAttacker->m_MMOAttack);
		else if(pAttacker && pAttacker->m_pMMOBotData)
			AttackerMMOAttack = maximum(1, pAttacker->m_pMMOBotData->m_Attack);
	}
	// Damage formula: scale by attack/(attack+defense) with per-weapon multipliers
	// Melee weapons (hammer) have higher DPS, ranged (gun) have convenience tax
	int WeaponScale = 5;
	switch(Weapon)
	{
		case WEAPON_HAMMER:  WeaponScale = 8; break;
		case WEAPON_GUN:     WeaponScale = 3; break;
		case WEAPON_SHOTGUN: WeaponScale = 5; break;
		case WEAPON_GRENADE: WeaponScale = 3; break;
		case WEAPON_LASER:   WeaponScale = 4; break;
		case WEAPON_NINJA:   WeaponScale = 8; break;
	}
	int ScaledDmg = Dmg * WeaponScale;
	int EffDmg = maximum(1, ScaledDmg * AttackerMMOAttack / (AttackerMMOAttack + pData->m_Defense));
	pData->m_HP -= EffDmg;

	if(From >= 0)
	{
		m_aDamageByPlayer[From] += EffDmg;
		if(m_pAI)
			m_pAI->OnTakeDamage(EffDmg, From, Weapon);
	}

	if(EffDmg > 0 && From >= 0 && GameServer()->m_apPlayers[From] && !GameServer()->m_apPlayers[From]->IsDummy())
	{
		if(Config()->m_SvContentFramework && GameServer()->Core() && GameServer()->Core()->EffectRegistry())
		{
			CEffectContext Ctx = {};
			Ctx.m_pAttacker = GameServer()->m_apPlayers[From]->GetCharacter();
			Ctx.m_pVictim = this;
			Ctx.m_pPlayer = GameServer()->m_apPlayers[From];
			Ctx.m_pExtraJson = GameServer()->m_apPlayers[From]->GetExtraForItem(GameServer()->m_apPlayers[From]->GetHolding(ITYPE_SWORD));
			Ctx.m_Weapon = Weapon;
			Ctx.m_Source = Source;
			Ctx.m_InDamage = EffDmg;
			GameServer()->Core()->EffectRegistry()->Apply(TRIGGER_DEAL_DAMAGE, Ctx);

			const int ArmorTypes[] = {ITYPE_HELMET, ITYPE_CHEST, ITYPE_LEGS};
			for(int a = 0; a < 3; a++)
			{
				const int ArmorId = GameServer()->m_apPlayers[From]->GetHolding(ArmorTypes[a]);
				if(ArmorId <= 0)
					continue;
				const char *pArmorExtra = GameServer()->m_apPlayers[From]->GetExtraForItem(ArmorId);
				if(!pArmorExtra || !pArmorExtra[0])
					continue;
				CEffectContext ArmorCtx = {};
				ArmorCtx.m_pAttacker = GameServer()->m_apPlayers[From]->GetCharacter();
				ArmorCtx.m_pVictim = this;
				ArmorCtx.m_pPlayer = GameServer()->m_apPlayers[From];
				ArmorCtx.m_pExtraJson = pArmorExtra;
				ArmorCtx.m_Weapon = Weapon;
				ArmorCtx.m_Source = Source;
				ArmorCtx.m_InDamage = EffDmg;
				GameServer()->Core()->EffectRegistry()->Apply(TRIGGER_DEAL_DAMAGE, ArmorCtx);
			}
		}
	}

	if(EffDmg > 9)
		GameWorld()->CreateFloatingAmount(m_Pos, pMyPlayer->GetCID(), EffDmg);
	else
		GameWorld()->CreateDamage(m_Pos, pMyPlayer->GetCID(), Source, EffDmg, 0,
			From == pMyPlayer->GetCID());

	if(From >= 0 && From != pMyPlayer->GetCID() && GameServer()->m_apPlayers[From])
	{
		int64 Mask = From < MAX_HUMAN_CLIENTS ? CmaskOne(From) : CmaskAll();
		for(int i = 0; i < MAX_HUMAN_CLIENTS; i++)
		{
			if(GameServer()->m_apPlayers[i] && (GameServer()->m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS || GameServer()->m_apPlayers[i]->m_DeadSpecMode) &&
				GameServer()->m_apPlayers[i]->GetSpectatorID() == From)
				Mask |= CmaskOne(i);
		}
		GameWorld()->CreateSound(GameServer()->m_apPlayers[From]->m_ViewPos, SOUND_HIT, Mask);
	}

	if(pData->m_HP <= 0)
	{
		Die(From, Weapon);
		return false;
	}

	if(Dmg > 2)
		GameWorld()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_LONG);
	else
		GameWorld()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_SHORT);
	SetEmote(EMOTE_PAIN, Server()->Tick() + 500 * Server()->TickSpeed() / 1000);
	return true;
}

// ─── Die ──────────────────────────────────────────────────────────

void CCharacterBotAI::Die(int Killer, int Weapon)
{
	CPlayer *pMyPlayer = GetPlayer();
	if(!pMyPlayer || !pMyPlayer->m_pMMOBotData) return;

	SMMOBotData *pData = pMyPlayer->m_pMMOBotData;
	CGameContext *pGS = GameServer();

	if(m_pAI)
		m_pAI->OnDie(Killer, Weapon);

	// Reward all damage dealers
	if(Weapon != WEAPON_SELF && Weapon != WEAPON_WORLD && !pData->m_IsNPC && !pData->m_IsQuestMob)
	{
		for(const auto &[ClientID, Damage] : m_aDamageByPlayer)
		{
			CPlayer *pPlayer = (ClientID >= 0 && ClientID < MAX_CLIENTS) ? pGS->m_apPlayers[ClientID] : nullptr;
			if(!pPlayer || pPlayer->m_pMMOBotData) continue;

			int Gold = pData->m_GoldMin;
			if(pData->m_GoldMax > pData->m_GoldMin)
				Gold += random_int() % (pData->m_GoldMax - pData->m_GoldMin + 1);
			pPlayer->m_MMOGold += Gold;
			pPlayer->m_MMODirty = true;

			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "获得 %d 金币", Gold);
			pGS->SendChat(ClientID, CHAT_ALL, -1, aBuf);

			// Experience reward
			int ExpReward = pData->m_ExpReward;
			if(pData->m_Level > 1)
				ExpReward = ExpReward * pData->m_Level; // scale by mob level
			pPlayer->AddMMOExperience(ExpReward);

			for(const auto &Drop : pData->m_vDrops)
			{
				int Roll = random_int() % 100;
				if(Roll >= Drop.m_Chance) continue;
				int Count = Drop.m_MinCount;
				if(Drop.m_MaxCount > Drop.m_MinCount)
					Count += random_int() % (Drop.m_MaxCount - Drop.m_MinCount + 1);
				CInventoryManager::AddItem(pPlayer->m_MMOInventory, Drop.m_ItemID, Count);
				pPlayer->m_MMODirty = true;

				const CMMOItemDescription *pDesc = CMMOItemDescription::Get(Drop.m_ItemID);
				const char *pItemName = pDesc ? pDesc->m_aName : "未知物品";
				str_format(aBuf, sizeof(aBuf), "获得 x%d %s", Count, pItemName);
				pGS->SendChat(ClientID, CHAT_ALL, -1, aBuf);
			}

			// Notify quest system of this kill (pass mob def ID + zone name)
			CQuestManager *pQM = pGS->Core()->QuestManager();
			if(pQM)
				pQM->OnPlayerKill(pPlayer, pData->m_DefID, m_ZoneName);

			char aMsg[128];
			str_format(aMsg, sizeof(aMsg), "%s 击败了 %s!",
				pGS->Server()->ClientName(ClientID),
				pGS->Server()->ClientName(pMyPlayer->GetCID()));
			pGS->SendChat(-1, CHAT_ALL, -1, aMsg);
		}
	}

	m_aDamageByPlayer.clear();
	m_BotTargetPos.reset();
	if(m_pAI && m_pAI->GetTarget())
		m_pAI->GetTarget()->Reset();

	pData->m_Respawning = true;
	pData->m_DeathTick = pGS->Server()->Tick();
	pData->m_RespawnAtTick = pGS->Server()->Tick() + pData->m_RespawnTicks;
	CCharacter::Die(Killer, Weapon);
}

// ─── Tick (MRPG exact flow, adapted for TDA API) ─────────────────

void CCharacterBotAI::Tick()
{
	if(!IsAlive())
		return;

	// debug: print bot state every 5 seconds
	/*if(Server()->Tick() % (Server()->TickSpeed() * 5) == 0)
	{
		dbg_msg("mmo_bot", "[CID=%d] alive=1 pos=(%.0f,%.0f) core_in_world=%d hook_state=%d hooked=%d",
			m_pPlayer->GetCID(), m_Core.m_Pos.x, m_Core.m_Pos.y,
			GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] == &m_Core,
			m_Core.m_HookState, m_Core.m_HookedPlayer);
	}*/

	// ── Core ──
	//if(Server()->Tick() % (Server()->TickSpeed() * 2) == 0)
	//	dbg_msg("mmo_bot", "[CID=%d] PRE_CORE m_Input.m_Hook=%d m_Input.m_Target=(%d,%d) m_Core.m_Hook=%d m_Core.m_HookState=%d",
	//		m_pPlayer->GetCID(), m_Input.m_Hook, m_Input.m_TargetX, m_Input.m_TargetY, m_Core.m_Input.m_Hook, m_Core.m_HookState);
	m_Core.m_Input = m_Input;

	// Override m_Hook with m_WantHook — survives the zeroing path
	if(m_WantHook)
	{
		m_Core.m_Input.m_Hook = 1;
	}
	m_Core.Tick(true);

	// Tile processing
	int MapIndex = GameServer()->Collision()->GetPureMapIndex(m_Pos.x, m_Pos.y);
	if(m_pTilesHandler)
		m_pTilesHandler->Handle(MapIndex);

	// MRPG extensions
	HandleTuning();
	HandleSafeFlags();

	ResetInput();

	// Save previous input for edge detection in FireWeapon
	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));

	// ── AI process + weapons ──
	ProcessBot();

	// game clipped / death tile
	if(GameLayerClipped(m_Pos))
	{
		Die(GetPlayer() ? GetPlayer()->GetCID() : -1, WEAPON_WORLD);
		return;
	}

	// DEBUG: m_Input state at end of Tick
	// if(Server()->Tick() % (Server()->TickSpeed() * 2) == 0)
	// 	dbg_msg("mmo_bot", "[CID=%d] POST_MOVE m_Input.m_Hook=%d m_Input.m_Target=(%d,%d)", m_pPlayer->GetCID(), m_Input.m_Hook, m_Input.m_TargetX, m_Input.m_TargetY);

	// TDA-specific effects (cards, status, etc)
	if(m_pPlayer && !m_pPlayer->IsDummy() && GameServer()->ItemHelper())
	{
		CItemHelper *pH = GameServer()->ItemHelper();
		const int LegsId = m_pPlayer->GetHolding(ITYPE_LEGS);
		const int SwiftStacks = pH->GetEffectStacksFromExtra(m_pPlayer->GetExtraForItem(LegsId), ITEM_CARD_SWIFTNESS, "swiftness");
		if(SwiftStacks > 0)
			m_Core.m_Vel.x *= 1.f + 0.08f * (float)SwiftStacks;

		const int RegenStacks = pH->SumArmorEffectStacks(m_pPlayer, ITEM_CARD_REGENERATION, "regeneration");
		if(RegenStacks > 0 && Server()->Tick() % (Server()->TickSpeed() * 3) == 0)
			IncreaseHealth(RegenStacks);
	}

	// ── HP bar ──
	CPlayer *pMyPlayer = GetPlayer();
	SMMOBotData *pData = pMyPlayer ? pMyPlayer->m_pMMOBotData : nullptr;
	if(pData && pData->m_MaxHP > 0)
	{
		char aClan[64];
		int pct = clamp((int)(pData->GetHPPct() * 100.0f), 0, 100);
		str_format(aClan, sizeof(aClan), "[HP: %d/%d %d%%]", pData->m_HP, pData->m_MaxHP, pct);
		Server()->SetClientClan(pMyPlayer->GetCID(), aClan);
	}
}

// ─── TickDeferred (MRPG exact) ──────────────────────────────────

void CCharacterBotAI::TickDefered()
{
	if(!IsAlive())
		return;

	// Apply hook interaction velocity (from player hooking us, or us hooking
	// another entity). Must be called BEFORE Move() so drag affects position.
	// Regular CCharacter::Tick() calls this but CCharacterBotAI has its own
	// Tick() that bypasses the parent — so we do it here in TickDefered.
	if(m_ActiveWeapon != WEAPON_NINJA || m_Ninja.m_CurrentMoveTime < 0)
		m_Core.AddDragVelocity();
	m_Core.ResetDragVelocity();

	m_Core.Move();
	m_Core.Quantize();
	m_PrevPos = m_Pos;
	m_Pos = m_Core.m_Pos;

	// update m_SendCore for client-side dead reckoning
	// bots have no client prediction, so ReckoningCore never diverges from Core;
	// always sync them together
	m_ReckoningTick = Server()->Tick();
	m_SendCore = m_Core;
	m_ReckoningCore = m_Core;
}

// ─── Snap (mirrors MRPG's simple approach) ──────────────────────

void CCharacterBotAI::Snap(int SnappingClient)
{
	CPlayer *pMyPlayer = GetPlayer();
	if(!pMyPlayer)
	{
		CCharacter::Snap(SnappingClient);
		return;
	}

	int ID = pMyPlayer->GetCID();

	if(SnappingClient >= 0 && !pMyPlayer->IsVisibleForClient(SnappingClient))
		return;

	if(NetworkClipped(SnappingClient))
	{
		if(m_Core.m_HookState == HOOK_IDLE)
			return;
		if(NetworkClippedLine(SnappingClient, m_Pos, m_Core.m_HookPos))
			return;
	}

	if(SnappingClient >= 0 && !Server()->Translate(ID, SnappingClient))
		return;

	CNetObj_Character *pCharacter = static_cast<CNetObj_Character *>(Server()->SnapNewItem(NETOBJTYPE_CHARACTER, ID, sizeof(CNetObj_Character)));
	if(!pCharacter) return;

	// write core
	if(!m_ReckoningTick)
	{
		// no dead reckoning when paused because the client doesn't know
		// how far to perform the reckoning
		pCharacter->m_Tick = 0;
		m_Core.Write(pCharacter);
	}
	else
	{
		pCharacter->m_Tick = m_ReckoningTick;
		m_SendCore.Write(pCharacter);
	}

	if(pCharacter->m_HookedPlayer != -1)
	{
		if(SnappingClient >= 0 && !Server()->Translate(pCharacter->m_HookedPlayer, SnappingClient))
			pCharacter->m_HookedPlayer = -1;
	}

	// emote
	if(m_EmoteStop < Server()->Tick())
	{
		m_EmoteType = EMOTE_NORMAL;
		m_EmoteStop = -1;
	}
	pCharacter->m_Emote = m_EmoteType;

	if(pCharacter->m_Emote == EMOTE_NORMAL)
	{
		if(5 * Server()->TickSpeed() - ((Server()->Tick() - m_LastAction) % (5 * Server()->TickSpeed())) < 5)
			pCharacter->m_Emote = EMOTE_BLINK;
	}

	pCharacter->m_AttackTick = m_AttackTick;
	pCharacter->m_Direction = m_Input.m_Direction;
	pCharacter->m_Weapon = m_ActiveWeapon;
	pCharacter->m_AmmoCount = 0;
	pCharacter->m_Health = 0;
	pCharacter->m_Armor = 0;
}

// ─── ProcessBot (MRPG exact) ─────────────────────────────────────

void CCharacterBotAI::ProcessBot()
{
	if(!m_pAI) return;

	if(!m_pAI->GetTarget()->IsEmpty())
		m_pAI->GetTarget()->Tick();

	m_pAI->Process();

	if(m_Input.m_Direction)
		m_PrevDirection = m_Input.m_Direction;

	SelectWeaponAtRandomInterval();
	SelectEmoteAtRandomInterval();
	HandleWeapons();
}

// ─── Move (MRPG exact, adapted for TDA API) ───────────────────────

void CCharacterBotAI::Move()
{
	if(!m_BotTargetPos.has_value())
		return;

	vec2 TargetPos = m_BotTargetPos.value();

	// Always clear old hook intent; the MRPG wall-hook section below manages hooks from scratch.
	m_WantHook = false;

	// Always aim toward the target (for both direct chase and path-based movement)
	SetAim(TargetPos - m_Pos);

	// ── Async pathfinding ──
	if(Server()->Tick() % 5 == 0 && !m_BotPathHandle.IsValid())
	{
		CMMOManager *pMMOMgr = GameServer()->TW()->GetMMOManager();
		if(pMMOMgr && pMMOMgr->m_pPathFinder)
			pMMOMgr->m_pPathFinder->RequestPath(m_BotPathHandle, m_Core.m_Pos, TargetPos);
	}

	// ── Try to get path results ──
	m_BotPathHandle.TryGetPath();
	if(m_BotPathHandle.vPath.empty())
	{
		// No path — direct chase (no wall-hooking, prevents getting stuck).
		vec2 AimDir = TargetPos - m_Core.m_Pos;

		// Direct chase movement
		if(AimDir.x < -16.f)
			m_Input.m_Direction = -1;
		else if(AimDir.x > 16.f)
			m_Input.m_Direction = 1;
		else
			m_Input.m_Direction = 0;

		if(AimDir.y < -64.f && IsGrounded())
			m_Input.m_Jump = 1;

		return;
	}

	// ── Waypoint processing ──
	int ActiveWayPoints = 0;
	vec2 WayPos = TargetPos;
	for(int i = 0; i < (int)m_BotPathHandle.vPath.size() && i < 30; i++)
	{
		if(GameServer()->Collision()->IntersectLine(m_BotPathHandle.vPath[i], m_Pos, 0, 0))
			break;
		ActiveWayPoints = i;
	}
	if(ActiveWayPoints > 0)
		WayPos = m_BotPathHandle.vPath[ActiveWayPoints];

	// Set aim to target
	SetAim(TargetPos - m_Pos);

	// Direction to waypoint
	float DistToWaypoint = distance(WayPos, m_Core.m_Pos);
	vec2 DirectionToWaypoint = DistToWaypoint > 0.1f ? normalize(WayPos - m_Core.m_Pos) : vec2(0.f, 0.f);

	// Movement direction
	int PathDirection = (ActiveWayPoints > 3) ?
		(DirectionToWaypoint.x < -0.1f ? -1 : (DirectionToWaypoint.x > 0.1f ? 1 : 0)) :
		m_PrevDirection;

	// Optimal distance / strafing
	bool HasActiveTarget = (!m_pAI->GetTarget()->IsEmpty() &&
						  m_pAI->GetTarget()->GetType() == ETargetType::Active &&
						  !m_pAI->GetTarget()->IsCollided());
	int DistanceDirection = 0;

	if(HasActiveTarget)
	{
		float OptimalDistance = 64.f;
		switch(m_ActiveWeapon)
		{
			case WEAPON_GUN:     OptimalDistance = 300.f; break;
			case WEAPON_SHOTGUN: OptimalDistance = 400.f; break;
			case WEAPON_GRENADE: OptimalDistance = 500.f; break;
			case WEAPON_LASER:   OptimalDistance = 600.f; break;
		}

		float DistanceToTarget = distance(GetPos(), TargetPos);
		float DistanceDifference = DistanceToTarget - OptimalDistance;
		bool LineOfSightClear = !GameServer()->Collision()->IntersectLine(m_Core.m_Pos, TargetPos, 0, 0);

		int CurrentTick = Server()->Tick();
		if(CurrentTick - m_LastStrafeChangeTick > Server()->TickSpeed() * (1 + (rand() % 2)))
		{
			m_StrafeDirection = (rand() % 2) ? 1 : -1;
			m_LastStrafeChangeTick = CurrentTick;
		}

		if(LineOfSightClear && fabs(DistanceDifference) < 80.f)
		{
			DistanceDirection = m_StrafeDirection;
		}
		else if(LineOfSightClear && fabs(DistanceDifference) >= 80.f)
		{
			vec2 DirToTarget = normalize(TargetPos - m_Core.m_Pos);
			DistanceDirection = (DistanceDifference > 0) ?
				(DirToTarget.x > 0 ? 1 : -1) :
				(DirToTarget.x > 0 ? -1 : 1);
		}
		else
		{
			vec2 DirToTarget = normalize(TargetPos - m_Core.m_Pos);
			DistanceDirection = (DirToTarget.x > 0) ? 1 : -1;
		}
	}

	// Set final direction
	m_Input.m_Direction = (DistanceDirection != 0) ? DistanceDirection : PathDirection;

	// ── Jump ──
	const bool IsOnGround = IsGrounded();
	if((IsOnGround && DirectionToWaypoint.y < -0.5f) ||
		(!IsOnGround && DirectionToWaypoint.y < -0.5f && m_Core.m_Vel.y > 0))
	{
		m_Input.m_Jump = 1;
	}

	// Wall jump
	vec2 WallIntersect;
	if(GameServer()->Collision()->IntersectLine(m_Pos, m_Pos + vec2(m_Input.m_Direction * 32.f, 0), &WallIntersect, 0))
	{
		float CheckHeight = IsOnGround ? -210.f : -125.f;
		if(!GameServer()->Collision()->IntersectLine(WallIntersect, WallIntersect + vec2(0, CheckHeight), 0, 0))
			m_Input.m_Jump = 1;
	}

	// Don't jump if waypoint is below or path is short
	if(m_Input.m_Jump == 1 && (DirectionToWaypoint.y >= 0 || ActiveWayPoints < 3))
		m_Input.m_Jump = 0;

	// Jump over characters ahead
	vec2 IntersectPos;
	CCharacter *pChar = GameWorld()->IntersectCharacter(m_Core.m_Pos,
		m_Core.m_Pos + vec2(m_Input.m_Direction * 64.f, 0), 16.f, IntersectPos, this);
	if(pChar && pChar->GetPlayer() && !pChar->GetPlayer()->m_pMMOBotData)
		m_Input.m_Jump = 1;

	// ── Wall-hooking (MRPG exact) ──
	if(ActiveWayPoints > 2 && !m_WantHook &&
		(DirectionToWaypoint.x != 0 || DirectionToWaypoint.y != 0) && !pChar)
	{
		if(m_Core.m_HookState == HOOK_GRABBED && m_Core.m_HookedPlayer == -1)
		{
			vec2 HookVel = normalize(m_Core.m_HookPos - GetPos()) * GameServer()->Tuning()->m_HookDragAccel;
			if(HookVel.y > 0) HookVel.y *= 0.3f;
			if((HookVel.x < 0 && m_Input.m_Direction < 0) || (HookVel.x > 0 && m_Input.m_Direction > 0))
				HookVel.x *= 0.95f;
			else
				HookVel.x *= 0.75f;
			vec2 Target = vec2(m_Input.m_TargetX, m_Input.m_TargetY);
			float ps = dot(Target, HookVel);
			if(ps > 0 || (Target.y < 0 && m_Core.m_Vel.y > 0.f && m_Core.m_HookTick < Server()->TickSpeed() + Server()->TickSpeed() / 2))
				m_WantHook = true;
			if(m_Core.m_HookTick > 4 * Server()->TickSpeed() || length(m_Core.m_HookPos - GetPos()) < 20.0f)
				m_WantHook = false;
		}
		else if(m_Core.m_HookState == HOOK_FLYING)
			m_WantHook = true;
		else if(!m_WantHook && m_Core.m_HookState == HOOK_IDLE && rand() % 3 == 0)
		{
			int NumDir = 32;
			vec2 HookDir(0.f, 0.f);
			float MaxForce = 0;
			for(int i = 0; i < NumDir; i++)
			{
				float a = 2 * i * pi / NumDir;
				vec2 dir = direction(a);
				vec2 Pos = GetPos() + dir * GameServer()->Tuning()->m_HookLength;

				if((GameServer()->Collision()->IntersectLine(GetPos(), Pos, &Pos, 0) & (CCollision::COLFLAG_SOLID | CCollision::COLFLAG_UNHOOKABLE)) == CCollision::COLFLAG_SOLID)
				{
					vec2 HookVel = dir * GameServer()->Tuning()->m_HookDragAccel;
					if(HookVel.y > 0) HookVel.y *= 0.3f;
					if((HookVel.x < 0 && m_Input.m_Direction < 0) || (HookVel.x > 0 && m_Input.m_Direction > 0))
						HookVel.x *= 0.95f;
					else
						HookVel.x *= 0.75f;

					HookVel += vec2(0, 1) * GameServer()->Tuning()->m_Gravity;

					float ps = dot(DirectionToWaypoint, HookVel);
					if(ps > MaxForce)
					{
						if(GameWorld()->IntersectCharacter(GetPos(), Pos, 16.f, IntersectPos, this))
							continue;
						MaxForce = ps;
						HookDir = Pos - GetPos();
					}
				}
			}

			if(length(HookDir) > 32.f)
			{
				SetAim(HookDir);
				m_WantHook = true;
				// Fire wall hook directly
				{
					vec2 WallDir = normalize(HookDir);
					m_Core.m_HookState = HOOK_FLYING;
					m_Core.m_HookPos = GetPos() + WallDir * CCharacterCore::PHYS_SIZE * 1.5f;
					m_Core.m_HookDir = WallDir;
					m_Core.m_HookedPlayer = -1;
					m_Core.m_HookTick = 0;
				}
			}
		}
	}

	// ── Stuck detection ──
	if(m_Pos.x != m_PrevPos.x)
	{
		m_MoveTick = Server()->Tick();
	}
	else if(Server()->Tick() - m_MoveTick > Server()->TickSpeed() / 2)
	{
		m_Input.m_Direction = -m_Input.m_Direction;
		m_Input.m_Jump = 1;
		m_MoveTick = Server()->Tick();
	}
}

// ─── Fire ────────────────────────────────────────────────────────

void CCharacterBotAI::Fire()
{
	if(!m_pAI || m_pAI->GetTarget()->IsEmpty() || m_pAI->GetTarget()->IsCollided())
		return;

	// Don't fire if hook is about to launch or reloading
	if(m_WantHook || m_ReloadTimer != 0)
		return;

	m_Input.m_Fire++;
	m_LatestInput.m_Fire++;
}

// ─── SetAim ──────────────────────────────────────────────────────

void CCharacterBotAI::SetAim(vec2 Dir)
{
	m_Input.m_TargetX = (int)Dir.x;
	m_Input.m_TargetY = (int)Dir.y;
	m_LatestInput.m_TargetX = (int)Dir.x;
	m_LatestInput.m_TargetY = (int)Dir.y;

	// Also update m_LatestInput Target for FireWeapon prediction
}

// ─── SelectWeaponAtRandomInterval ────────────────────────────────

void CCharacterBotAI::SelectWeaponAtRandomInterval()
{
	if(m_ForcedActiveWeapon.has_value())
	{
		m_ActiveWeapon = clamp(m_ForcedActiveWeapon.value(), (int)WEAPON_HAMMER, (int)WEAPON_LASER);
		return;
	}

	if(--m_IntervalChangeWeapon <= 0)
	{
		m_IntervalChangeWeapon = 25 + rand() % 100;

		int AvailableWeapons[WEAPON_LASER + 1] {};
		int WeaponCount = 0;
		for(int i = WEAPON_HAMMER; i <= WEAPON_LASER; i++)
			if(i != m_ActiveWeapon && m_aWeapons[i].m_Got)
				AvailableWeapons[WeaponCount++] = i;

		if(WeaponCount > 0)
			m_ActiveWeapon = AvailableWeapons[rand() % WeaponCount];
	}
}

// ─── SelectEmoteAtRandomInterval ─────────────────────────────────

void CCharacterBotAI::SelectEmoteAtRandomInterval()
{
	const int emoteInterval = Server()->TickSpeed() * 3 + rand() % 10;
	if(Server()->Tick() % emoteInterval == 0)
		SetEmote(EMOTE_BLINK, 1 + rand() % 2);
}

// ─── IsAllowedPVP ─────────────────────────────────────────────────

bool CCharacterBotAI::IsAllowedPVP(int FromID)
{
	CGameContext *pGS = GameServer();
	CPlayer *pFrom = (FromID >= 0 && FromID < MAX_CLIENTS) ? pGS->m_apPlayers[FromID] : nullptr;
	if(!pFrom || FromID == GetCID()) return false;
	if(!pFrom->GetCharacter()) return false;

	return m_pAI && m_pAI->CanDamage(pFrom);
}

// ─── GiveWeapon ──────────────────────────────────────────────────

bool CCharacterBotAI::GiveWeapon(int Weapon, int GiveAmmo)
{
	return CCharacter::GiveWeapon(Weapon, GiveAmmo);
}

void CCharacterBotAI::SetForcedWeapon(int WeaponID)
{
	if(WeaponID >= WEAPON_HAMMER && WeaponID <= WEAPON_NINJA)
		m_ForcedActiveWeapon = WeaponID;
}

void CCharacterBotAI::ClearForcedWeapon()
{
	m_ForcedActiveWeapon.reset();
}
