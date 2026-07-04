#include "equipped_slots.h"

void EquippedSlots::initSlot(ItemType type, int itemID)
{
	// Remove existing entry for this type if any
	for(auto it = m_Slots.begin(); it != m_Slots.end(); ++it)
	{
		if(it->first == type)
		{
			it->second = itemID;
			return;
		}
	}
	// Add new entry
	m_Slots.emplace_back(type, itemID);
}

bool EquippedSlots::equipSlot(ItemType type, int itemID)
{
	if(itemID < 0) return false;
	for(auto &slot : m_Slots)
	{
		if(slot.first == type)
		{
			slot.second = itemID;
			return true;
		}
	}
	m_Slots.emplace_back(type, itemID);
	return true;
}

bool EquippedSlots::unequipSlot(ItemType type)
{
	for(auto it = m_Slots.begin(); it != m_Slots.end(); ++it)
	{
		if(it->first == type)
		{
			m_Slots.erase(it);
			return true;
		}
	}
	return false;
}

int EquippedSlots::getSlot(ItemType type) const
{
	for(const auto &slot : m_Slots)
	{
		if(slot.first == type)
			return slot.second;
	}
	return -1;
}

bool EquippedSlots::isEquippedItem(int itemID) const
{
	for(const auto &slot : m_Slots)
	{
		if(slot.second == itemID)
			return true;
	}
	return false;
}
