#ifndef GAME_SERVER_CORE_COMPONENTS_VOTE_VOTE_WRAPPER_H
#define GAME_SERVER_CORE_COMPONENTS_VOTE_VOTE_WRAPPER_H

#include <game/voting.h>

class CGameContext;
class CPlayer;
class CVoteMenuManager;

/**
 * Lightweight fluent helper for building vote menu pages.
 * Delegates to CVoteMenuManager internally. Each method returns *this
 * for chaining.
 */
class CVoteWrapper
{
	int m_ClientID;
	CGameContext *m_pGS;
	CVoteMenuManager *m_pVote;

public:
	CVoteWrapper(int ClientID, CGameContext *pGS, CVoteMenuManager *pVote);

	// MRPG-style API (Info = description, Option = selectable)
	CVoteWrapper &Info(const char *pText);
	CVoteWrapper &Option(const char *pCmd, const char *pText);
	CVoteWrapper &GroupTitle(const char *pTitle);
	CVoteWrapper &GroupLine();
	CVoteWrapper &Footer();
	CVoteWrapper &GoToPage(int Page, const char *pDesc);

	// Fluent API (legacy / TD menus)
	CVoteWrapper &Add(const char *pDesc, const char *pCmd);
	CVoteWrapper &AddOption(const char *pCmd, const char *pText);
	CVoteWrapper &AddBullet(const char *pDesc, const char *pCmd);
	CVoteWrapper &AddItem(const char *pDesc, const char *pCmd);
	CVoteWrapper &AddBackpage();
	CVoteWrapper &AddLine();
	CVoteWrapper &AddEmptyline();
	CVoteWrapper &AddSpace();
	CVoteWrapper &PageHeader(const char *pTitle);
	CVoteWrapper &PageSubtitle(const char *pText);
	CVoteWrapper &Separator();
	CVoteWrapper &Section(const char *pLabel);
	CVoteWrapper &GoTo(int Page, const char *pDesc);
	CVoteWrapper &EmptyHint(const char *pText);
	CVoteWrapper &PageFooter();
	CVoteWrapper &ProgressLine(int Current, int Max);

	int ClientID() const { return m_ClientID; }
};

#endif
