/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/shared/config.h>

#include <game/server/gamecontext.h>
#include <game/server/data_center.h>
#include <game/server/gamecontroller.h>
#include <game/server/worldmodes/defence.h>
#include <game/server/item_system.h>
#include <game/server/player.h>
#include <game/server/interaction_sound.h>
#include <generated/server_data.h>

#include <engine/shared/config.h>

#include <game/server/core/components/content/content_types.h>
#include <game/server/core/components/content/effect_registry.h>
#include <game/server/core/components/content/status_manager.h>
#include <game/server/core/components/mmo/mmo_manager.h>
#include <game/server/core/components/mmo/mmo_item.h>
#include <game/server/core/components/mmo/mmo_types.h>
#include <game/server/core/components/mmo/mmo_world_boss.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/core/components/tunes/tune_zone_manager.h>
#include <game/server/core/components/skills/skill_manager.h>

#include "character.h"
#include "rpg_fishing.h"
#include <game/server/account.h>
#include <game/server/sql_pool.h>
#include <game/server/sql_query.h>
#include <game/server/entities/vehicle/vehicle_util.h>
#include <mysql.h>
/*
#include "laser.h"
*/
#include "character_bot_ai.h"
#include "projectile.h"

// Helper — spider boss core check only applies in defence mode
static inline bool CheckSpiderBossCore(CGameContext *pGS, CCharacter *pChr)
{
	auto *pCtrl = dynamic_cast<CGameControllerDefence *>(pGS->m_pController);
	return pCtrl && pCtrl->IsSpiderBossCore(pChr);
}

// input count
struct CInputCount
{
	int m_Presses;
	int m_Releases;
};

CInputCount CountInput(int Prev, int Cur)
{
	CInputCount c = {0, 0};
	Prev &= INPUT_STATE_MASK;
	Cur &= INPUT_STATE_MASK;
	int i = Prev;

	while(i != Cur)
	{
		i = (i + 1) & INPUT_STATE_MASK;
		if(i & 1)
			c.m_Presses++;
		else
			c.m_Releases++;
	}

	return c;
}

MACRO_ALLOC_POOL_ID_IMPL(CCharacter, MAX_CLIENTS)

// Character, "physical" player's part
CCharacter::CCharacter(CGameWorld *pWorld) : CHitableEntity(pWorld, CGameWorld::ENTTYPE_CHARACTER, 0, vec2(0, 0), ms_PhysSize)
{
	m_Health = 0;
	m_Armor = 0;
	m_FreezeTime = 0;
	m_FreezeTick = 0;
	m_FreezeAllowHoldFire = false;
	m_FreezeWeaponSwitch = false;
	m_TriggeredEvents = 0;
	m_OnVehicle = false;
	m_VehicleSeat = VEHICLE_SEAT_NONE;
	m_VehicleDismountTick = 0;
}

void CCharacter::Reset()
{
	Destroy();
}

bool CCharacter::Spawn(CPlayer *pPlayer, vec2 Pos)
{
	m_EmoteStop = -1;
	m_LastAction = -1;
	m_LastNoAmmoSound = -1;
	m_ActiveWeapon = WEAPON_HAMMER;
	m_LastWeapon = WEAPON_HAMMER;
	m_QueuedWeapon = -1;
	m_aCategoryLastWeapon[WEAPONCAT_MELEE] = WEAPON_HAMMER;
	m_aCategoryLastWeapon[WEAPONCAT_RANGED] = WEAPON_GUN;
	m_ActiveCategory = WEAPONCAT_MELEE;
	m_ActiveSkillSlot = 0;
	m_ActiveMeleeLoadoutIdx = 0;
	m_ActiveRangedLoadoutIdx = 0;
	m_ActiveWeaponItemID = -1;

	m_pPlayer = pPlayer;
	m_Pos = Pos;

	m_Core.Reset();
	m_Core.Init(&GameWorld()->m_Core, GameServer()->Collision());
	m_Core.m_Pos = m_Pos;
	GameWorld()->m_Core.m_apCharacters[m_pPlayer->GetCID()] = &m_Core;

	m_ReckoningTick = 0;
	mem_zero(&m_SendCore, sizeof(m_SendCore));
	mem_zero(&m_ReckoningCore, sizeof(m_ReckoningCore));

	GameWorld()->InsertEntity(this);
	m_Alive = true;

	m_InMining = false;
	m_MiningTick = -1;
	m_pFishingRod = nullptr;
	m_AutoFishingEnabled = false;
	m_LockedCK = false;
	m_LockPos = vec2(0.0f, 0.0f);
	m_CardElectronTicks = 0;
	// Apply MMO level-based HP scaling for real players (not bots/dummies)
	if(m_pPlayer && !m_pPlayer->IsDummy() && m_pPlayer->m_MMOLevel > 0)
		m_MaxHealth = m_pPlayer->GetMaxHealth();
	else
		m_MaxHealth = GameServer()->Config()->m_SvPlayerMaxHealth;
	m_RetaliationExpireTick = 0;
	m_RetaliationStacks = 0;

	// Reset skill runtime states
	m_RenewTicks = 0;
	m_RenewAmount = 0;
	m_IronWillTicks = 0;
	m_ShadowTicks = 0;
	m_ShadowNextCrit = false;
	m_IsInvisible = false;
	m_FreezeTime = 0;
	m_FreezeTick = 0;
	m_FreezeAllowHoldFire = false;
	m_FreezeWeaponSwitch = false;

	for(int i = 0; i < NUM_WEAPONS; i++)
		m_aWeapons[i].m_Valid = true;

	m_NumInputs = 0;
	mem_zero(&m_Input, sizeof(m_Input));
	mem_zero(&m_LatestInput, sizeof(m_LatestInput));
	mem_zero(&m_LatestPrevInput, sizeof(m_LatestPrevInput));
	m_Input.m_TargetY = -1;
	m_LatestInput.m_TargetY = -1;
	m_LatestPrevInput.m_TargetY = -1;

	// Initialize MRPG extensions
	m_Mana = 0;
	m_WaterAir = 0;
	m_aZoneName[0] = '\0';
	m_SafeTickFlags = 0;
	m_TuneZoneOverride = 0;
	m_MoveRestrictions = 0;
	m_PrevPos = vec2(0, 0);
	m_pTilesHandler = new CTileHandler(GameServer()->Collision(), this);

	GameServer()->m_pController->OnCharacterSpawn(this);

	if(m_pPlayer && !m_pPlayer->IsDummy() && m_pPlayer->GetAccountId() > 0)
	{
		m_Mana = m_pPlayer->GetMaxMana();
		if(TWorldController *pCore = GameServer()->Core())
		{
			if(CMMOManager *pMMO = pCore->GetMMOManager())
			{
				m_pPlayer->MigrateWeaponLoadoutFromEquippedSlots();
				pMMO->ApplyEquippedWeapon(m_pPlayer);
			}
		}
	}

	if(m_pPlayer && !m_pPlayer->IsDummy())
		GameServer()->MarkUpdatedBroadcast(m_pPlayer->GetCID());

	return true;
}

void CCharacter::Destroy()
{
	if(m_pFishingRod)
	{
		delete m_pFishingRod;
		m_pFishingRod = nullptr;
	}
	GameWorld()->m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	m_Alive = false;
	delete m_pTilesHandler;
	m_pTilesHandler = nullptr;
}

// Classify weapon as melee (hammer/sword) or ranged (gun/shotgun/grenade/laser)
int CCharacter::WeaponCategoryForWeapon(int Weapon)
{
	if(Weapon == WEAPON_HAMMER)
		return WEAPONCAT_MELEE;
	// All other standard weapons are ranged
	return WEAPONCAT_RANGED;
}

void CCharacter::SetWeapon(int W)
{
	if(W == m_ActiveWeapon)
		return;

	m_LastWeapon = m_ActiveWeapon;
	m_QueuedWeapon = -1;
	m_ActiveWeapon = W;
	m_ActiveCategory = WeaponCategoryForWeapon(W);
	m_aCategoryLastWeapon[m_ActiveCategory] = W;
	GameWorld()->CreateSound(m_Pos, SOUND_WEAPON_SWITCH);

	if(m_ActiveWeapon < 0 || m_ActiveWeapon >= NUM_WEAPONS)
		m_ActiveWeapon = 0;
	m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = -1;

	if(m_pPlayer && !m_pPlayer->IsDummy())
		GameServer()->MarkUpdatedBroadcast(m_pPlayer->GetCID());
}

bool CCharacter::IsGrounded()
{
	if(GameServer()->Collision()->CheckPoint(m_Pos.x + GetProximityRadius() / 2, m_Pos.y + GetProximityRadius() / 2 + 5))
		return true;
	if(GameServer()->Collision()->CheckPoint(m_Pos.x - GetProximityRadius() / 2, m_Pos.y + GetProximityRadius() / 2 + 5))
		return true;
	return false;
}

void CCharacter::HandleNinja()
{
	if(m_ActiveWeapon != WEAPON_NINJA)
		return;

	if((Server()->Tick() - m_Ninja.m_ActivationTick) > (g_pData->m_Weapons.m_Ninja.m_Duration * Server()->TickSpeed() / 1000))
	{
		// time's up, return
		m_aWeapons[WEAPON_NINJA].m_Got = false;
		m_ActiveWeapon = m_LastWeapon;

		// reset velocity and current move
		if(m_Ninja.m_CurrentMoveTime > 0)
			m_Core.m_Vel = m_Ninja.m_ActivationDir * m_Ninja.m_OldVelAmount;
		m_Ninja.m_CurrentMoveTime = -1;

		SetWeapon(m_ActiveWeapon);
		return;
	}

	// force ninja Weapon
	SetWeapon(WEAPON_NINJA);

	m_Ninja.m_CurrentMoveTime--;

	if(m_Ninja.m_CurrentMoveTime == 0)
	{
		// reset velocity
		m_Core.m_Vel = m_Ninja.m_ActivationDir * m_Ninja.m_OldVelAmount;
	}
	else if(m_Ninja.m_CurrentMoveTime > 0)
	{
		// Set velocity
		m_Core.m_Vel = m_Ninja.m_ActivationDir * g_pData->m_Weapons.m_Ninja.m_Velocity;
		vec2 OldPos = m_Pos;
		GameServer()->Collision()->MoveBox(&m_Core.m_Pos, &m_Core.m_Vel, vec2(GetProximityRadius(), GetProximityRadius()), 0.f);

		// reset velocity so the client doesn't predict stuff
		m_Core.m_Vel = vec2(0.f, 0.f);

		// check if we hit anything along the way
		const float Radius = GetProximityRadius() * 2.0f;
		const vec2 Center = OldPos + (m_Pos - OldPos) * 0.5f;
		array<CEntity *> lpEnts;
		lpEnts.hint_size(8);
		const int Num = GameWorld()->FindFlagEntities(Center, Radius, lpEnts, CGameWorld::ENTFLAG_HITABLE);

		for(int i = 0; i < Num; ++i)
		{
			if(lpEnts[i] == this)
				continue;

			// make sure we haven't hit this object before
			bool AlreadyHit = false;
			for(int j = 0; j < m_lpHitObjects.size(); j++)
			{
				if(m_lpHitObjects[j] == lpEnts[i])
				{
					AlreadyHit = true;
					break;
				}
			}
			if(AlreadyHit)
				continue;

			// check so we are sufficiently close
			if(distance(lpEnts[i]->GetPos(), m_Pos) > Radius)
				continue;

			// Hit a player, give him damage and stuffs...
			GameWorld()->CreateSound(lpEnts[i]->GetPos(), SOUND_NINJA_HIT);
			m_lpHitObjects.add(lpEnts[i]);

			// set his velocity to fast upward (for now)
			static_cast<CHitableEntity *>(lpEnts[i])->TakeHit(vec2(0, -10.0f), m_Ninja.m_ActivationDir * -1, g_pData->m_Weapons.m_Ninja.m_pBase->m_Damage, this, WEAPON_NINJA);
		}
	}
}

