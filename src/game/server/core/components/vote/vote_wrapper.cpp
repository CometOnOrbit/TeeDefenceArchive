/* (c) TeeDefenceArchive - 2026 */

#include <base/system.h>

#include <engine/shared/protocol.h>

#include <game/voting.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/core/components/vote/vote_menu_manager.h>
#include <game/server/core/components/vote/vote_wrapper.h>

CVoteWrapper::CVoteWrapper(int ClientID, CGameContext *pGS, CVoteMenuManager *pVote) :
	m_ClientID(ClientID),
	m_pGS(pGS),
	m_pVote(pVote)
{
	if(m_pVote)
		m_pVote->SetVoteBuildClientID(m_ClientID);
}

CVoteWrapper &CVoteWrapper::Info(const char *pText)
{
	if(m_pVote)
		m_pVote->AddVote_Info(pText);
	return *this;
}

CVoteWrapper &CVoteWrapper::Option(const char *pCmd, const char *pText)
{
	if(m_pVote)
		m_pVote->AddVote_Option(pText, pCmd);
	return *this;
}

CVoteWrapper &CVoteWrapper::GroupTitle(const char *pTitle)
{
	if(m_pVote)
		m_pVote->AddVote_GroupTitle(pTitle);
	return *this;
}

CVoteWrapper &CVoteWrapper::GroupLine()
{
	if(m_pVote)
		m_pVote->AddVote_GroupLine();
	return *this;
}

CVoteWrapper &CVoteWrapper::Footer()
{
	if(m_pVote)
		m_pVote->AddVote_Footer();
	return *this;
}

CVoteWrapper &CVoteWrapper::GoToPage(int Page, const char *pDesc)
{
	if(m_pVote)
		m_pVote->AddVote_Goto(Page, pDesc);
	return *this;
}

CVoteWrapper &CVoteWrapper::Add(const char *pDesc, const char *pCmd)
{
	if(!pCmd || !pCmd[0] || str_comp(pCmd, "ccv_null") == 0)
	{
		if(m_pVote)
			m_pVote->AddVote_Info(pDesc);
	}
	else if(m_pVote)
	{
		m_pVote->AddVote_Option(pDesc, pCmd);
	}
	return *this;
}

CVoteWrapper &CVoteWrapper::AddOption(const char *pCmd, const char *pText)
{
	return Option(pCmd, pText);
}

CVoteWrapper &CVoteWrapper::AddBackpage()
{
	if(m_pVote)
		m_pVote->AddVote_Back();
	return *this;
}

CVoteWrapper &CVoteWrapper::AddLine()
{
	return GroupLine();
}

CVoteWrapper &CVoteWrapper::AddEmptyline()
{
	if(m_pVote)
		m_pVote->AddVote_TextLine(" ");
	return *this;
}

CVoteWrapper &CVoteWrapper::AddSpace()
{
	if(m_pVote)
		m_pVote->AddVote_Space();
	return *this;
}

CVoteWrapper &CVoteWrapper::PageHeader(const char *pTitle)
{
	return GroupTitle(pTitle);
}

CVoteWrapper &CVoteWrapper::PageSubtitle(const char *pText)
{
	return Info(pText);
}

CVoteWrapper &CVoteWrapper::Separator()
{
	return GroupLine();
}

CVoteWrapper &CVoteWrapper::Section(const char *pLabel)
{
	return GroupTitle(pLabel);
}

CVoteWrapper &CVoteWrapper::GoTo(int Page, const char *pDesc)
{
	return GoToPage(Page, pDesc);
}

CVoteWrapper &CVoteWrapper::EmptyHint(const char *pText)
{
	return Info(pText);
}

CVoteWrapper &CVoteWrapper::PageFooter()
{
	return Footer();
}

CVoteWrapper &CVoteWrapper::ProgressLine(int Current, int Max)
{
	if(m_pVote)
		m_pVote->AddVote_ProgressLine(Current, Max);
	return *this;
}

CVoteWrapper &CVoteWrapper::AddBullet(const char *pDesc, const char *pCmd)
{
	return Option(pCmd, pDesc);
}

CVoteWrapper &CVoteWrapper::AddItem(const char *pDesc, const char *pCmd)
{
	return Option(pCmd, pDesc);
}
