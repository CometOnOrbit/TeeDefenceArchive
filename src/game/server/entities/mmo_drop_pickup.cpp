#include "mmo_drop_pickup.h"

#include <game/collision.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <generated/server_data.h>

CEntityDropPickup::CEntityDropPickup(CGameWorld *pGameWorld, vec2 Pos, vec2 Vel, int Kind, int Subtype, int Value, int OwnerClientID)
	: CEntity(pGameWorld, CGameWorld::ENTTYPE_PICKUP, 0, Pos, (float)PickupPhysSize)
{
	m_Vel = Vel;
	m_Kind = Kind;
	m_Subtype = Subtype;
	m_Value = Value;
	m_OwnerClientID = OwnerClientID;
	m_LifeSpan = Server()->TickSpeed() * 15;

	GameWorld()->InsertEntity(this);
}

static int SnapTypeForKind(int Kind, int Subtype)
{
	switch(Kind)
	{
	case MOBDROP_EXP: return PICKUP_ARMOR;
	case MOBDROP_HEALTH: return PICKUP_HEALTH;
	case MOBDROP_MANA: return PICKUP_LASER;
	case MOBDROP_AMMO:
		switch(Subtype)
		{
		case WEAPON_GUN: return PICKUP_GUN;
		case WEAPON_SHOTGUN: return PICKUP_SHOTGUN;
		case WEAPON_GRENADE: return PICKUP_GRENADE;
		case WEAPON_LASER: return PICKUP_LASER;
		default: return PICKUP_GUN;
		}
	default: return PICKUP_ARMOR;
	}
}

void CEntityDropPickup::Tick()
{
	m_LifeSpan--;
	if(m_LifeSpan < 0)
	{
		GameWorld()->CreatePlayerSpawn(m_Pos);
		GameWorld()->DestroyEntity(this);
		return;
	}

	GameServer()->Collision()->MovePhysicalBox(&m_Pos, &m_Vel, vec2(GetProximityRadius(), GetProximityRadius()), 0.5f);

	CCharacter *pChr = (CCharacter *)GameWorld()->ClosestEntity(m_Pos, GetProximityRadius(), CGameWorld::ENTTYPE_CHARACTER, nullptr);
	if(!pChr || !pChr->IsAlive() || !pChr->GetPlayer())
		return;

	CPlayer *pPlayer = pChr->GetPlayer();
	if(pPlayer->m_pMMOBotData || pPlayer->IsDummy())
		return;

	if(m_OwnerClientID >= 0 && pPlayer->GetCID() != m_OwnerClientID)
		return;

	CGameContext *pGS = GameServer();
	const int ClientID = pPlayer->GetCID();

	switch(m_Kind)
	{
	case MOBDROP_EXP:
		if(m_Value > 0)
		{
			pPlayer->AddMMOExperience(m_Value);
			pGS->SendChatLocF(ClientID, "mob_kill.pickup_exp", "+%d EXP", m_Value);
		}
		GameWorld()->CreateSound(m_Pos, SOUND_PICKUP_ARMOR);
		break;

	case MOBDROP_HEALTH:
		if(pChr->IncreaseHealth(m_Value > 0 ? m_Value : 1))
			GameWorld()->CreateSound(m_Pos, SOUND_PICKUP_HEALTH);
		break;

	case MOBDROP_MANA:
		if(pChr->IncreaseMana(m_Value))
		{
			GameWorld()->CreateSound(m_Pos, SOUND_PICKUP_ARMOR);
			pGS->SendChatLocF(ClientID, "mob_kill.pickup_mana", "+%d mana", m_Value);
		}
		break;

	case MOBDROP_AMMO:
	{
		const int Weapon = m_Subtype;
		if(Weapon < WEAPON_GUN || Weapon > WEAPON_LASER)
			break;

		if(!pChr->GiveWeapon(Weapon, m_Value))
			pChr->AddWeaponAmmo(Weapon, m_Value);

		GameWorld()->CreateSound(m_Pos, SOUND_PICKUP_SHOTGUN);
		pGS->SendChatLocF(ClientID, "mob_kill.pickup_ammo", "+%d ammo", m_Value);
	}
	break;

	default:
		return;
	}

	GameWorld()->DestroyEntity(this);
}

void CEntityDropPickup::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CNetObj_Pickup *pP = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, GetID(), sizeof(CNetObj_Pickup)));
	if(!pP)
		return;

	pP->m_X = round_to_int(m_Pos.x);
	pP->m_Y = round_to_int(m_Pos.y);
	pP->m_Type = SnapTypeForKind(m_Kind, m_Subtype);
}