void CCharacter::DoWeaponSwitch()
{
	// make sure we can switch
	if(m_ReloadTimer != 0 || m_QueuedWeapon == -1 || m_aWeapons[WEAPON_NINJA].m_Got)
		return;

	// switch Weapon
	SetWeapon(m_QueuedWeapon);
}

bool CCharacter::TryActivateLoadoutIdx(int Category, int LoadoutIdx)
{
	if(!m_pPlayer || LoadoutIdx < 0 || LoadoutIdx >= CPlayer::MMO_WEAPON_LOADOUT_SIZE)
		return false;

	int ItemID = -1;
	int EngineWpn = -1;

	if(Category == WEAPONCAT_MELEE)
	{
		ItemID = m_pPlayer->m_aMeleeLoadout[LoadoutIdx];
		if(ItemID <= 0)
			return false;
		EngineWpn = WEAPON_HAMMER;
	}
	else if(Category == WEAPONCAT_RANGED)
	{
		ItemID = m_pPlayer->m_aRangedLoadout[LoadoutIdx];
		if(ItemID <= 0)
			return false;
		const CMMOItemDescription *pDef = CMMOItemDescription::Get(ItemID);
		if(!pDef)
			return false;
		EngineWpn = MMOItemTypeToWeapon(pDef->GetType());
		if(EngineWpn < 0 || EngineWpn >= NUM_WEAPONS)
			return false;
	}
	else
		return false;

	if(!m_aWeapons[EngineWpn].m_Got || !m_aWeapons[EngineWpn].m_Valid)
		return false;

	m_ActiveCategory = Category;
	m_ActiveWeaponItemID = ItemID;
	if(Category == WEAPONCAT_MELEE)
		m_ActiveMeleeLoadoutIdx = LoadoutIdx;
	else
		m_ActiveRangedLoadoutIdx = LoadoutIdx;

	m_aCategoryLastWeapon[Category] = EngineWpn;
	if(EngineWpn != m_ActiveWeapon)
		m_QueuedWeapon = EngineWpn;

	return true;
}

void CCharacter::CycleLoadoutInCategory(int Direction)
{
	if(!m_pPlayer || Direction == 0)
		return;
	if(m_ActiveCategory != WEAPONCAT_MELEE && m_ActiveCategory != WEAPONCAT_RANGED)
		return;

	int aSlots[CPlayer::MMO_WEAPON_LOADOUT_SIZE];
	int Num = 0;
	for(int i = 0; i < CPlayer::MMO_WEAPON_LOADOUT_SIZE; i++)
	{
		const int ItemID = m_ActiveCategory == WEAPONCAT_MELEE
			? m_pPlayer->m_aMeleeLoadout[i]
			: m_pPlayer->m_aRangedLoadout[i];
		if(ItemID > 0)
			aSlots[Num++] = i;
	}
	if(Num <= 1)
		return;

	const int CurIdx = m_ActiveCategory == WEAPONCAT_MELEE
		? m_ActiveMeleeLoadoutIdx
		: m_ActiveRangedLoadoutIdx;

	int CurPos = 0;
	for(int i = 0; i < Num; i++)
	{
		if(aSlots[i] == CurIdx)
		{
			CurPos = i;
			break;
		}
	}

	const int NextPos = (CurPos + Direction + Num) % Num;
	TryActivateLoadoutIdx(m_ActiveCategory, aSlots[NextPos]);
}

void CCharacter::HandleWeaponSwitch()
{
	// ── Direct slot selection: 1 → melee, 2 → ranged (priority over 3/4/5) ──
	if(m_LatestInput.m_WantedWeapon == 1 || m_LatestInput.m_WantedWeapon == 2)
	{
		const int Category = (m_LatestInput.m_WantedWeapon == 1) ? WEAPONCAT_MELEE : WEAPONCAT_RANGED;
		int LoadoutIdx = Category == WEAPONCAT_MELEE ? m_ActiveMeleeLoadoutIdx : m_ActiveRangedLoadoutIdx;
		if(LoadoutIdx < 0 || LoadoutIdx >= CPlayer::MMO_WEAPON_LOADOUT_SIZE)
			LoadoutIdx = 0;

		if(!TryActivateLoadoutIdx(Category, LoadoutIdx))
		{
			for(int i = 0; i < CPlayer::MMO_WEAPON_LOADOUT_SIZE; i++)
			{
				if(TryActivateLoadoutIdx(Category, i))
					break;
			}
		}

		DoWeaponSwitch();
		if(m_pPlayer && !m_pPlayer->IsDummy())
			GameServer()->FlushBroadcastStats(m_pPlayer->GetCID());
		return;
	}

	// ── Magic bar: 3/4/5 select skill slot (cast on fire) ──
	if(m_LatestInput.m_WantedWeapon >= 3 && m_LatestInput.m_WantedWeapon <= 5)
	{
		if(m_LatestPrevInput.m_WantedWeapon != m_LatestInput.m_WantedWeapon)
		{
			m_ActiveCategory = WEAPONCAT_MAGIC;
			m_ActiveSkillSlot = m_LatestInput.m_WantedWeapon - 3;
			m_QueuedWeapon = -1;
			if(m_pPlayer && !m_pPlayer->IsDummy())
			{
				GameWorld()->CreateSound(m_Pos, SOUND_WEAPON_SWITCH);
				GameServer()->FlushBroadcastStats(m_pPlayer->GetCID());
			}
		}
		return;
	}

	if(m_ActiveCategory == WEAPONCAT_MAGIC)
		return;

	// ── Scroll wheel: cycle within current melee/ranged loadout only ──
	int Next = CountInput(m_LatestPrevInput.m_NextWeapon, m_LatestInput.m_NextWeapon).m_Presses;
	int Prev = CountInput(m_LatestPrevInput.m_PrevWeapon, m_LatestInput.m_PrevWeapon).m_Presses;

	const int PrevItemID = m_ActiveWeaponItemID;

	if(Next > 0 && Next < 128)
		CycleLoadoutInCategory(1);
	else if(Prev > 0 && Prev < 128)
		CycleLoadoutInCategory(-1);
	else
		return;

	const bool LoadoutChanged = m_ActiveWeaponItemID != PrevItemID;

	if(m_QueuedWeapon != -1)
		DoWeaponSwitch();

	if(LoadoutChanged && m_pPlayer && !m_pPlayer->IsDummy())
		GameServer()->FlushBroadcastStats(m_pPlayer->GetCID());
}

void CCharacter::FireWeapon()
{
	if(m_OnVehicle)
		return;

	if(m_ReloadTimer != 0)
		return;

	if(IsFrozen() && !m_FreezeAllowHoldFire)
		return;

	if(m_ActiveCategory != WEAPONCAT_MAGIC)
		DoWeaponSwitch();

	// Magic bar: left click casts slot skill, or wand bolt while skill unavailable
	if(m_ActiveCategory == WEAPONCAT_MAGIC)
	{
		if(!CountInput(m_LatestPrevInput.m_Fire, m_LatestInput.m_Fire).m_Presses)
			return;

		CPlayer *pPl = GetPlayer();
		if(!pPl)
			return;

		CSkillManager *pSM = GameServer()->Core() ? GameServer()->Core()->SkillManager() : nullptr;
		const int SkillId = pPl->m_aSkillSlots[m_ActiveSkillSlot];
		const int TickSpeed = Server()->TickSpeed();

		bool SkillFired = false;
		if(SkillId >= 0 && pSM && pSM->CanUse(pPl, SkillId))
			SkillFired = pSM->Use(pPl, SkillId, false);

		bool WandFired = false;
		if(!SkillFired && pSM)
			WandFired = pSM->CastMagicWand(pPl);

		if(!SkillFired && !WandFired)
			return;

		m_AttackTick = Server()->Tick();
		if(SkillFired)
			m_ReloadTimer = maximum(TickSpeed / 8, 125 * TickSpeed / 1000);
		else
		{
			const int DexVal = pPl->GetStat(AttributeIdentifier::DEX);
			m_ReloadTimer = maximum(TickSpeed / 3, TickSpeed / 2 - DexVal);
		}

		if(m_pPlayer && !m_pPlayer->IsDummy())
			GameServer()->MarkUpdatedBroadcast(m_pPlayer->GetCID());
		return;
	}

	vec2 Direction = normalize(vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY));

	// check if we gonna fire
	bool WillFire = false;
	if(CountInput(m_LatestPrevInput.m_Fire, m_LatestInput.m_Fire).m_Presses)
		WillFire = true;

	if(GameServer()->m_pController->CanCharacterWeaponFullAuto(this, m_ActiveWeapon) && (m_LatestInput.m_Fire & 1) && m_aWeapons[m_ActiveWeapon].m_Ammo)
		WillFire = true;

	if(!WillFire)
		return;

	// check for ammo
	if(!m_aWeapons[m_ActiveWeapon].m_Ammo)
	{
		// 125ms is a magical limit of how fast a human can click
		m_ReloadTimer = 125 * Server()->TickSpeed() / 1000;
		if(m_LastNoAmmoSound + Server()->TickSpeed() <= Server()->Tick())
		{
			GameWorld()->CreateSound(m_Pos, SOUND_WEAPON_NOAMMO);
			m_LastNoAmmoSound = Server()->Tick();
		}
		return;
	}

	if(Config()->m_Debug)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "shot player='%d:%s' team=%d weapon=%d", m_pPlayer->GetCID(), Server()->ClientName(m_pPlayer->GetCID()), m_pPlayer->GetTeam(), m_ActiveWeapon);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	}

	m_AttackTick = Server()->Tick();

	if(m_aWeapons[m_ActiveWeapon].m_Ammo > 0) // -1 == unlimited
		m_aWeapons[m_ActiveWeapon].m_Ammo--;

	m_ReloadTimer = GameServer()->m_pController->OnCharacterFireWeapon(this, Direction, m_ActiveWeapon);
}

