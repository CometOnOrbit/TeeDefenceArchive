#include <engine/server.h>
#include <engine/shared/protocol.h>
#include <generated/protocol.h>
#include "input_events.h"

// TDA protocol flags (PLAYERFLAG_* from generated/protocol.h):
//   PLAYERFLAG_CHATTING (1<<1), PLAYERFLAG_SCOREBOARD (1<<2)
//   No PLAYERFLAG_IN_MENU available yet

// CountInput is duplicated from character.cpp since it's not in a shared header
// (In MRPG it lives in game/gamecore.h)
struct CInputCount
{
	int m_Presses;
	int m_Releases;
};

static CInputCount CountInput(int Prev, int Cur)
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

void CInputEvents::ParseInputClickedKeys(int ClientID, const CNetObj_PlayerInput *pNewInput, const CNetObj_PlayerInput *pLastInput)
{
	// Detect edge transitions on player flags (MRPG-style)
	if(pNewInput->m_PlayerFlags & PLAYERFLAG_CHATTING && !(pLastInput->m_PlayerFlags & PLAYERFLAG_CHATTING))
		AppendEventKeyClick(ClientID, KEY_EVENT_CHAT);

	if(pNewInput->m_PlayerFlags & PLAYERFLAG_SCOREBOARD && !(pLastInput->m_PlayerFlags & PLAYERFLAG_SCOREBOARD))
		AppendEventKeyClick(ClientID, KEY_EVENT_SCOREBOARD);

	// Detect jump/hook edge transitions
	if(pNewInput->m_Jump && !pLastInput->m_Jump)
		AppendEventKeyClick(ClientID, KEY_EVENT_JUMP);

	if(pNewInput->m_Hook && !pLastInput->m_Hook)
		AppendEventKeyClick(ClientID, KEY_EVENT_HOOK);

	// PLAYERFLAG_IN_MENU not available in TDA protocol yet
}

void CInputEvents::ProcessKeyPress(int ClientID, int LastInput, int NewInput, int EventKey, int ActiveWeaponKey)
{
	if(CountInput(LastInput, NewInput).m_Presses)
	{
		AppendEventKeyClick(ClientID, EventKey);
		if(ActiveWeaponKey != -1)
			AppendEventKeyClick(ClientID, ActiveWeaponKey);
	}
}

void CInputEvents::ProcessCharacterInput(int ClientID, int ActiveWeapon,
	const CNetObj_PlayerInput *pNewInput, const CNetObj_PlayerInput *pLastInput)
{
	ProcessKeyPress(ClientID, pLastInput->m_Fire, pNewInput->m_Fire, KEY_EVENT_FIRE,
		1 << (KEY_EVENT_FIRE + ActiveWeapon));
	ProcessKeyPress(ClientID, pLastInput->m_NextWeapon, pNewInput->m_NextWeapon, KEY_EVENT_NEXT_WEAPON);
	ProcessKeyPress(ClientID, pLastInput->m_PrevWeapon, pNewInput->m_PrevWeapon, KEY_EVENT_PREV_WEAPON);

	if(pLastInput->m_WantedWeapon != pNewInput->m_WantedWeapon)
	{
		AppendEventKeyClick(ClientID, KEY_EVENT_WANTED_WEAPON);
		AppendEventKeyClick(ClientID, KEY_EVENT_WANTED_WEAPON << (pNewInput->m_WantedWeapon));
	}
}

void CInputEvents::AppendEventKeyClick(int ClientID, int KeyID)
{
	if(ClientID >= 0 && ClientID < MAX_CLIENTS)
		m_aActionEventKeys[ClientID] |= KeyID;
}

bool CInputEvents::IsKeyClicked(int ClientID, int KeyID)
{
	return (ClientID >= 0 && ClientID < MAX_CLIENTS) ? (m_aActionEventKeys[ClientID] & KeyID) != 0 : false;
}

void CInputEvents::BlockInputGroup(int ClientID, int64_t FlagBlockedGroup)
{
	if(ClientID >= 0 && ClientID < MAX_CLIENTS)
		m_aBlockedInputKeys[ClientID] |= FlagBlockedGroup;
}

bool CInputEvents::IsBlockedInputGroup(int ClientID, int64_t FlagBlockedGroup)
{
	return (ClientID >= 0 && ClientID < MAX_CLIENTS) ? (m_aBlockedInputKeys[ClientID] & FlagBlockedGroup) != 0 : false;
}

CInputEvents *CreateInputKeys() { return new CInputEvents; }
