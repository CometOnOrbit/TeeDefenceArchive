#include "profession_data.h"
#include <cstring>
#include <base/system.h>

SProfessionDef CProfessionData::m_aDefs[] = {};
static bool s_DefsInitialized = false;

void CProfessionData::InitDefs()
{
	if(s_DefsInitialized)
		return;
	s_DefsInitialized = true;

	m_aDefs[static_cast<int>(PROF_WARRIOR)] = {
		PROF_WARRIOR, "Warrior", "A strong melee fighter. High HP and defense.",
		15, 8.0f, 2.0f, 1.5f, 3.0f, true
	};
	m_aDefs[static_cast<int>(PROF_MAGE)] = {
		PROF_MAGE, "Mage", "A powerful spellcaster. High attack, low defense.",
		8, 4.0f, 3.5f, 0.5f, 8.0f, true
	};
	m_aDefs[static_cast<int>(PROF_ARCHER)] = {
		PROF_ARCHER, "Archer", "A ranged precision fighter. Balanced stats.",
		10, 5.0f, 2.5f, 1.0f, 5.0f, true
	};
	m_aDefs[static_cast<int>(PROF_ASSASSIN)] = {
		PROF_ASSASSIN, "Assassin", "A stealthy close-combat specialist. High burst damage.",
		10, 5.0f, 3.0f, 0.8f, 4.0f, true
	};
	m_aDefs[static_cast<int>(PROF_HEALER)] = {
		PROF_HEALER, "Healer", "A support class. Good mana and balanced defense.",
		12, 6.0f, 1.5f, 1.2f, 7.0f, true
	};
	m_aDefs[static_cast<int>(PROF_FARMER)] = {
		PROF_FARMER, "Farmer", "A gathering profession. Extra yields from herbs and plants.",
		10, 4.0f, 0.5f, 0.5f, 2.0f, false
	};
	m_aDefs[static_cast<int>(PROF_MINER)] = {
		PROF_MINER, "Miner", "A gathering profession. Extra yields from ores and gems.",
		12, 5.0f, 0.5f, 0.8f, 2.0f, false
	};
	m_aDefs[static_cast<int>(PROF_FISHERMAN)] = {
		PROF_FISHERMAN, "Fisherman", "A gathering profession. Better fishing yields.",
		10, 4.0f, 0.3f, 0.3f, 2.0f, false
	};
}

const SProfessionDef *CProfessionData::GetDef(EProfession Type)
{
	int idx = static_cast<int>(Type);
	if(idx < 0 || idx >= static_cast<int>(PROF_NUM))
		return 0;
	InitDefs();
	return &m_aDefs[idx];
}

const SProfessionDef *CProfessionData::GetDef(int Type)
{
	return GetDef(static_cast<EProfession>(Type));
}

const char *CProfessionData::GetName(EProfession Type)
{
	const SProfessionDef *pDef = GetDef(Type);
	return pDef ? pDef->m_pName : "None";
}

bool CProfessionData::IsCombat(EProfession Type)
{
	const SProfessionDef *pDef = GetDef(Type);
	return pDef && pDef->m_bCombatClass;
}

EProfession CProfessionData::FindByName(const char *pName)
{
	if(!pName || !pName[0])
		return PROF_NONE;

	InitDefs();
	for(int i = 0; i < static_cast<int>(PROF_NUM); i++)
	{
		if(str_comp(m_aDefs[i].m_pName, pName) == 0)
			return static_cast<EProfession>(i);
	}
	return PROF_NONE;
}