void CCharacter::TickVehicleWeapon()
{
	if(!m_OnVehicle || IsFrozen())
		return;

	if(m_ReloadTimer > 0)
	{
		m_ReloadTimer--;
		return;
	}

	if(!(m_Input.m_Fire & 1))
		return;

	vec2 Direction = vec2(m_Input.m_TargetX, m_Input.m_TargetY);
	if(length(Direction) < 0.001f)
		return;
	Direction = normalize(Direction);

	vec2 ProjStartPos = m_Pos + Direction * (float)ms_PhysSize * 0.75f;
	new CProjectile(GameWorld(), WEAPON_GUN, m_pPlayer->GetCID(), ProjStartPos, Direction,
		(int)(Server()->TickSpeed() * GameServer()->Tuning()->m_GunLifetime),
		1, 0, 0, -1, WEAPON_GUN);
	GameWorld()->CreateSound(m_Pos, SOUND_GUN_FIRE);
	m_AttackTick = Server()->Tick();
	m_ReloadTimer = g_pData->m_Weapons.m_aId[WEAPON_GUN].m_Firedelay * Server()->TickSpeed() / 1000;
}

void CCharacter::HandleWeapons()
{
	// ninja
	HandleNinja();

	// check reload timer
	if(m_ReloadTimer)
	{
		m_ReloadTimer--;
		return;
	}

	// MRPG-style: block fire when menu/modal is active
	if(m_pPlayer)
	{
		IInputEvents *pInput = Server()->Input();
		if(pInput && pInput->IsBlockedInputGroup(m_pPlayer->GetCID(), BLOCK_INPUT_FIRE))
		{
			m_ReloadTimer = 10;
			return;
		}
	}

	// fire Weapon, if wanted
	FireWeapon();

	// MMO finite ammo regen (MRPG-style)
	if(m_pPlayer && m_pPlayer->UsesMMOFiniteAmmo() && m_aWeapons[m_ActiveWeapon].m_Ammo >= 0)
	{
		const int MaxAmmo = m_pPlayer->GetMMOMaxAmmo();
		if(m_aWeapons[m_ActiveWeapon].m_Ammo < MaxAmmo)
		{
			const int Speed = maximum(1, 100 * m_pPlayer->GetMMOAmmoRegenPercent() / 100);
			const int AmmoRegenTime = 500 / Speed;
			if(m_ReloadTimer <= 0)
			{
				if(m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart < 0)
				{
					m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = Server()->Tick() +
						(m_ActiveWeapon == WEAPON_GUN ? (Server()->TickSpeed() / 2) : (AmmoRegenTime * Server()->TickSpeed()));
				}

				if(m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart <= Server()->Tick())
				{
					m_aWeapons[m_ActiveWeapon].m_Ammo = minimum(m_aWeapons[m_ActiveWeapon].m_Ammo + 1, MaxAmmo);
					m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = -1;
				}
			}
			else
				m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = -1;
		}
		return;
	}

	// ammo regen
	int AmmoRegenTime = g_pData->m_Weapons.m_aId[m_ActiveWeapon].m_Ammoregentime;
	if(m_ActiveWeapon != WEAPON_HAMMER && m_aWeapons[m_ActiveWeapon].m_Ammo < 10 && !AmmoRegenTime)
	{
		if(CItemHelper *pH = GameServer()->ItemHelper())
		{
			const char *pSx = m_pPlayer->GetExtraForItem(m_pPlayer->GetHolding(ITYPE_SWORD));
			if(Config()->m_SvContentFramework && GameServer()->Core() && GameServer()->Core()->EffectRegistry())
			{
				CEffectContext Ctx = {};
				Ctx.m_pPlayer = m_pPlayer;
				Ctx.m_pExtraJson = pSx;
				GameServer()->Core()->EffectRegistry()->Apply(TRIGGER_TICK, Ctx);

				// Aggregate ammo regen from armor items (helm/chest/legs)
				const int ArmorTypes[] = {ITYPE_HELMET, ITYPE_CHEST, ITYPE_LEGS};
				for(int a = 0; a < 3; a++)
				{
					const int ArmorId = m_pPlayer->GetHolding(ArmorTypes[a]);
					if(ArmorId <= 0)
						continue;
					const char *pArmorExtra = m_pPlayer->GetExtraForItem(ArmorId);
					if(!pArmorExtra || !pArmorExtra[0])
						continue;
					CEffectContext ArmorCtx = {};
					ArmorCtx.m_pPlayer = m_pPlayer;
					ArmorCtx.m_pExtraJson = pArmorExtra;
					GameServer()->Core()->EffectRegistry()->Apply(TRIGGER_TICK, ArmorCtx);
					if(ArmorCtx.m_AmmoRegenTime > 0 && Ctx.m_AmmoRegenTime <= 0)
						Ctx.m_AmmoRegenTime = ArmorCtx.m_AmmoRegenTime;
					else if(ArmorCtx.m_AmmoRegenTime > 0)
						Ctx.m_AmmoRegenTime = minimum(Ctx.m_AmmoRegenTime, ArmorCtx.m_AmmoRegenTime);
				}

				if(Ctx.m_AmmoRegenTime > 0)
					AmmoRegenTime = Ctx.m_AmmoRegenTime;
			}
			if(Config()->m_SvContentLegacyCards || !Config()->m_SvContentFramework)
			{
				int NumCard = pH->GetCard(pSx, ITEM_CARD_QUICKLY_LOADING_ID);
				// Aggregate quickly-loading from armor items
				const int ArmorTypes[] = {ITYPE_HELMET, ITYPE_CHEST, ITYPE_LEGS};
				for(int a = 0; a < 3; a++)
				{
					const int ArmorId = m_pPlayer->GetHolding(ArmorTypes[a]);
					if(ArmorId <= 0)
						continue;
					const char *pArmorExtra = m_pPlayer->GetExtraForItem(ArmorId);
					NumCard += pH->GetCard(pArmorExtra, ITEM_CARD_QUICKLY_LOADING_ID);
				}
				if(NumCard)
				{
					const int MaxPlace = pH->GetMaxPlace(ITEM_CARD_QUICKLY_LOADING_ID);
					AmmoRegenTime = 1 + MaxPlace * 500 - NumCard * 500;
					AmmoRegenTime = clamp(AmmoRegenTime, 0, 1 + MaxPlace * 500);
				}
			}
		}
	}
	else if(AmmoRegenTime > 0)
	{
		if(CItemHelper *pH = GameServer()->ItemHelper())
		{
			const char *pSx = m_pPlayer->GetExtraForItem(m_pPlayer->GetHolding(ITYPE_SWORD));
			const char *pPx = m_pPlayer->GetExtraForItem(m_pPlayer->GetHolding(ITYPE_PICKAXE));
			int QL = pH->GetCard(pSx, ITEM_CARD_QUICKLY_LOADING_ID);
			QL += (pH->GetCard(pPx, ITEM_CARD_QUICKLY_LOADING_ID) + 1) / 2;
			// Aggregate ammo regen speed from armor items
			const int ArmorTypes[] = {ITYPE_HELMET, ITYPE_CHEST, ITYPE_LEGS};
			for(int a = 0; a < 3; a++)
			{
				const int ArmorId = m_pPlayer->GetHolding(ArmorTypes[a]);
				if(ArmorId <= 0)
					continue;
				const char *pArmorExtra = m_pPlayer->GetExtraForItem(ArmorId);
				QL += pH->GetCard(pArmorExtra, ITEM_CARD_QUICKLY_LOADING_ID);
			}
			if(QL > 0)
				AmmoRegenTime = maximum(1, AmmoRegenTime - QL * (Server()->TickSpeed() / 25));
			int QF = pH->GetCard(pSx, ITEM_CARD_QUICKLY_FIRE_ID);
			// Aggregate quickly-fire from armor items
			for(int a = 0; a < 3; a++)
			{
				const int ArmorId = m_pPlayer->GetHolding(ArmorTypes[a]);
				if(ArmorId <= 0)
					continue;
				const char *pArmorExtra = m_pPlayer->GetExtraForItem(ArmorId);
				QF += pH->GetCard(pArmorExtra, ITEM_CARD_QUICKLY_FIRE_ID);
			}
			if(QF > 0 && QL > 0)
				AmmoRegenTime = maximum(1, AmmoRegenTime - Server()->TickSpeed() / 40);
		}
	}
	if(AmmoRegenTime && m_aWeapons[m_ActiveWeapon].m_Ammo >= 0)
	{
		// If equipped and not active, regen ammo?
		if(m_ReloadTimer <= 0)
		{
			if(m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart < 0)
				m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = Server()->Tick();

			if((Server()->Tick() - m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart) >= AmmoRegenTime * Server()->TickSpeed() / 1000)
			{
				// Add some ammo
				m_aWeapons[m_ActiveWeapon].m_Ammo = minimum(m_aWeapons[m_ActiveWeapon].m_Ammo + 1,
					g_pData->m_Weapons.m_aId[m_ActiveWeapon].m_Maxammo);
				m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = -1;
			}
		}
		else
		{
			m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = -1;
		}
	}

	return;
}

bool CCharacter::GiveWeapon(int Weapon, int Ammo)
{
	bool IsNew = !m_aWeapons[Weapon].m_Got;
	if(m_aWeapons[Weapon].m_Ammo < g_pData->m_Weapons.m_aId[Weapon].m_Maxammo || !m_aWeapons[Weapon].m_Got)
	{
		m_aWeapons[Weapon].m_Got = true;
		m_aWeapons[Weapon].m_Ammo = minimum(g_pData->m_Weapons.m_aId[Weapon].m_Maxammo, Ammo);
		// Track this weapon in its category (first weapon of category becomes default)
		if(IsNew)
		{
			int Cat = WeaponCategoryForWeapon(Weapon);
			int CurrentDefault = m_aCategoryLastWeapon[Cat];
			// If the current default isn't actually acquired yet, replace it
			if(!m_aWeapons[CurrentDefault].m_Got || !m_aWeapons[CurrentDefault].m_Valid)
				m_aCategoryLastWeapon[Cat] = Weapon;
		}
		return true;
	}
	return false;
}

