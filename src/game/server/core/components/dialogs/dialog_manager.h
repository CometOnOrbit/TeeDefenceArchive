#ifndef GAME_SERVER_CORE_COMPONENTS_DIALOGS_MANAGER_H
#define GAME_SERVER_CORE_COMPONENTS_DIALOGS_MANAGER_H

#include <game/server/core/tworld_component.h>

#include "dialog_data.h"
#include "mrpg_dialog.h"

class CPlayer;

// ── Per-session dialog state for a player ────────────────────
struct SPlayerDialogSession
{
	bool m_Active = false;
	float m_NpcPosX = 0;
	float m_NpcPosY = 0;
	CPlayerDialog m_MrpgDialog;

	void Reset()
	{
		m_Active = false;
		m_NpcPosX = m_NpcPosY = 0;
		m_MrpgDialog.End();
	}
};

// ── Dialog manager component (B-1: MRPG MOTD dialog) ────────
class CDialogManager : public TWorldComponent
{
	enum { MAX_NPC_DIALOGS = 16 };

	SNpcDialogDef m_aNpcDialogs[MAX_NPC_DIALOGS];
	int m_NumNpcDialogs = 0;
	SPlayerDialogSession m_aSessions[MAX_CLIENTS];

public:
	CDialogManager();

	void OnInitWorld(const char *pWhereLocalWorld) override;
	void OnTick() override;

	// MRPG-style hammer conversation
	bool TryTalk(class CPlayer *pPlayer, const char *pNpcId, float NpcPosX = 0, float NpcPosY = 0, int NpcWorld = -1, int NpcClientID = -1);
	bool HasActiveDialog(int ClientID) const { return ClientID >= 0 && ClientID < MAX_CLIENTS && m_aSessions[ClientID].m_Active; }

	// MRPG-style: /dialog next / cancel
	bool HandleDialogCommand(CPlayer *pPlayer, const char *pArgs);

	// F3/F4 vote input — called immediately from CL_VOTE handler
	bool HandleVoteInput(CPlayer *pPlayer, int Vote);

	// MotdMenu stub (B-1 doesn't use MotdMenu for dialog)
	bool HandleMotdMenuCommand(CPlayer *pPlayer, const char *pCommand);

	void ResetSession(int ClientID);
	const SNpcDialogDef *FindDialogDef(const char *pNpcId) const;
	void RegisterChatCommands(class CCommandManager *pManager);

private:
	void LoadDialogs();
	void ExecuteActions(CPlayer *pPlayer, const CDialogStep &Step);
};

#endif
