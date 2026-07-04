#include "mmo_item.h"
#include "mmo_types.h"
#include <base/math.h>
#include <generated/server_data.h>
#include <generated/protocol.h>

static AttributeIdentifier DamageAttrForEngineWeapon(int EngineWeapon)
{
	switch(EngineWeapon)
	{
	case WEAPON_HAMMER: return AttributeIdentifier::HammerDMG;
	case WEAPON_GUN: return AttributeIdentifier::GunDMG;
	case WEAPON_SHOTGUN: return AttributeIdentifier::ShotgunDMG;
	case WEAPON_GRENADE: return AttributeIdentifier::GrenadeDMG;
	case WEAPON_LASER: return AttributeIdentifier::RifleDMG;
	default: return AttributeIdentifier::Unknown;
	}
}

int CMMOItemDescription::GetEngineWeaponDamageBonus(int EngineWeapon, int Enchant) const
{
	const AttributeIdentifier Attr = DamageAttrForEngineWeapon(EngineWeapon);
	if(Attr == AttributeIdentifier::Unknown)
		return 0;
	return GetAttributeValue(Attr, Enchant);
}

int CMMOItemDescription::GetAttackSpeedPercent(int Enchant) const
{
	const int Spd = GetAttributeValue(AttributeIdentifier::AttackSPD, Enchant);
	return Spd > 0 ? Spd : 100;
}

// ─── CMMOInventory ───────────────────────────────────────────────────

bool CMMOInventory::Add(const CMMOItem& Item)
{
	if(!Item.IsValid()) return false;

	// Try to stack with existing item
	if(Item.m_Enchant == 0 && Item.m_Flags == 0) // only basic items stack
	{
		for(auto& Existing : m_aItems)
		{
			if(Existing.CanStackWith(Item) && Existing.m_Count < MMO_INVENTORY_MAX_STACK)
			{
				int Room = MMO_INVENTORY_MAX_STACK - Existing.m_Count;
				int ToAdd = minimum(Item.m_Count, Room);
				Existing.m_Count += ToAdd;
				// If we couldn't add all, the caller handles remainder
				return true;
			}
		}
	}

	m_aItems.push_back(Item);
	return true;
}

bool CMMOInventory::Add(int ItemID, int Count, int Enchant, int Durability, time_t ExpiresAt)
{
	if(Count <= 0) return false;
	return Add(CMMOItem(ItemID, Count, Enchant, Durability, ExpiresAt));
}

bool CMMOInventory::RemoveAt(int Index, int Count)
{
	if(Index < 0 || Index >= (int)m_aItems.size()) return false;
	if(Count <= 0) return false;

	auto& Item = m_aItems[Index];
	if(Count >= Item.m_Count)
	{
		// Remove entire slot
		m_aItems.erase(m_aItems.begin() + Index);
	}
	else
	{
		Item.m_Count -= Count;
	}
	return true;
}

bool CMMOInventory::RemoveByID(int ItemID, int Count)
{
	if(Count <= 0) return false;

	for(int i = 0; i < (int)m_aItems.size(); i++)
	{
		if(m_aItems[i].m_ItemID == ItemID && m_aItems[i].m_Enchant == 0)
		{
			int RemoveNow = minimum(Count, m_aItems[i].m_Count);
			if(RemoveNow >= m_aItems[i].m_Count)
			{
				m_aItems.erase(m_aItems.begin() + i);
				Count -= RemoveNow;
			}
			else
			{
				m_aItems[i].m_Count -= RemoveNow;
				Count -= RemoveNow;
			}
			if(Count <= 0) return true;
		}
	}
	return Count <= 0;
}

int CMMOInventory::FindByID(int ItemID) const
{
	for(int i = 0; i < (int)m_aItems.size(); i++)
		if(m_aItems[i].m_ItemID == ItemID) return i;
	return -1;
}

int CMMOInventory::CountByID(int ItemID) const
{
	int Total = 0;
	for(const auto& Item : m_aItems)
		if(Item.m_ItemID == ItemID) Total += Item.m_Count;
	return Total;
}

void CMMOInventory::RemoveExpired()
{
	auto it = m_aItems.begin();
	while(it != m_aItems.end())
	{
		if(it->IsExpired())
			it = m_aItems.erase(it);
		else
			++it;
	}
}