void CCharacter::SyncMMOWeaponAmmo(int MaxAmmo)
{
	MaxAmmo = maximum(1, MaxAmmo);
	for(int W = WEAPON_GUN; W <= WEAPON_LASER; W++)
	{
		if(!m_aWeapons[W].m_Got)
			continue;
		if(m_aWeapons[W].m_Ammo < 0)
			m_aWeapons[W].m_Ammo = MaxAmmo;
		else
			m_aWeapons[W].m_Ammo = clamp(m_aWeapons[W].m_Ammo, 0, MaxAmmo);
	}
}

void CCharacter::AddWeaponAmmo(int Weapon, int Bonus)
{
	if(Weapon < 0 || Weapon >= NUM_WEAPONS || Bonus <= 0)
		return;
	if(!m_aWeapons[Weapon].m_Got || m_aWeapons[Weapon].m_Ammo < 0)
		return;
	const int Cap = g_pData->m_Weapons.m_aId[Weapon].m_Maxammo + Bonus;
	m_aWeapons[Weapon].m_Ammo = minimum(Cap, m_aWeapons[Weapon].m_Ammo + Bonus);
}

void CCharacter::GiveNinja()
{
	m_Ninja.m_ActivationTick = Server()->Tick();
	m_aWeapons[WEAPON_NINJA].m_Got = true;
	m_aWeapons[WEAPON_NINJA].m_Ammo = -1;
	if(m_ActiveWeapon != WEAPON_NINJA)
		m_LastWeapon = m_ActiveWeapon;
	m_ActiveWeapon = WEAPON_NINJA;

	GameWorld()->CreateSound(m_Pos, SOUND_PICKUP_NINJA);
}

void CCharacter::SetEmote(int Emote, int Tick)
{
	m_EmoteType = Emote;
	m_EmoteStop = Tick;
}

bool CCharacter::Freeze(float Seconds, bool BlockHoldFire)
{
	const int Ticks = round_to_int(Seconds * Server()->TickSpeed());
	m_FreezeAllowHoldFire = !BlockHoldFire;
	if(Seconds <= 0.0f || m_FreezeTime > Ticks)
		return false;
	if(m_FreezeTime == 0 || m_FreezeTick < Server()->Tick() - Server()->TickSpeed())
	{
		m_FreezeTime = Ticks;
		m_FreezeTick = Server()->Tick();
		return true;
	}
	return false;
}

bool CCharacter::IsFrozen() const
{
	return m_FreezeTime > 0;
}

bool CCharacter::UnFreeze()
{
	if(m_FreezeTime > 0)
	{
		m_FreezeTime = 0;
		m_FreezeTick = 0;
		return true;
	}
	return false;
}

bool CCharacter::ReduceFreeze(float Seconds)
{
	if(m_FreezeTime <= 0)
		return false;
	m_FreezeTime -= round_to_int(Seconds * Server()->TickSpeed());
	if(m_FreezeTime <= 0)
	{
		m_FreezeTime = 0;
		m_FreezeTick = 0;
	}
	return true;
}

void CCharacter::SetAllowFrozenWeaponSwitch(bool Allow)
{
	m_FreezeWeaponSwitch = Allow;
}

void CCharacter::TickFreeze()
{
	if(m_FreezeTime <= 0)
		return;

	m_FreezeTime--;
	m_Input.m_Direction = 0;
	m_Input.m_Jump = 0;
	m_LatestInput.m_Direction = 0;
	m_LatestInput.m_Jump = 0;

	if(m_FreezeTime <= 0)
	{
		m_FreezeTime = 0;
		m_FreezeTick = 0;
	}
}

int CCharacter::GetCID()
{
	return m_pPlayer->GetCID();
}

int CCharacter::WeaponAmmo(int Weapon) const
{
	if(Weapon < 0 || Weapon >= NUM_WEAPONS)
		return 0;
	return m_aWeapons[Weapon].m_Ammo;
}

void CCharacter::DoNinjaFire(vec2 Direction, int MoveTime)
{
	m_lpHitObjects.clear();
	m_lpHitObjects.hint_size(4);
	m_Ninja.m_ActivationDir = Direction;
	m_Ninja.m_CurrentMoveTime = MoveTime;
	m_Ninja.m_OldVelAmount = length(m_Core.m_Vel);
}

void CCharacter::EnableWeapon(int WeaponID)
{
	if(WeaponID == -1)
	{
		for(int i = 0; i < NUM_WEAPONS; i++)
		{
			EnableWeapon(i);
		}
	}
	if(WeaponID < 0 || WeaponID >= NUM_WEAPONS)
		return;
	m_aWeapons[WeaponID].m_Valid = true;
}

void CCharacter::DisableWeapon(int WeaponID)
{
	if(WeaponID == -1)
	{
		for(int i = 0; i < NUM_WEAPONS; i++)
		{
			DisableWeapon(i);
		}
	}
	if(WeaponID < 0 || WeaponID >= NUM_WEAPONS)
		return;
	m_aWeapons[WeaponID].m_Valid = false;
}

void CCharacter::RemoveWeapon(int WeaponID)
{
	m_aWeapons[WeaponID].m_Got = false;
}

void CCharacter::OnPredictedInput(CNetObj_PlayerInput *pNewInput)
{
	// check for changes
	if(mem_comp(&m_Input, pNewInput, sizeof(CNetObj_PlayerInput)) != 0)
		m_LastAction = Server()->Tick();

	// copy new input
	mem_copy(&m_Input, pNewInput, sizeof(m_Input));
	m_NumInputs++;

	// it is not allowed to aim in the center
	if(m_Input.m_TargetX == 0 && m_Input.m_TargetY == 0)
		m_Input.m_TargetY = -1;
}

void CCharacter::OnDirectInput(CNetObj_PlayerInput *pNewInput)
{
	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
	mem_copy(&m_LatestInput, pNewInput, sizeof(m_LatestInput));

	// it is not allowed to aim in the center
	if(m_LatestInput.m_TargetX == 0 && m_LatestInput.m_TargetY == 0)
		m_LatestInput.m_TargetY = -1;

	if(m_NumInputs > 2 && m_pPlayer->GetTeam() != TEAM_SPECTATORS)
	{
		HandleWeaponSwitch();
		FireWeapon();
	}

	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
}

void CCharacter::ResetInput()
{
	m_Input.m_Direction = 0;
	m_Input.m_Hook = 0;
	// simulate releasing the fire button
	if((m_Input.m_Fire & 1) != 0)
		m_Input.m_Fire++;
	m_Input.m_Fire &= INPUT_STATE_MASK;
	m_Input.m_Jump = 0;
	m_LatestPrevInput = m_LatestInput = m_Input;
}

