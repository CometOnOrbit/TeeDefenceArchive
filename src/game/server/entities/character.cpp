/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/shared/config.h>

#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/item_system.h>
#include <game/server/player.h>
#include <generated/server_data.h>

#include <engine/shared/config.h>

#include <game/server/core/components/content/content_types.h>
#include <game/server/core/components/content/effect_registry.h>
#include <game/server/core/components/content/status_manager.h>
#include <game/server/core/tworld_controller.h>

#include "character.h"
/*
#include "laser.h"
*/
#include "projectile.h"

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
	m_TriggeredEvents = 0;
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
	m_ActiveWeapon = WEAPON_GUN;
	m_LastWeapon = WEAPON_HAMMER;
	m_QueuedWeapon = -1;

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
	m_LockedCK = false;
	m_LockPos = vec2(0.0f, 0.0f);
	m_CardElectronTicks = 0;
	m_MaxHealth = GameServer()->Config()->m_SvPlayerMaxHealth;
	m_RetaliationExpireTick = 0;
	m_RetaliationStacks = 0;

	for(int i = 0; i < NUM_WEAPONS; i++)
		m_aWeapons[i].m_Valid = true;

	m_NumInputs = 0;
	mem_zero(&m_Input, sizeof(m_Input));
	mem_zero(&m_LatestInput, sizeof(m_LatestInput));
	mem_zero(&m_LatestPrevInput, sizeof(m_LatestPrevInput));
	m_Input.m_TargetY = -1;
	m_LatestInput.m_TargetY = -1;
	m_LatestPrevInput.m_TargetY = -1;

	GameServer()->m_pController->OnCharacterSpawn(this);

	return true;
}

void CCharacter::Destroy()
{
	GameWorld()->m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	m_Alive = false;
}

