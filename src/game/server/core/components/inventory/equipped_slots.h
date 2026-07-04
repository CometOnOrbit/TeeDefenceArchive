/* Adapted from Teeworlds-MRPG-0.6 for TeeDefenceArchive */
#ifndef GAME_SERVER_CORE_COMPONENTS_INVENTORY_EQUIPPED_SLOTS_H
#define GAME_SERVER_CORE_COMPONENTS_INVENTORY_EQUIPPED_SLOTS_H

#include <game/server/core/mmo_context.h>
#include <vector>
#include <utility>

class EquippedSlots
{
public:
	using SlotEntry = std::pair<ItemType, int>; // type → item ID (-1 = empty)

	EquippedSlots() = default;

	void initSlot(ItemType type, int itemID = -1);
	bool equipSlot(ItemType type, int itemID);
	bool unequipSlot(ItemType type);
	int getSlot(ItemType type) const;
	bool isEquipped(ItemType type) const { return getSlot(type) >= 0; }
	bool isEquippedItem(int itemID) const;

	const std::vector<SlotEntry>& getSlots() const { return m_Slots; }
	void clear() { m_Slots.clear(); }

	// Aliases used by mmo_manager.cpp
	void SetSlotForType(int type, int itemID) { equipSlot(static_cast<ItemType>(type), itemID); }
	void ClearSlotForType(int type) { unequipSlot(static_cast<ItemType>(type)); }

private:
	std::vector<SlotEntry> m_Slots{};
};

#endif