void CCharacter::Tick()
{
	if(m_OnVehicle)
	{
		m_Core.m_Input = m_Input;
		VehicleResetCharacterHook(this);
		return;
	}

	if(m_LockedCK && m_Input.m_Jump)
		m_LockedCK = false;

	if(m_LockedCK)
	{
		m_Input.m_Jump = 0;
		m_Input.m_Direction = 0;
		m_Input.m_Hook = 0;
		m_Core.m_HookState = HOOK_IDLE;
	}

	// ─── Auto Pathfinding / Follow ──────────────────────────────
	if(m_pPlayer && m_pPlayer->m_AutoMoving)
	{
		// Update target position if in follow mode
		if(m_pPlayer->m_FollowTargetCID >= 0)
		{
			CPlayer *pTarget = GameServer()->m_apPlayers[m_pPlayer->m_FollowTargetCID];
			if(pTarget && pTarget->GetCharacter())
			{
				m_pPlayer->m_AutoTargetX = (int)pTarget->GetCharacter()->m_Core.m_Pos.x;
				m_pPlayer->m_AutoTargetY = (int)pTarget->GetCharacter()->m_Core.m_Pos.y;
			}
			else
			{
				GameServer()->SendChatTo(m_pPlayer->GetCID(), "跟随目标已离开，停止跟随。");
				m_pPlayer->m_AutoMoving = false;
				m_pPlayer->m_FollowTargetCID = -1;
			}
		}

		// Compute distance to target
		float dx = m_pPlayer->m_AutoTargetX - m_Core.m_Pos.x;
		float dy = m_pPlayer->m_AutoTargetY - m_Core.m_Pos.y;
		float dist = sqrtf(dx*dx + dy*dy);

		if(dist < 48.0f)
		{
			// Arrived — stop
			m_pPlayer->m_AutoMoving = false;
			m_pPlayer->m_FollowTargetCID = -1;
		}
		else
		{
			// Set horizontal direction
			if(dx > 0) m_Input.m_Direction = 1;
			else if(dx < 0) m_Input.m_Direction = -1;

			// Jump over obstacles: check if there's a wall in front
			vec2 CheckPos = m_Core.m_Pos + vec2(m_Input.m_Direction * 32.f, 0);
			if(GameServer()->Collision()->CheckPoint(CheckPos))
			{
				m_Input.m_Jump = 1;
			}
			// Jump if target is above and we're on ground
			if(IsGrounded() && dy < -64.f)
			{
				m_Input.m_Jump = 1;
			}
		}
	}

	m_Core.m_Input = m_Input;
	m_Core.Tick(true);

	// Handle tile processing
	int MapIndex = GameServer()->Collision()->GetPureMapIndex(m_Pos.x, m_Pos.y);
	m_pTilesHandler->Handle(MapIndex);

	// MRPG extensions - safe zone check BEFORE HandleSafeFlags for immediate effect
	HandleTuning();

	// Handle safe zone tile (MRPG) - clears when leaving zone
	if(m_pTilesHandler && m_pTilesHandler->IsActive(TILE_SW_ZONE))
		SetSafeFlags();
	else
		m_SafeTickFlags = 0;

	// Zone name overlay (MRPG: merged into HUD via GameBasicStats)
	if(m_pPlayer && !m_pPlayer->IsDummy() && m_pTilesHandler)
	{
		if(m_pTilesHandler->IsActive(TILE_SW_ZONE))
		{
			CCollision::ZoneDetail Zone;
			if(GameServer()->Collision()->GetZonedetail(m_Pos, &Zone))
			{
				if((Server()->Tick() % Server()->TickSpeed() == 0) || str_comp(m_aZoneName, Zone.Name.c_str()) != 0)
				{
					str_copy(m_aZoneName, Zone.Name.c_str(), sizeof(m_aZoneName));
					GameServer()->Broadcast(m_pPlayer->GetCID(), CGameContext::BROADCAST_PRIORITY_GAME_BASIC_STATS, 50,
						"%s 区域 (%s)", Zone.Name.c_str(), Zone.PVP ? "PVP" : "安全");
				}
			}
		}
		else if(m_pTilesHandler->IsExit(TILE_SW_ZONE))
		{
			m_aZoneName[0] = '\0';
			GameServer()->Broadcast(m_pPlayer->GetCID(), CGameContext::BROADCAST_PRIORITY_GAME_BASIC_STATS, 50, "");
		}
	}

	HandleSafeFlags();

	// ─── MRPG Tile Interactions ────────────────────────────────────
	if(m_pTilesHandler)
	{
		static vec2 s_LastTelePos = vec2(0, 0);
		static int s_TeleCooldownTick = 0;

		// NPC interaction tile
		if(m_pTilesHandler->IsEnter(TILE_NPC_INTERACT))
		{
			CPlayer *pPlayer = GetPlayer();
			if(pPlayer && !pPlayer->IsDummy())
			{
				PlayUiMenuSelect(GameServer()->m_World, pPlayer->GetCID());
				GameServer()->SendChatLoc(pPlayer->GetCID(), "npc.interact_hint", "用锤子敲击 NPC 开始对话");
			}
		}

		// Info zone
		if(m_pTilesHandler->IsEnter(TILE_INFO_ZONE))
		{
			GameServer()->SendChat(-1, CHAT_ALL, -1, "You entered a special zone.");
		}

		// Teleport FROM tile with hammer confirmation
		if(m_pTilesHandler->IsActive(TILE_TELE_FROM_CONFIRM))
		{
			CCollision *pColl = GameServer()->Collision();
			if(pColl && m_pPlayer)
			{
				const int Index = pColl->GetMapIndex(m_Pos);
				if(Index >= 0)
				{
					const CTeleTile &Tile = pColl->GetTeleTile(Index);
					vec2 TelePos;
					if(pColl->GetTeleportOutByNumber(Tile.m_Number, m_Pos, &TelePos))
					{
						GameServer()->Broadcast(m_pPlayer->GetCID(), CGameContext::BROADCAST_PRIORITY_TITLE,
							Server()->TickSpeed(), "用锤子进入");
						if(m_ActiveWeapon == WEAPON_HAMMER && m_AttackTick == Server()->Tick() - 1)
						{
							GameServer()->m_World.CreateSound(m_Pos, SOUND_SFX_TELEPORT);
							m_Core.m_Pos = TelePos;
							m_Pos = TelePos;
						}
					}
				}
			}
		}

		// Teleport FROM tile
		if(m_pTilesHandler->IsActive(TILE_TELE_FROM))
		{
			if(Server()->Tick() >= s_TeleCooldownTick)
			{
				CCollision *pColl = GameServer()->Collision();
				vec2 TelePos;
				if(pColl && pColl->GetTeleportOut(m_Pos, &TelePos))
				{
					m_Core.m_Pos = TelePos;
					m_Pos = TelePos;
					GameServer()->m_World.CreateSound(m_Pos, SOUND_SFX_TELEPORT);
					s_LastTelePos = TelePos;
					s_TeleCooldownTick = Server()->Tick() + Server()->TickSpeed() * 3;
				}
			}
		}

		// Shop zone
		if(m_pTilesHandler->IsEnter(TILE_SHOP_ZONE))
		{
			if(m_pPlayer && !m_pPlayer->IsDummy())
				PlayUiMenuOpen(GameServer()->m_World, m_pPlayer->GetCID());
			GameServer()->SendChat(-1, CHAT_ALL, -1, "Welcome to the shop! Use /shop to browse items.");
		}
		if(m_pTilesHandler->IsExit(TILE_SHOP_ZONE))
		{
			GameServer()->SendChat(-1, CHAT_ALL, -1, "Left the shop zone.");
		}
	}

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

	// ─── Mount Speed Bonus ────────────────────────────────────
	// (legacy mount removed — vehicle movement handled by CVehicle)

	if(CheckSpiderBossCore(GameServer(), this))
	{
		m_Input.m_Direction = 0;
		m_Input.m_Jump = 0;
		m_Input.m_Hook = 0;
		m_Core.m_HookState = HOOK_IDLE;
		m_Core.m_Vel = vec2(0.0f, 0.0f);
	}

	if(!Config()->m_SvContentFramework && m_CardElectronTicks > 0)
	{
		m_Core.m_Vel *= 0.86f;
		m_CardElectronTicks--;
	}

	// Skill tick-based effects
	// Renew: regen over time
	if(m_RenewTicks > 0 && (Server()->Tick() % Server()->TickSpeed()) == 0)
	{
		IncreaseHealth(m_RenewAmount);
		m_RenewTicks -= Server()->TickSpeed();
		if(m_RenewTicks <= 0)
			m_RenewTicks = 0;
	}

	// MMO mana regen (1/sec; slower while recently in combat)
	if(m_pPlayer && !m_pPlayer->IsDummy() && m_pPlayer->GetAccountId() > 0 && (Server()->Tick() % Server()->TickSpeed()) == 0)
	{
		const int MaxMana = GetMaxMana();
		if(m_Mana < MaxMana)
		{
			const int WisVal = m_pPlayer->GetStat(AttributeIdentifier::WIS);
			const int CombatGrace = Server()->TickSpeed() * 4;
			const bool InCombat = m_pPlayer->m_LastCombatTick > 0
				&& Server()->Tick() - m_pPlayer->m_LastCombatTick < CombatGrace;
			const int Regen = InCombat
				? maximum(1, MaxMana / 120 + WisVal / 10)
				: maximum(1, MaxMana / 55 + WisVal / 7);
			m_Mana = minimum(MaxMana, m_Mana + Regen);
		}
	}

	// Iron Will: countdown (effect applied in TakeDamage)
	if(m_IronWillTicks > 0)
	{
		m_IronWillTicks--;
		// Speed reduction is applied in ApplyMoveRestrictions or Handle move
		// Iron Will slow is handled automatically here
		if(m_IronWillTicks <= 0)
			m_IronWillTicks = 0;
	}

	// Shadow Step: invisibility countdown
	if(m_ShadowTicks > 0)
	{
		m_ShadowTicks--;
		m_IsInvisible = true;
		if(m_ShadowTicks <= 0)
		{
			m_ShadowTicks = 0;
			m_IsInvisible = false;
		}
	}

	if(m_LockedCK)
	{
		m_Core.m_Vel = vec2(0.0f, 0.0f);
		m_Core.m_Pos = m_LockPos;
	}

	// handle leaving gamelayer
	if(GameLayerClipped(m_Pos))
	{
		Die(m_pPlayer->GetCID(), WEAPON_WORLD);
	}

	// handle Weapons
	HandleWeapons();

	if(Server()->Tick() % 25 == 0 && m_InMining)
		m_InMining = false;
	if(m_MiningTick > -1)
		m_MiningTick--;

	// ─── Fashion Aura Effects ──────────────────────────────────
	if(m_pPlayer && m_pPlayer->m_FashionItemID > 0)
	{
		TickFashionAura();
	}
}

// ─── Fashion/Transmog visual aura effect ─────────────────────────────
void CCharacter::TickFashionAura()
{
	if(!m_pPlayer || !GameWorld())
		return;

	const int FID = m_pPlayer->m_FashionItemID;
	if(FID <= 0)
		return;

	const int Tick = Server()->Tick();
	const vec2 MyPos = m_Core.m_Pos;

	// Effect type based on FashionItemID range
	enum
	{
		EFFECT_SPARKLE = 0, // 金色小圈
		EFFECT_FIRE,        // 火焰橙红光
		EFFECT_ICE,         // 冰蓝光
		EFFECT_THUNDER,     // 闪电紫光
		EFFECT_DARK,        // 暗黑紫黑
	};

	int Effect = EFFECT_SPARKLE;
	int SoundID = -1;
	if(FID <= 100)         { Effect = EFFECT_SPARKLE; SoundID = -1; }
	else if(FID <= 200)    { Effect = EFFECT_FIRE;    SoundID = SOUND_GUN_FIRE; }
	else if(FID <= 300)    { Effect = EFFECT_ICE;     SoundID = SOUND_PLAYER_AIRJUMP; }
	else if(FID <= 400)    { Effect = EFFECT_THUNDER; SoundID = SOUND_GRENADE_FIRE; }
	else                   { Effect = EFFECT_DARK;    SoundID = SOUND_PLAYER_DIE; }

	// ── Visual pulse: small ring / glow every ~5 ticks ────────
	if(Tick % 5 == 0)
	{
		// Random offset in a ring around the player
		float Angle = (float)(Tick % 100) * 0.0628f + (float)(FID * 0.1f);
		float Radius = 32.0f;
		vec2 Off = vec2(cosf(Angle) * Radius, sinf(Angle) * Radius);

		switch(Effect)
		{
		case EFFECT_SPARKLE:
			GameWorld()->CreateHammerHit(MyPos + Off);
			break;
		case EFFECT_FIRE:
			GameWorld()->CreateHammerHit(MyPos + Off);
			break;
		case EFFECT_ICE:
			GameWorld()->CreatePlayerSpawn(MyPos + Off);
			break;
		case EFFECT_THUNDER:
			GameWorld()->CreateHammerHit(MyPos + vec2(Off.x, 0));
			GameWorld()->CreateHammerHit(MyPos + vec2(0, Off.y));
			break;
		case EFFECT_DARK:
			GameWorld()->CreateDeath(MyPos + Off, m_pPlayer->GetCID());
			break;
		}
	}

	// ── Sound effect every ~15 ticks ───────────────────────────
	if(SoundID >= 0 && Tick % 15 == 0)
	{
		GameWorld()->CreateSound(MyPos, SoundID);
	}

	// ── Big burst effect every ~45 ticks ───────────────────────
	if(Tick % 45 == 0)
	{
		switch(Effect)
		{
		case EFFECT_SPARKLE:
			GameWorld()->CreatePlayerSpawn(MyPos);
			break;
		case EFFECT_FIRE:
			GameWorld()->CreateExplosion(MyPos, (CEntity*)this, WEAPON_GRENADE, 0);
			break;
		case EFFECT_ICE:
			GameWorld()->CreatePlayerSpawn(MyPos + vec2(0, -32));
			break;
		case EFFECT_THUNDER:
			GameWorld()->CreateHammerHit(MyPos + vec2(-20, -20));
			GameWorld()->CreateHammerHit(MyPos + vec2(20, -20));
			GameWorld()->CreateHammerHit(MyPos + vec2(-20, 20));
			GameWorld()->CreateHammerHit(MyPos + vec2(20, 20));
			break;
		case EFFECT_DARK:
			GameWorld()->CreateDeath(MyPos, m_pPlayer->GetCID());
			GameWorld()->CreateSound(MyPos, SOUND_PLAYER_DIE);
			break;
		}
	}
}

