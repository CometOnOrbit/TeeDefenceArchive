#include "dialog_data.h"

#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/entities/character.h>

// ══════════════════════════════════════════════════════════════
//  CDialogCondition::Evaluate
// ══════════════════════════════════════════════════════════════

bool CDialogCondition::Evaluate(CPlayer *pPlayer, CGameContext *pGS) const
{
	if(!pPlayer || !pGS) return true;

	switch(m_Type)
	{
	case EDialogCondType::NONE:
		return true;

	case EDialogCondType::FLAG:
		return pPlayer->HasStoryFlag(m_aKey);

	case EDialogCondType::NOT_FLAG:
		return !pPlayer->HasStoryFlag(m_aKey);

	case EDialogCondType::HAS_ITEM:
	case EDialogCondType::NOT_ITEM:
	case EDialogCondType::LEVEL_GE:
	case EDialogCondType::LEVEL_EQ:
	case EDialogCondType::REPUTATION_GE:
	case EDialogCondType::REPUTATION_LT:
	case EDialogCondType::GOLD_GE:
	case EDialogCondType::QUEST_DONE:
	case EDialogCondType::QUEST_ACTIVE:
	case EDialogCondType::CHANCE:
	case EDialogCondType::CUSTOM:
		return true; // Placeholder — Phase B/C
	}
	return true;
}

// ══════════════════════════════════════════════════════════════
//  CDialogAction::Execute
// ══════════════════════════════════════════════════════════════

void CDialogAction::Execute(CPlayer *pPlayer, CGameContext *pGS) const
{
	if(!pPlayer || !pGS) return;
	const int CID = pPlayer->GetCID();

	switch(m_Type)
	{
	case EDialogActionType::NONE:
		return;

	case EDialogActionType::SET_FLAG:
		pPlayer->SetStoryFlag(m_aKey, m_Value);
		break;

	case EDialogActionType::CLEAR_FLAG:
		pPlayer->SetStoryFlag(m_aKey, 0);
		break;

	case EDialogActionType::ADD_REPUTATION:
		pPlayer->SetStat(AttributeIdentifier::Reputation,
			pPlayer->GetStat(AttributeIdentifier::Reputation) + m_Value);
		break;

	case EDialogActionType::ADD_GOLD:
		pPlayer->SetStat(AttributeIdentifier::Gold,
			pPlayer->GetStat(AttributeIdentifier::Gold) + m_Value);
		break;

	case EDialogActionType::REMOVE_GOLD:
		pPlayer->SetStat(AttributeIdentifier::Gold,
			maximum(0, pPlayer->GetStat(AttributeIdentifier::Gold) - m_Value));
		break;

	case EDialogActionType::ADD_ITEM:
	case EDialogActionType::REMOVE_ITEM:
		// Phase B: inventory system
		break;

	case EDialogActionType::COMPLETE_QUEST:
	case EDialogActionType::START_QUEST:
		// Phase B: quest system
		break;

	case EDialogActionType::HEAL:
		if(pPlayer->GetCharacter())
		{
			pPlayer->GetCharacter()->IncreaseHealth(m_Value);
			pPlayer->GetCharacter()->IncreaseArmor(m_Value);
		}
		break;

	case EDialogActionType::TELEPORT:
		// Phase C: teleport to world
		break;

	case EDialogActionType::OPEN_SHOP:
	case EDialogActionType::OPEN_SKILLS:
	case EDialogActionType::OPEN_CRAFT:
		// Handled at higher level by dialog_manager::TryTalk
		break;

	case EDialogActionType::MESSAGE:
		if(m_aStrValue[0])
			pGS->SendChatTo(CID, m_aStrValue);
		break;

	case EDialogActionType::SET_LEVEL:
		pPlayer->SetStat(AttributeIdentifier::Level, m_Value);
		break;

	case EDialogActionType::SET_ATTRIBUTE:
		// Set attribute by key
		if(m_aKey[0])
		{
			// Phase C: parse attribute name to enum
		}
		break;

	case EDialogActionType::CUSTOM:
		// Extensible — Phase C
		break;
	}
}
