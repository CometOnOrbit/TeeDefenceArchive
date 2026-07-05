#include "mob_combat.h"

#include <game/server/entities/character_bot_ai.h>
#include <game/server/core/components/mmo/mmo_types.h>
#include <game/server/core/components/mmo/mmo_item.h>
#include <game/server/data_center.h>

float SMMOMobDef::GetPreferredCombatRange() const
{
	return MobPreferredCombatRange(this, -1);
}

float MobPreferredCombatRange(const SMMOMobDef *pDef, int ActiveWeapon)
{
	if(!pDef)
		return 300.f;
	if(pDef->m_PreferredRange > 1.f)
		return pDef->m_PreferredRange;

	switch(pDef->m_Archetype)
	{
	case MOB_ARCHETYPE_MELEE: return 72.f;
	case MOB_ARCHETYPE_RANGED: return 350.f;
	case MOB_ARCHETYPE_CASTER: return 420.f;
	case MOB_ARCHETYPE_TANK: return 80.f;
	default: break;
	}

	if(ActiveWeapon >= 0)
	{
		switch(ActiveWeapon)
		{
		case WEAPON_HAMMER: return 64.f;
		case WEAPON_GUN: return 300.f;
		case WEAPON_SHOTGUN: return 400.f;
		case WEAPON_GRENADE: return 500.f;
		case WEAPON_LASER: return 600.f;
		default: break;
		}
	}

	for(const SMMOMobWeaponEntry &W : pDef->m_vWeapons)
	{
		if(W.m_Primary)
			return MobPreferredCombatRange(pDef, W.m_WeaponId);
	}
	return 300.f;
}

void ApplyMobCombatLoadout(CCharacterBotAI *pChr, const SMMOMobDef *pDef)
{
	if(!pChr || !pDef)
		return;

	if(!pDef->m_vWeapons.empty())
	{
		int PrimaryWeapon = WEAPON_HAMMER;
		for(const SMMOMobWeaponEntry &W : pDef->m_vWeapons)
		{
			const int WeaponId = clamp(W.m_WeaponId, (int)WEAPON_HAMMER, (int)WEAPON_LASER);
			pChr->GiveWeapon(WeaponId, W.m_Ammo);
			if(W.m_Primary)
				PrimaryWeapon = WeaponId;
		}
		pChr->SetWeapon(PrimaryWeapon);
	}
	else
	{
		pChr->GiveWeapon(WEAPON_HAMMER, -1);
		pChr->GiveWeapon(WEAPON_GUN, 10);
		pChr->SetWeapon(WEAPON_HAMMER);
	}

	if(pDef->m_WeaponItemId > 0)
	{
		const CMMOItemDescription *pItem = CMMOItemDescription::Get(pDef->m_WeaponItemId);
		if(pItem)
		{
			const int EngineWeapon = MMOItemTypeToWeapon(pItem->GetType());
			if(EngineWeapon >= 0)
			{
				pChr->GiveWeapon(EngineWeapon, -1);
				pChr->SetWeapon(EngineWeapon);
			}
			pChr->m_ActiveWeaponItemID = pDef->m_WeaponItemId;
		}
	}

	pChr->m_IntervalChangeWeapon = 1 << 30;
}