void CCharacter::TickDefered()
{
	if(m_OnVehicle)
	{
		m_SendCore = m_Core;
		m_ReckoningCore = m_Core;
		m_ReckoningTick = Server()->Tick();
		m_Pos = m_Core.m_Pos;
		return;
	}

	TickFreeze();

	static const vec2 ColBox(CCharacterCore::PHYS_SIZE, CCharacterCore::PHYS_SIZE);
	// advance the dummy
	{
		CWorldCore TempWorld;
		if(Server()->GetClientVersion(GetCID()) >= MIN_CORRECTTUNING_CLIENTVERSION)
			TempWorld.m_Tuning = *GameServer()->Tuning();
		m_ReckoningCore.Init(&TempWorld, GameServer()->Collision());
		m_ReckoningCore.Tick(false);
		m_ReckoningCore.Move();
		m_ReckoningCore.Quantize();
	}

	// apply drag velocity when the player is not firing ninja
	// and set it back to 0 for the next tick
	if(m_ActiveWeapon != WEAPON_NINJA || m_Ninja.m_CurrentMoveTime < 0)
		m_Core.AddDragVelocity();
	m_Core.ResetDragVelocity();

	// lastsentcore
	vec2 StartPos = m_Core.m_Pos;
	vec2 StartVel = m_Core.m_Vel;
	bool StuckBefore = GameServer()->Collision()->TestBox(m_Core.m_Pos, ColBox);

	if(!CheckSpiderBossCore(GameServer(), this))
		m_Core.Move();

	bool StuckAfterMove = GameServer()->Collision()->TestBox(m_Core.m_Pos, ColBox);
	m_Core.Quantize();
	bool StuckAfterQuant = GameServer()->Collision()->TestBox(m_Core.m_Pos, ColBox);
	m_Pos = m_Core.m_Pos;

	// MRPG extensions: apply move restrictions and save position for stuck detection
	ApplyMoveRestrictions();
	m_PrevPos = m_Pos;

	if(!StuckBefore && (StuckAfterMove || StuckAfterQuant))
	{
		// Hackish solution to get rid of strict-aliasing warning
		union
		{
			float f;
			unsigned u;
		} StartPosX, StartPosY, StartVelX, StartVelY;

		StartPosX.f = StartPos.x;
		StartPosY.f = StartPos.y;
		StartVelX.f = StartVel.x;
		StartVelY.f = StartVel.y;

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "STUCK!!! %d %d %d %f %f %f %f %x %x %x %x",
			StuckBefore,
			StuckAfterMove,
			StuckAfterQuant,
			StartPos.x, StartPos.y,
			StartVel.x, StartVel.y,
			StartPosX.u, StartPosY.u,
			StartVelX.u, StartVelY.u);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	}

	m_TriggeredEvents |= m_Core.m_TriggeredEvents;

	if(m_pPlayer->GetTeam() == TEAM_SPECTATORS)
	{
		m_Pos.x = m_Input.m_TargetX;
		m_Pos.y = m_Input.m_TargetY;
	}
	else if(m_Core.m_Death)
	{
		// handle death-tiles and ensure no core dumped issue.
		// also, since dead characters are removed from the game world and won't be snapped,
		// so we can skip the 'update the m_SendCore' process.
		Die(m_pPlayer->GetCID(), WEAPON_WORLD);
		return;
	}

	// manage emote here instead of in the snap function
	if(m_EmoteStop < Server()->Tick())
	{
		SetEmote(EMOTE_NORMAL, -1);
	}

	GameServer()->m_pController->HandleCharacterTiles(this, StartPos, m_Pos);

	// update the m_SendCore if needed
	{
		CNetObj_Character Predicted;
		CNetObj_Character Current;
		mem_zero(&Predicted, sizeof(Predicted));
		mem_zero(&Current, sizeof(Current));
		m_ReckoningCore.Write(&Predicted);
		m_Core.Write(&Current);

		// only allow dead reckoning for a top of 3 seconds
		if(m_ReckoningTick + Server()->TickSpeed() * 3 < Server()->Tick() || mem_comp(&Predicted, &Current, sizeof(CNetObj_Character)) != 0)
		{
			m_ReckoningTick = Server()->Tick();
			m_SendCore = m_Core;
			m_ReckoningCore = m_Core;
		}
	}
}

void CCharacter::TickPaused()
{
	++m_AttackTick;
	++m_Ninja.m_ActivationTick;
	++m_ReckoningTick;
	if(m_LastAction != -1)
		++m_LastAction;
	if(m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart > -1)
		++m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart;
	if(m_EmoteStop > -1)
		++m_EmoteStop;
}

bool CCharacter::IncreaseHealth(int Amount)
{
	if(m_Health >= m_MaxHealth)
		return false;
	const int OldHealth = m_Health;
	m_Health = clamp(m_Health + Amount, 0, m_MaxHealth);
	if(m_Health != OldHealth && m_pPlayer && !m_pPlayer->IsDummy())
		GameServer()->MarkUpdatedBroadcast(m_pPlayer->GetCID());
	return true;
}

void CCharacter::AddMaxHealth(int Amount)
{
	if(Amount == 0)
		return;
	m_Health += Amount;
	m_MaxHealth += Amount;
	if(m_pPlayer && !m_pPlayer->IsDummy())
		GameServer()->MarkUpdatedBroadcast(m_pPlayer->GetCID());
}

void CCharacter::SetHealthDirect(int Amount)
{
	const int Max = GameServer()->Config()->m_SvPlayerMaxHealth;
	m_Health = clamp(Amount, 0, Max);
	if(m_pPlayer && !m_pPlayer->IsDummy())
		GameServer()->MarkUpdatedBroadcast(m_pPlayer->GetCID());
}

void CCharacter::SetArmorDirect(int Amount)
{
	m_Armor = clamp(Amount, 0, 10);
}

void CCharacter::SetBossHealth(int Amount)
{
	m_Health = maximum(1, Amount);
}

void CCharacter::SetHitRadius(float Radius)
{
	SetProximityRadius(Radius);
}

void CCharacter::SyncSpiderBody(vec2 Pos)
{
	m_Core.m_Pos = Pos;
	m_Pos = Pos;
	m_Core.m_Vel = vec2(0.0f, 0.0f);
}

bool CCharacter::IncreaseArmor(int Amount)
{
	if(m_Armor >= 10)
		return false;
	m_Armor = clamp(m_Armor + Amount, 0, 10);
	return true;
}

void CCharacter::ReduceArmor(int Amount)
{
	if(Amount <= 0)
		return;
	m_Armor = maximum(0, m_Armor - Amount);
}

void CCharacter::Die(int Killer, int Weapon)
{
	if(!m_Alive)
		return;

	if(m_pPlayer)
		VehicleOnCharacterDie(GameServer(), m_pPlayer->GetCID());

	if(m_pFishingRod)
	{
		delete m_pFishingRod;
		m_pFishingRod = nullptr;
	}

	delete m_pTilesHandler;
	m_pTilesHandler = nullptr;

	// we got to wait 0.5 secs before respawning
	m_Alive = false;
	m_pPlayer->m_RespawnTick = Server()->Tick() + Server()->TickSpeed() / 2;
	int ModeSpecial = GameServer()->m_pController->OnCharacterDeath(this, (Killer < 0) ? 0 : GameServer()->m_apPlayers[Killer], Weapon);

	const bool VictimIsZombie = m_pPlayer->IsDummy() && m_pPlayer->GetZomb() != ZOMB_NONE;
	if(!VictimIsZombie)
	{
		char aBuf[256];
		if(Killer < 0)
		{
			str_format(aBuf, sizeof(aBuf), "kill killer='%d:%d:' victim='%d:%d:%s' weapon=%d special=%d",
				Killer, -1 - Killer,
				m_pPlayer->GetCID(), m_pPlayer->GetTeam(), Server()->ClientName(m_pPlayer->GetCID()), Weapon, ModeSpecial);
		}
		else
		{
			const int KillerTeam = GameServer()->m_apPlayers[Killer] ? GameServer()->m_apPlayers[Killer]->GetTeam() : -1;
			str_format(aBuf, sizeof(aBuf), "kill killer='%d:%d:%s' victim='%d:%d:%s' weapon=%d special=%d",
				Killer, KillerTeam, Server()->ClientName(Killer),
				m_pPlayer->GetCID(), m_pPlayer->GetTeam(), Server()->ClientName(m_pPlayer->GetCID()), Weapon, ModeSpecial);
		}
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	}

	const int VictimServer = m_pPlayer->GetCID();
	CNetMsg_Sv_KillMsg Msg;
	Msg.m_ModeSpecial = ModeSpecial;
	Msg.m_Assist = -1;
	for(int i = 0; i < MAX_HUMAN_CLIENTS; i++)
	{
		if(!Server()->ClientIngame(i))
			continue;

		const int Victim = GameServer()->ClientDisplaySlot(i, VictimServer);
		if(Victim < 0)
			continue;

		Msg.m_Victim = Victim;

		if(Killer < 0 && Server()->GetClientVersion(i) < MIN_KILLMESSAGE_CLIENTVERSION)
		{
			Msg.m_Killer = 0;
			Msg.m_Weapon = WEAPON_WORLD;
		}
		else if(Killer >= MAX_HUMAN_CLIENTS)
		{
			const int KillerDisplay = GameServer()->ClientDisplaySlot(i, Killer);
			Msg.m_Killer = KillerDisplay >= 0 ? KillerDisplay : -1;
			Msg.m_Weapon = Weapon;
		}
		else
		{
			Msg.m_Killer = Killer;
			Msg.m_Weapon = Weapon;
		}
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
	}

	// a nice sound
	GameWorld()->CreateSound(m_Pos, SOUND_PLAYER_DIE);

	// this is for auto respawn after 3 secs
	m_pPlayer->m_DieTick = Server()->Tick();

	GameWorld()->RemoveEntity(this);
	GameWorld()->m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	GameWorld()->CreateDeath(m_Pos, m_pPlayer->GetCID());
}

