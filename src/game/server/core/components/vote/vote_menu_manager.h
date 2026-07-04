#ifndef GAME_SERVER_COMPONENT_VOTE_MENU_MANAGER_H
#define GAME_SERVER_COMPONENT_VOTE_MENU_MANAGER_H

#include <engine/shared/protocol.h>
#include <engine/shared/jsonparser.h>

#include <game/server/core/components/vote/vote_menu_types.h>
#include <game/server/core/tworld_component.h>

class CGameContext;
class CPlayer;
class CCommandManager;
struct STurretAmmoMix;

class CVoteMenuManager : public TWorldComponent
{
	SPlayerVote m_aPlayerVotes[MAX_CLIENTS];
	int m_VoteBuildClientID = -1;

public:
	// Static helpers for vote page building
	static const char *VL(CGameContext *pCtx, CPlayer *pP, const char *pKey, const char *pDefault);
	static const char *ItemTypeLoc(CGameContext *pCtx, CPlayer *pP, int ItemType);
	static const char *TurretMatLoc(CGameContext *pCtx, CPlayer *pP, int Mat);
	static const char *TurretMatShort(CGameContext *pCtx, CPlayer *pP, int Mat);
	static void TurretAmmoSummaryLines(CGameContext *pCtx, CPlayer *pP, const STurretAmmoMix &Mix, char *pLine0, int Len0, char *pLine1, int Len1);
	static void FormatProgressBar(int Current, int Max, char *pOut, int OutSize);
	static void AddVoteWrappedText(CVoteMenuManager *pVote, const char *pText);
	static void AddVoteItemDesc(CVoteMenuManager *pVote, CGameContext *pCtx, CPlayer *pP, int ItemId);
	static void AddVoteEmbeddedItems(CVoteMenuManager *pVote, CGameContext *pCtx, CPlayer *pP, int HostId);
	static void AddVotePlaceableTypes(CVoteMenuManager *pVote, CGameContext *pCtx, CPlayer *pP, int ItemId);
	static void AddVotesHostCardManage(CGameContext *pCtx, CVoteMenuManager *pVote, int VoteClient, CPlayer *pP, int HostId);
	static int AddVotesSeparateFromHostExtra(CGameContext *pCtx, CVoteMenuManager *pVote, CPlayer *pP, int VoteClient, int HostId, const json_value &Extra);

	SPlayerVote *GetPlayerVote(int ClientID);
	void SetVoteBuildClientID(int CID) { m_VoteBuildClientID = CID; }

	void AddVote(const char *pDesc, const char *pCmd, int ClientID);
	// MRPG-style formatting: plain text vs selectable (▾ prefix)
	void AddVote_Info(const char *pText);
	void AddVote_Option(const char *pDesc, const char *pCmd);
	void AddVote_GroupTitle(const char *pTitle);
	void AddVote_GroupLine();
	void AddVote_Footer();
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
	// Decorated variants – automatically apply MRPG-style prefixes
	void AddVote_BulletItem(const char *pDesc, const char *pCmd);
	void AddVote_ItemEntry(const char *pDesc, const char *pCmd);

	void InitVotes(int ClientID);
	bool BuildMenuPage(int ClientID, int Page);
	void ClearVotes(int ClientID);
	void ClearVoteOptions(int ClientID); // clear + send msg only, no InitVotes (safe for page builders)
	void CountItemNum(int ClientID);
	bool TryHandleVoteMenuOption(int ClientID, const char *pDescription, int ReasonNumber, const char *pReason);
	void ProcessVoteMenuCommand(int ClientID, const char *pCmdLine, int ReasonNumber, const char *pReason);

	void RegisterChatCommands(CCommandManager *pManager);
};

#endif