void CCharacter::SetWeapon(int W)
{
	if(W == m_ActiveWeapon)
		return;

	m_LastWeapon = m_ActiveWeapon;
	m_QueuedWeapon = -1;
	m_ActiveWeapon = W;
	GameWorld()->CreateSound(m_Pos, SOUND_WEAPON_SWITCH);

	if(m_ActiveWeapon < 0 || m_ActiveWeapon >= NUM_WEAPONS)
		m_ActiveWeapon = 0;
	m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = -1;
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

void CCharacter::HandleWeaponSwitch()
{
	int WantedWeapon = m_ActiveWeapon;
	if(m_QueuedWeapon != -1)
		WantedWeapon = m_QueuedWeapon;

	// select Weapon
	int Next = CountInput(m_LatestPrevInput.m_NextWeapon, m_LatestInput.m_NextWeapon).m_Presses;
	int Prev = CountInput(m_LatestPrevInput.m_PrevWeapon, m_LatestInput.m_PrevWeapon).m_Presses;

	if(Next < 128) // make sure we only try sane stuff
	{
		while(Next) // Next Weapon selection
		{
			WantedWeapon = (WantedWeapon + 1) % NUM_WEAPONS;
			if(m_aWeapons[WantedWeapon].m_Got && m_aWeapons[WantedWeapon].m_Valid)
				Next--;
		}
	}

	if(Prev < 128) // make sure we only try sane stuff
	{
		while(Prev) // Prev Weapon selection
		{
			WantedWeapon = (WantedWeapon - 1) < 0 ? NUM_WEAPONS - 1 : WantedWeapon - 1;
			if(m_aWeapons[WantedWeapon].m_Got && m_aWeapons[WantedWeapon].m_Valid)
				Prev--;
		}
	}

	// Direct Weapon selection
	if(m_LatestInput.m_WantedWeapon)
		WantedWeapon = m_Input.m_WantedWeapon - 1;

	// check for insane values
	if(WantedWeapon >= 0 && WantedWeapon < NUM_WEAPONS && WantedWeapon != m_ActiveWeapon && m_aWeapons[WantedWeapon].m_Got && m_aWeapons[WantedWeapon].m_Valid)
		m_QueuedWeapon = WantedWeapon;

	DoWeaponSwitch();
}

void CCharacter::FireWeapon()
{
	if(m_ReloadTimer != 0)
		return;

	DoWeaponSwitch();
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

	// fire Weapon, if wanted
	FireWeapon();

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
	if(m_aWeapons[Weapon].m_Ammo < g_pData->m_Weapons.m_aId[Weapon].m_Maxammo || !m_aWeapons[Weapon].m_Got)
	{
		m_aWeapons[Weapon].m_Got = true;
		m_aWeapons[Weapon].m_Ammo = minimum(g_pData->m_Weapons.m_aId[Weapon].m_Maxammo, Ammo);
		return true;
	}
	return false;
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
	if(m_LockedCK && m_Input.m_Jump)
		m_LockedCK = false;

	if(m_LockedCK)
	{
		m_Input.m_Jump = 0;
		m_Input.m_Direction = 0;
		m_Input.m_Hook = 0;
		m_Core.m_HookState = HOOK_IDLE;
	}

	m_Core.m_Input = m_Input;
	m_Core.Tick(true);

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

	if(GameServer()->m_pController->IsSpiderBossCore(this))
	{
		m_Input.m_Direction = 0;
		m_Input.m_Jump = 0;
		m_Input.m_Hook = 0;
		m_Core.m_HookState = HOOK_IDLE;
		m_Core.m_Vel = vec2(0.0f, 0.0f);
	}

	if(Config()->m_SvContentFramework && GameServer()->Core() && GameServer()->Core()->StatusManager())
		GameServer()->Core()->StatusManager()->TickCharacter(this);
	else if(m_CardElectronTicks > 0)
	{
		m_Core.m_Vel *= 0.86f;
		m_CardElectronTicks--;
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
}

void CCharacter::TickDefered()
{
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

	if(!GameServer()->m_pController->IsSpiderBossCore(this))
		m_Core.Move();

	bool StuckAfterMove = GameServer()->Collision()->TestBox(m_Core.m_Pos, ColBox);
	m_Core.Quantize();
	bool StuckAfterQuant = GameServer()->Collision()->TestBox(m_Core.m_Pos, ColBox);
	m_Pos = m_Core.m_Pos;

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
	m_Health = clamp(m_Health + Amount, 0, m_MaxHealth);
	return true;
}

void CCharacter::AddMaxHealth(int Amount)
{
	m_Health += Amount;
	m_MaxHealth += Amount;
}

void CCharacter::SetHealthDirect(int Amount)
{
	const int Max = GameServer()->Config()->m_SvPlayerMaxHealth;
	m_Health = clamp(Amount, 0, Max);
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

	if(m_pPlayer->m_ZamerDetonating)
		return false;

	if(GameServer()->m_pController->IsSpiderBossCore(this) && From == m_pPlayer->GetCID())
		return false;

	m_Core.m_Vel += Force;

	if(From >= 0)
	{
		if(GameServer()->m_pController->IsFriendlyFire(m_pPlayer->GetCID(), From, Dmg))
			return false;
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

	if(Config()->m_SvContentFramework && GameServer()->Core() && GameServer()->Core()->StatusManager())
		GameServer()->Core()->StatusManager()->AbsorbDamage(this, Dmg);

	// Apply armor defense reduction
	if(Dmg > 0 && m_pPlayer && !m_pPlayer->IsDummy() && GameServer()->ItemHelper())
	{
		const int HelmetId = m_pPlayer->GetHolding(ITYPE_HELMET);
		const int ChestId = m_pPlayer->GetHolding(ITYPE_CHEST);
		const int LegsId = m_pPlayer->GetHolding(ITYPE_LEGS);
		const int Defense = GameServer()->ItemHelper()->GetDefense(HelmetId)
			+ GameServer()->ItemHelper()->GetDefense(ChestId)
			+ GameServer()->ItemHelper()->GetDefense(LegsId);
		if(Defense > 0)
		{
			Dmg = maximum(1, Dmg - Defense);
		}
	}

	int OldHealth = m_Health, OldArmor = m_Armor;
	if(Dmg)
	{
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
	const int SnapID = GameServer()->ClientSnapID(SnappingClient, m_pPlayer->GetCID());
	if(SnapID < 0)
		return;

	CNetObj_Character *pCharacter = static_cast<CNetObj_Character *>(Server()->SnapNewItem(NETOBJTYPE_CHARACTER, SnapID, sizeof(CNetObj_Character)));
	if(!pCharacter)
		return;

	// write down the m_Core
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

	if(SnappingClient >= 0 && !GameServer()->ClientUsesExtendedSlots(SnappingClient) && pCharacter->m_HookedPlayer >= MAX_HUMAN_CLIENTS)
	{
		const int HookedSnap = GameServer()->ClientSnapID(SnappingClient, pCharacter->m_HookedPlayer);
		pCharacter->m_HookedPlayer = HookedSnap >= 0 ? HookedSnap : -1;
	}

	pCharacter->m_Emote = m_EmoteType;

	pCharacter->m_AmmoCount = 0;
	pCharacter->m_Health = 0;
	pCharacter->m_Armor = 0;
	pCharacter->m_TriggeredEvents = m_TriggeredEvents;

	pCharacter->m_Weapon = m_ActiveWeapon;
	pCharacter->m_AttackTick = m_AttackTick;

	pCharacter->m_Direction = m_Input.m_Direction;

	if(m_pPlayer->GetCID() == SnappingClient || SnappingClient == -1 ||
		(!Config()->m_SvStrictSpectateMode && m_pPlayer->GetCID() == GameServer()->m_apPlayers[SnappingClient]->GetSpectatorID()))
	{
		pCharacter->m_Health = m_Health;
		pCharacter->m_Armor = m_Armor;
		if(m_ActiveWeapon == WEAPON_NINJA)
			pCharacter->m_AmmoCount = m_Ninja.m_ActivationTick + g_pData->m_Weapons.m_Ninja.m_Duration * Server()->TickSpeed() / 1000;
		else if(m_aWeapons[m_ActiveWeapon].m_Ammo > 0)
			pCharacter->m_AmmoCount = m_aWeapons[m_ActiveWeapon].m_Ammo;
	}

	if(pCharacter->m_Emote == EMOTE_NORMAL)
	{
		if(5 * Server()->TickSpeed() - ((Server()->Tick() - m_LastAction) % (5 * Server()->TickSpeed())) < 5)
			pCharacter->m_Emote = EMOTE_BLINK;
	}
}

void CCharacter::PostSnap()
{
	m_TriggeredEvents = 0;
}