bool CCharacter::TakeDamage(vec2 Force, vec2 Source, int Dmg, int From, int Weapon)
{
	if(!m_Alive)
		return false;

	if(GameServer()->m_pController->OnCharacterTakeDamage(this, Force, Dmg, From, Weapon))
		return false;

	if(m_pPlayer->m_ZamerDetonating)
		return false;

	if(CheckSpiderBossCore(GameServer(), this) && From == m_pPlayer->GetCID())
		return false;

	m_Core.m_Vel += Force;

	if(From >= 0)
	{
		if(GameServer()->m_pController->IsFriendlyFire(m_pPlayer->GetCID(), From, Dmg))
			return false;
		// NPCs (Quest Npcs) are invulnerable
		if(m_pPlayer && m_pPlayer->IsQuestNpc())
			return false;

		// Check AI damage rules for bot characters
		if(dynamic_cast<CCharacterBotAI*>(this))
		{
			CCharacterBotAI *pBotAI = static_cast<CCharacterBotAI*>(this);
			if(!pBotAI->IsAllowedPVP(From))
				return false;
		}
		if(GameServer()->m_apPlayers[From] && !GameServer()->m_apPlayers[From]->IsDummy())
		{
			if(Weapon != WEAPON_LASER && Weapon != WEAPON_WORLD && Weapon != WEAPON_SELF && Weapon != WEAPON_NINJA)
			{
				if(CItemHelper *pH = GameServer()->ItemHelper())
				{
					const char *pEx = GameServer()->m_apPlayers[From]->GetExtraForItem(GameServer()->m_apPlayers[From]->GetHolding(ITYPE_SWORD));
					if(Config()->m_SvContentFramework && GameServer()->Core() && GameServer()->Core()->EffectRegistry())
					{
						CEffectContext Ctx = {};
						Ctx.m_pAttacker = GameServer()->m_apPlayers[From]->GetCharacter();
						Ctx.m_pVictim = this;
						Ctx.m_pExtraJson = pEx;
						Ctx.m_Weapon = Weapon;
						Ctx.m_Source = Source;
						GameServer()->Core()->EffectRegistry()->Apply(TRIGGER_TAKE_DAMAGE, Ctx);
					}
					if(Config()->m_SvContentLegacyCards || !Config()->m_SvContentFramework)
					{
						const int El = pH->GetCard(pEx, ITEM_CARD_ELECTRON_ID);
						const int Frc = pH->GetCard(pEx, ITEM_CARD_FORCE_ID);
						if(El > 0)
							ApplyElectronSlow(El + minimum(El, Frc));
					}
				}
			}
		}
	}
	else
	{
		int Team = TEAM_RED;
		if(From == PLAYER_TEAM_BLUE)
			Team = TEAM_BLUE;
		if(GameServer()->m_pController->IsFriendlyTeamFire(m_pPlayer->GetTeam(), Team, Dmg))
			return false;
	}

	// Self-damage: immunity cards or default halving
	if(From == m_pPlayer->GetCID())
	{
		CItemHelper *pH = GameServer()->ItemHelper();
		int ImmunityPct = 0;
		if(pH && m_pPlayer && !m_pPlayer->IsDummy())
			ImmunityPct = pH->CountArmorWithCard(m_pPlayer, ITEM_CARD_SELF_HARM_IMMUNITY) * 30;
		if(ImmunityPct >= 100)
			return false;
		if(ImmunityPct > 0)
			Dmg = maximum(1, Dmg * (100 - ImmunityPct) / 100);
		else
			Dmg = maximum(1, Dmg / 2);
	}

	if(m_pPlayer->GetZomb() == ZOMB_SPIDER_BOSS && From >= 0 && From < MAX_CLIENTS &&
		GameServer()->m_apPlayers[From] && !GameServer()->m_apPlayers[From]->IsDummy())
		Dmg *= 4;

	if(Dmg > 0 && m_pPlayer && !m_pPlayer->IsDummy() && GameServer()->ItemHelper())
	{
		CItemHelper *pH = GameServer()->ItemHelper();

		const int ShieldStacks = pH->SumArmorEffectStacks(m_pPlayer, ITEM_CARD_ABSORPTION_SHIELD, "absorption_shield");
		if(ShieldStacks > 0 && Config()->m_SvContentFramework && GameServer()->Core() && GameServer()->Core()->StatusManager()
			&& (random_int() % 100) < 25)
		{
			GameServer()->Core()->StatusManager()->ApplyStatus(this, "shield", ShieldStacks, 300, 0.f, 5 * ShieldStacks);
		}

		const int FortStacks = pH->SumArmorEffectStacks(m_pPlayer, ITEM_CARD_FORTIFICATION, "fortification");
		if(FortStacks > 0)
			Dmg = maximum(1, Dmg - 2 * FortStacks);

		if(m_MaxHealth > 0 && m_Health * 2 <= m_MaxHealth)
		{
			const int ResStacks = pH->SumArmorEffectStacks(m_pPlayer, ITEM_CARD_RESILIENCE, "resilience");
			if(ResStacks > 0)
			{
				const int Reduction = minimum(90, 25 * ResStacks);
				Dmg = maximum(1, Dmg * (100 - Reduction) / 100);
			}
		}
	}

	// Iron Will: 50% damage reduction
	if(m_IronWillTicks > 0)
		Dmg = maximum(1, Dmg / 2);

	if(Config()->m_SvContentFramework && GameServer()->Core() && GameServer()->Core()->StatusManager())
		GameServer()->Core()->StatusManager()->AbsorbDamage(this, Dmg);

	// Apply MMO defense reduction (EquippedSlots + player CON defense stat)
	if(Dmg > 0 && m_pPlayer && !m_pPlayer->IsDummy())
	{
		int TotalDefense = 0;

		const ItemType aArmorSlots[] = {
			ItemType::EquipHelmetTank, ItemType::EquipHelmetDPS, ItemType::EquipHelmetHealer,
			ItemType::EquipArmorTank, ItemType::EquipArmorDPS, ItemType::EquipArmorHealer,
			ItemType::EquipGloves, ItemType::EquipEidolon,
		};
		for(int i = 0; i < (int)(sizeof(aArmorSlots) / sizeof(aArmorSlots[0])); i++)
		{
			const int SlotItemID = m_pPlayer->m_EquippedSlots.getSlot(aArmorSlots[i]);
			if(SlotItemID > 0)
			{
				const CMMOItemDescription *pDef = CMMOItemDescription::Get(SlotItemID);
				if(pDef)
					TotalDefense += pDef->GetAttributeValue(AttributeIdentifier::Defense, m_pPlayer->GetMMOItemEnchant(SlotItemID));
			}
		}

		// Add player defense stat from CON (TRPG)
		TotalDefense += m_pPlayer->GetEffectiveDefense();

		// Fallback: old system legacy items (may return 0 if server_items JSONs deleted)
		if(GameServer()->ItemHelper())
		{
			const int OldHelmet = m_pPlayer->GetHolding(ITYPE_HELMET);
			const int OldChest = m_pPlayer->GetHolding(ITYPE_CHEST);
			const int OldLegs = m_pPlayer->GetHolding(ITYPE_LEGS);
			TotalDefense += GameServer()->ItemHelper()->GetDefense(OldHelmet)
				+ GameServer()->ItemHelper()->GetDefense(OldChest)
				+ GameServer()->ItemHelper()->GetDefense(OldLegs);
		}

		if(TotalDefense > 0)
			Dmg = maximum(1, Dmg - TotalDefense);
	}

	int OldHealth = m_Health, OldArmor = m_Armor;
	if(Dmg)
	{
		if(m_pPlayer && !m_pPlayer->IsDummy())
			m_pPlayer->MarkCombat();
		if(From >= 0 && From < MAX_CLIENTS)
		{
			CPlayer *pFrom = GameServer()->m_apPlayers[From];
			if(pFrom && !pFrom->IsDummy())
				pFrom->MarkCombat();
		}

		if(m_Armor)
		{
			if(Dmg > 1)
			{
				m_Health--;
				Dmg--;
			}

			if(Dmg > m_Armor)
			{
				Dmg -= m_Armor;
				m_Armor = 0;
			}
			else
			{
				m_Armor -= Dmg;
				Dmg = 0;
			}
		}

		m_Health -= Dmg;
	}

	const int Dealt = (OldHealth - m_Health) + (OldArmor - m_Armor);
	if(Dealt > 0 && From >= 0 && GameServer()->m_apPlayers[From] && !GameServer()->m_apPlayers[From]->IsDummy())
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
			Ctx.m_InDamage = Dealt;
			GameServer()->Core()->EffectRegistry()->Apply(TRIGGER_DEAL_DAMAGE, Ctx);

			// Aggregate lifesteal / armor_shred from armor items (helm/chest/legs)
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
				ArmorCtx.m_InDamage = Dealt;
				GameServer()->Core()->EffectRegistry()->Apply(TRIGGER_DEAL_DAMAGE, ArmorCtx);
			}
		}
	}

	if(Dealt > 0 && m_pPlayer && !m_pPlayer->IsDummy() && GameServer()->ItemHelper())
	{
		CItemHelper *pH = GameServer()->ItemHelper();

		const int ThornsStacks = pH->SumArmorEffectStacks(m_pPlayer, ITEM_CARD_THORNS, "thorns");
		if(ThornsStacks > 0 && From >= 0 && From != m_pPlayer->GetCID() && From < MAX_CLIENTS
			&& GameServer()->m_apPlayers[From] && GameServer()->m_apPlayers[From]->GetCharacter()
			&& GameServer()->m_apPlayers[From]->GetCharacter()->IsAlive())
		{
			const int Reflect = maximum(1, Dealt * 7 * ThornsStacks / 100);
			GameServer()->m_apPlayers[From]->GetCharacter()->TakeDamage(vec2(0.f, 0.f), m_Pos, Reflect, m_pPlayer->GetCID(), WEAPON_WORLD);
		}

		const int RetStacks = pH->SumArmorEffectStacks(m_pPlayer, ITEM_CARD_RETRIBUTION, "retribution");
		if(RetStacks > 0)
		{
			m_RetaliationStacks = RetStacks;
			m_RetaliationExpireTick = Server()->Tick() + Server()->TickSpeed() * 3;
		}
	}

	// create healthmod indicator
	GameWorld()->CreateDamage(m_Pos, m_pPlayer->GetCID(), Source, OldHealth - m_Health, OldArmor - m_Armor, From == m_pPlayer->GetCID());

	// do damage Hit sound
	if(From >= 0 && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From])
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

	// ─── World Boss damage tracking ────────────────────────────────
	if(Dealt > 0 && From >= 0 && From < MAX_CLIENTS && m_pPlayer && m_pPlayer->m_IsWorldBoss)
	{
		if(TWorldController *pCore = GameServer()->Core())
		{
			if(CWorldBossManager *pWB = pCore->GetWorldBossManager())
			{
				pWB->RecordDamage(m_pPlayer->GetCID(), From, Dealt);
			}
		}
	}

	if(Dealt > 0 && m_pPlayer && !m_pPlayer->IsDummy())
		GameServer()->MarkUpdatedBroadcast(m_pPlayer->GetCID());

	// check for death
	if(m_Health <= 0)
	{
		// set attacker's face to happy (taunt!)
		if(From >= 0 && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From])
		{
			CCharacter *pChr = GameServer()->m_apPlayers[From]->GetCharacter();
			if(pChr)
			{
				pChr->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
			}
		}
		// move the die function below so that we can ensure no core dumped issue here
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

