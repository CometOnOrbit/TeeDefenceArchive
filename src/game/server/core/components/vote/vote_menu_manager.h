#ifndef GAME_SERVER_COMPONENT_VOTE_MENU_MANAGER_H
#define GAME_SERVER_COMPONENT_VOTE_MENU_MANAGER_H

#include <engine/shared/protocol.h>

#include <game/server/core/components/vote/vote_menu_types.h>
#include <game/server/core/tworld_component.h>

class CCommandManager;

class CVoteMenuManager : public TWorldComponent
{
	SPlayerVote m_aPlayerVotes[MAX_CLIENTS];
	int m_VoteBuildClientID = -1;

public:
	SPlayerVote *GetPlayerVote(int ClientID);
	void SetVoteBuildClientID(int CID) { m_VoteBuildClientID = CID; }

	void AddVote(const char *pDesc, const char *pCmd, int ClientID);
	void AddVote_ListInventory(int ItemType, const char *pCmdPrefix, bool Equip = false);
	void AddVote_ListCraft(int ItemType);
	void AddVote_ListFormula(int ItemID);
	void AddVote_Craft(int ItemID);
	void AddVote_Back();
	void AddVote_Space(int Num = 1);
	void AddVote_Goto(int Page, const char *pDesc);
	void AddVote_TextLine(const char *pText);
	void SetVoteLastPage(int Page);

	void AddVote_PageHeader(const char *pTitle);
	void AddVote_PageSubtitle(const char *pText);
	void AddVote_Separator();
	void AddVote_Section(const char *pLabel);
	void AddVote_ProgressLine(int Current, int Max);
	void AddVote_PageFooter();
	void AddVote_EmptyHint(const char *pText);

	void InitVotes(int ClientID);
	void ClearVotes(int ClientID);
	void CountItemNum(int ClientID);
	bool TryHandleVoteMenuOption(int ClientID, const char *pDescription);
	void ProcessVoteMenuCommand(int ClientID, const char *pCmdLine);

	void RegisterChatCommands(CCommandManager *pManager);
};

#endif