void CCharacter::ApplyElectronSlow(int CardStacks)
{
	if(CardStacks <= 0)
		return;
	m_CardElectronTicks += (Server()->TickSpeed() * CardStacks) / 8;
}

bool CCharacter::TakeHit(vec2 Force, vec2 Source, int Dmg, CEntity *pFrom, int Weapon)
{
	return TakeDamage(Force, Source, Dmg, GameWorld()->DamageOwnerFromEntity(pFrom), Weapon);
}

void CCharacter::Snap(int SnappingClient)
{
	if(m_pPlayer && m_pPlayer->GetZomb() == ZOMB_ZINVIS && !m_pPlayer->IsZombVisible() &&
		SnappingClient != -1 && SnappingClient != m_pPlayer->GetCID())
		return;

	if(NetworkClipped(SnappingClient))
	{
		if(m_Core.m_HookState == HOOK_IDLE)
			return;
		if(NetworkClippedLine(SnappingClient, m_Pos, m_Core.m_HookPos))
			return;
	}

	int ID = m_pPlayer->GetCID();
	if(SnappingClient >= 0 && ID >= MAX_HUMAN_CLIENTS)
	{
		if(!m_pPlayer->IsVisibleForClient(SnappingClient))
			return;
		if(!Server()->Translate(ID, SnappingClient))
			return;
	}

	CNetObj_Character *pCharacter = static_cast<CNetObj_Character *>(Server()->SnapNewItem(NETOBJTYPE_CHARACTER, ID, sizeof(CNetObj_Character)));
	if(!pCharacter)
		return;

	// write down the m_Core
	if(m_OnVehicle)
	{
		VehicleResetCharacterHook(this);
		pCharacter->m_Tick = 0;
		m_Core.Write(pCharacter);
		pCharacter->m_HookState = HOOK_IDLE;
		pCharacter->m_HookX = pCharacter->m_X;
		pCharacter->m_HookY = pCharacter->m_Y;
	}
	else if(!m_ReckoningTick)
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

	pCharacter->m_Emote = m_EmoteType;

	pCharacter->m_AmmoCount = 0;
	pCharacter->m_Health = 0;
	pCharacter->m_Armor = 0;
	pCharacter->m_TriggeredEvents = m_TriggeredEvents;

	pCharacter->m_Weapon = (m_ActiveCategory == WEAPONCAT_MAGIC) ? WEAPON_VISUAL_NONE : m_ActiveWeapon;
	pCharacter->m_AttackTick = m_AttackTick;

	pCharacter->m_Direction = m_Input.m_Direction;

	if(m_pPlayer->GetCID() == SnappingClient || SnappingClient == -1 ||
		(!Config()->m_SvStrictSpectateMode && m_pPlayer->GetCID() == GameServer()->m_apPlayers[SnappingClient]->GetSpectatorID()))
	{
		pCharacter->m_Health = m_Health;
		pCharacter->m_Armor = m_Armor;
		if(m_ActiveCategory == WEAPONCAT_MAGIC)
			pCharacter->m_AmmoCount = 0;
		else if(m_ActiveWeapon == WEAPON_NINJA)
			pCharacter->m_AmmoCount = m_Ninja.m_ActivationTick + g_pData->m_Weapons.m_Ninja.m_Duration * Server()->TickSpeed() / 1000;
		else if(m_aWeapons[m_ActiveWeapon].m_Ammo > 0)
			pCharacter->m_AmmoCount = m_aWeapons[m_ActiveWeapon].m_Ammo;
	}

	if(pCharacter->m_Emote == EMOTE_NORMAL)
	{
		if(5 * Server()->TickSpeed() - ((Server()->Tick() - m_LastAction) % (5 * Server()->TickSpeed())) < 5)
			pCharacter->m_Emote = EMOTE_BLINK;
	}

	// Vehicle visual: happy emote while riding
	if(m_OnVehicle)
		pCharacter->m_Emote = EMOTE_HAPPY;

	// Fashion visual: offset angle to indicate fashion is equipped (client-side visual cue)
	if(m_pPlayer && m_pPlayer->m_FashionItemID > 0)
	{
		pCharacter->m_Angle += 512; // offset aim angle as fashion marker
	}

}

void CCharacter::PostSnap()
{
	m_TriggeredEvents = 0;
}

void CCharacter::HandleSafeFlags()
{
	// Reset all safety flags first (MRPG-style)
	m_Core.m_CollisionDisabled = false;
	m_Core.m_HookHitDisabled = false;
	m_Core.m_DamageDisabled = false;
	m_Core.m_Super = false;
	// m_NewHook is controlled by tuning, not safe flags

	// Apply current safe tick flags
	if(m_SafeTickFlags & SAFEFLAG_COLLISION_DISABLED)
		m_Core.m_CollisionDisabled = true;
	if(m_SafeTickFlags & SAFEFLAG_HOOK_HIT_DISABLED)
		m_Core.m_HookHitDisabled = true;
	if(m_SafeTickFlags & SAFEFLAG_DAMAGE_DISABLED)
		m_Core.m_DamageDisabled = true;
	if(m_SafeTickFlags & SAFEFLAG_SUPER)
		m_Core.m_Super = true;
}

void CCharacter::ApplyMoveRestrictions()
{
	if(m_MoveRestrictions & MOVERESTRICTION_PREVENT_LEFT)
		m_Core.m_Vel.x = maximum(m_Core.m_Vel.x, 0.0f);
	if(m_MoveRestrictions & MOVERESTRICTION_PREVENT_RIGHT)
		m_Core.m_Vel.x = minimum(m_Core.m_Vel.x, 0.0f);
	if(m_MoveRestrictions & MOVERESTRICTION_PREVENT_UP)
		m_Core.m_Vel.y = maximum(m_Core.m_Vel.y, 0.0f);
	if(m_MoveRestrictions & MOVERESTRICTION_PREVENT_DOWN)
		m_Core.m_Vel.y = minimum(m_Core.m_Vel.y, 0.0f);
}

bool CCharacter::IncreaseMana(int Amount)
{
	if(Amount <= 0)
		return true;
	const int MaxMana = GetMaxMana();
	if(m_Mana + Amount > MaxMana)
		return false;
	m_Mana += Amount;
	return true;
}

bool CCharacter::TryUseMana(int Mana)
{
	if(Mana <= 0)
		return true;
	if(m_Mana < Mana)
		return false;
	m_Mana -= Mana;
	return true;
}

void CCharacter::RefillMana()
{
	if(m_pPlayer && !m_pPlayer->IsDummy())
		m_Mana = m_pPlayer->GetMaxMana();
}

int CCharacter::GetMaxMana() const
{
	if(m_pPlayer && !m_pPlayer->IsDummy())
		return m_pPlayer->GetMaxMana();
	return 10;
}

void CCharacter::HandleIndependentTuning()
{
	// Move restrictions are already applied by ApplyMoveRestrictions()
	// called directly from Tick(), so no need to handle them here.

	// Handle water physics and oxygen
	HandleWater();

	// Handle buffs and status effects
	HandleBuff();
}

void CCharacter::HandleWater()
{
	const int MaxWaterAir = 60;

	// Not in water or no tiles handler → recover oxygen (MRPG-style)
	if(!m_pTilesHandler || !m_pTilesHandler->IsActive(TILE_WATER))
	{
		if(Server()->Tick() % Server()->TickSpeed() == 0)
		{
			if(m_WaterAir < MaxWaterAir)
				m_WaterAir++;
		}
		return;
	}

	// Apply water zone tuning via CTuneZoneManager (MRPG-style)
	// The predefined WATER params handle gravity/friction/control
	m_TuneZoneOverride = static_cast<int>(ETuneZone::WATER);

	SetEmote(EMOTE_BLINK, Server()->Tick() + Server()->TickSpeed() / 2);

	// Check if head is submerged (MRPG-style: check 16px above position)
	const bool HeadSubmerged = GameServer()->Collision()->CheckPoint(
		vec2(m_Core.m_Pos.x, m_Core.m_Pos.y - 16.f),
		CCollision::COLFLAG_WATER);

	if(HeadSubmerged)
	{
		// Submerged → consume oxygen
		if(m_WaterAir > 0)
		{
			if(Server()->Tick() % Server()->TickSpeed() == 0)
			{
				m_WaterAir--;
				// MRPG-style: broadcast air level to player
				if(m_pPlayer)
				{
					GameServer()->Broadcast(m_pPlayer->GetCID(), CGameContext::BROADCAST_PRIORITY_GAME_WARNING,
						Server()->TickSpeed(), "氧气: %d/%d", m_WaterAir, MaxWaterAir);
				}
			}
		}
	}
	else
	{
		// Head above water but still in water tile → recover oxygen slowly (MRPG-style)
		if(Server()->Tick() % (Server()->TickSpeed() / 2) == 0)
		{
			if(m_WaterAir < MaxWaterAir)
				m_WaterAir++;
		}
	}
}

void CCharacter::HandleBuff()
{
	// Handle water drowning damage (oxygen depleted, MRPG-style)
	if(m_pTilesHandler && m_pTilesHandler->IsActive(TILE_WATER))
	{
		const bool HeadSubmerged = GameServer()->Collision()->CheckPoint(
			vec2(m_Core.m_Pos.x, m_Core.m_Pos.y - 16.f),
			CCollision::COLFLAG_WATER);
		if(HeadSubmerged && m_WaterAir <= 0)
		{
			if(Server()->Tick() % (Server()->TickSpeed() / 2) == 0)
				TakeDamage(vec2(0, 0), m_Pos, 2, -1, WEAPON_WORLD);
		}
	}

	// CStatusManager already handles DOT (burn/bleed/poison) effects globally
	// via its OnTick() → TickCharacter() call, and slow effects (electron_slow/frost)
	// are applied directly to m_Vel in ProcessStatus(). No duplication needed.
}

void CCharacter::HandleTuning()
{
	// Reset tune zone override each tick (MRPG-style: base state before independent tuning)
	m_TuneZoneOverride = -1;

	// Process move restrictions, water, and buffs in order
	HandleIndependentTuning();
}

bool CCharacter::IsTileActive(int Tile) const
{
	return m_pTilesHandler && m_pTilesHandler->IsActive(Tile);
}

bool CCharacter::IsTileEnter(int Tile)
{
	return m_pTilesHandler && m_pTilesHandler->IsEnter(Tile);
}

bool CCharacter::IsTileExit(int Tile)
{
	return m_pTilesHandler && m_pTilesHandler->IsExit(Tile);
}
