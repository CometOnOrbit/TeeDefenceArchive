#include "mrpg_dialog.h"
#include "dialog_data.h"

#include <game/server/core/components/npcs/npc_manager.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/entities/character.h>
#include <base/system.h>
#include <engine/message.h>

// ── Helper: send MOTD to client (MRPG-style, not MotdMenu) ──
static void SendMotdToClient(CGameContext *pGS, int ClientID, const char *pText)
{
	if(!pGS || ClientID < 0 || ClientID >= MAX_CLIENTS) return;
	CNetMsg_Sv_Motd Msg;
	Msg.m_pMessage = pText;
	pGS->Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

// ── Helper: substring replace (TDA has no str_replace) ──
static void StrReplace(char *pBuf, int BufSize, const char *pFrom, const char *pTo)
{
	const int FromLen = str_length(pFrom);
	const int ToLen = str_length(pTo);
	if(FromLen <= 0 || BufSize <= 0) return;

	char aTemp[2048];
	int TempPos = 0;
	const char *pSrc = pBuf;

	while(*pSrc && TempPos < BufSize - 1)
	{
		const char *pFound = str_find(pSrc, pFrom);
		if(!pFound)
		{
			while(*pSrc && TempPos < BufSize - 1) aTemp[TempPos++] = *pSrc++;
			break;
		}
		while(pSrc < pFound && TempPos < BufSize - 1) aTemp[TempPos++] = *pSrc++;
		pSrc += FromLen;
		for(int i = 0; i < ToLen && TempPos < BufSize - 1; i++) aTemp[TempPos++] = pTo[i];
	}
	aTemp[TempPos] = '\0';
	str_copy(pBuf, aTemp, BufSize);
}

// ══════════════════════════════════════════════════════════════
//  CDialogStepMrpg
// ══════════════════════════════════════════════════════════════

void CDialogStepMrpg::Init(int NpcBotID, const char *pNpcName, const json_value &JsonDialog)
{
	m_Text[0] = '\0';
	m_Request = false;
	m_Flags = 0;
	m_LeftSideID = 0;
	m_RightSideID = 0;

	auto JsGet = [](const json_value &Obj, const char *pKey) -> const json_value& {
		return Obj.operator[](pKey);
	};

	(void)pNpcName;

	if(JsonDialog.type == json_object)
	{
		const json_value &Text = JsGet(JsonDialog, "text");
		if(Text.type == json_string) str_copy(m_Text, Text.u.string.ptr, sizeof(m_Text));

		const json_value &Act = JsGet(JsonDialog, "action");
		if(Act.type == json_boolean) m_Request = Act.u.boolean != 0;

		const json_value &Side = JsGet(JsonDialog, "side");
		if(Side.type == json_string)
		{
			const char *pSide = Side.u.string.ptr;
			if(str_comp(pSide, "author") == 0) m_Flags |= DIALOGFLAG_SPEAK_AUTHOR;
			else if(str_comp(pSide, "thoughts") == 0) m_Flags |= DIALOGFLAG_SPEAK_THOUGHTS;
		}

		const json_value &LID = JsGet(JsonDialog, "left_speaker_id");
		if(LID.type == json_integer) m_LeftSideID = (int)LID.u.integer;

		const json_value &RID = JsGet(JsonDialog, "right_speaker_id");
		if(RID.type == json_integer) m_RightSideID = (int)RID.u.integer;
	}

	if(!(m_Flags & DIALOGFLAG_SPEAK_AUTHOR) && !(m_Flags & DIALOGFLAG_SPEAK_THOUGHTS))
	{
		if(m_LeftSideID == -1) m_Flags |= DIALOGFLAG_LEFT_PLAYER;
		else if(m_LeftSideID >= 0) { m_Flags |= DIALOGFLAG_LEFT_BOT; if(m_LeftSideID == 0) m_LeftSideID = NpcBotID; }
		if(m_RightSideID == -1) m_Flags |= DIALOGFLAG_RIGHT_PLAYER;
		else if(m_RightSideID >= 0) { m_Flags |= DIALOGFLAG_RIGHT_BOT; if(m_RightSideID == 0) m_RightSideID = NpcBotID; }
	}
}

// ══════════════════════════════════════════════════════════════
//  CPlayerDialog
// ══════════════════════════════════════════════════════════════

void CPlayerDialog::Init(CGameContext *pGS, CPlayer *pPlayer)
{
	m_pGS = pGS;
	m_pPlayer = pPlayer;
	Clear();
}

void CPlayerDialog::Start(int BotCID, const char *pNpcId, const char *pNpcName, const array<CDialogStepMrpg> &Steps)
{
	if(!m_pPlayer || !Steps.size()) return;
	Clear();
	m_BotCID = BotCID;
	m_Step = 0;
	m_vSteps = Steps;
	str_copy(m_aNpcName, pNpcName ? pNpcName : "NPC", sizeof(m_aNpcName));
	(void)pNpcId;
	ShowCurrentDialog();
}

void CPlayerDialog::Next()
{
	if(!m_pPlayer || !IsActive()) return;

	m_Step++;
	if(m_Step >= (int)m_vSteps.size())
	{
		End();
		return;
	}
	ShowCurrentDialog();
}

void CPlayerDialog::End()
{
	if(!m_pPlayer) return;
	// Clear MOTD
	if(m_pGS) SendMotdToClient(m_pGS, m_pPlayer->GetCID(), "");
	Clear();
}

void CPlayerDialog::Tick()
{
	if(!IsActive() || !m_pPlayer) return;
	if(!m_pPlayer->GetCharacter() || !m_pPlayer->GetCharacter()->IsAlive())
	{
		Clear();
		return;
	}

	// MRPG: close dialog when player moves too far from the NPC bot
	if(m_BotCID >= MAX_HUMAN_CLIENTS && m_pGS && m_pGS->m_apPlayers[m_BotCID])
	{
		CCharacter *pNpc = m_pGS->m_apPlayers[m_BotCID]->GetCharacter();
		if(!pNpc || distance(m_pPlayer->m_ViewPos, pNpc->GetPos()) > 180.f)
			Clear();
	}
}

void CPlayerDialog::Clear()
{
	m_BotCID = -1;
	m_Step = -1;
	m_vSteps.clear();
	m_aNpcName[0] = '\0';
	m_aFormattedText[0] = '\0';
}

const CDialogStepMrpg *CPlayerDialog::GetCurrent() const
{
	if(m_Step < 0 || m_Step >= (int)m_vSteps.size()) return nullptr;
	return &m_vSteps[m_Step];
}

// ══════════════════════════════════════════════════════════════
//  MRPG-style MOTD rendering
// ══════════════════════════════════════════════════════════════

void CPlayerDialog::ShowCurrentDialog()
{
	if(!m_pPlayer) return;
	const CDialogStepMrpg *pCurrent = GetCurrent();
	if(!pCurrent) { End(); return; }

	FormatDialog(pCurrent);

	// Send as MOTD (MRPG-style)
	if(m_pGS)
	{
		const char *pMsg = m_aFormattedText;
		if(pMsg[0] == '\0') pMsg = "\0";
		dbg_msg("dialog", "ShowCurrentDialog: CID=%d step=%d/%d, sending MOTD (%d chars)",
			m_pPlayer->GetCID(), m_Step+1, m_vSteps.size(), str_length(pMsg));
		SendMotdToClient(m_pGS, m_pPlayer->GetCID(), pMsg);
	}
}

void CPlayerDialog::FormatDialog(const CDialogStepMrpg *pDialog)
{
	if(!pDialog || !m_pPlayer) return;

	const int ClientID = m_pPlayer->GetCID();
	const bool IsAuthor = pDialog->m_Flags & DIALOGFLAG_SPEAK_AUTHOR;

	// ── Nicknames ──
	const char *pLeft = nullptr, *pRight = nullptr;
	if(IsAuthor) pLeft = "...";
	else
	{
		if(pDialog->m_Flags & DIALOGFLAG_LEFT_PLAYER) pLeft = m_pGS->Server()->ClientName(ClientID);
		else if(pDialog->m_Flags & DIALOGFLAG_LEFT_BOT) pLeft = m_aNpcName;
		if(pDialog->m_Flags & DIALOGFLAG_RIGHT_PLAYER) pRight = m_pGS->Server()->ClientName(ClientID);
		else if(pDialog->m_Flags & DIALOGFLAG_RIGHT_BOT) pRight = m_aNpcName;
	}

	char aBuf[1536] = {};

	// MRPG-style: info + speaker + text
	if(IsAuthor)
	{
		str_append(aBuf, "\n\n\u00abF4 - continue dialog\u00bb\n\n", sizeof(aBuf));
	}
	else
	{
		str_append(aBuf, "\n\n\u00abF4 (vote no) - continue dialog\u00bb\n\n", sizeof(aBuf));
	}

	// Speaker line
	if(pLeft && pRight) { char n[128]; str_format(n, sizeof(n), "* %s says to %s:\n", pLeft, pRight); str_append(aBuf, n, sizeof(aBuf)); }
	else if(pRight)     { char n[128]; str_format(n, sizeof(n), "* %s:\n", pRight); str_append(aBuf, n, sizeof(aBuf)); }
	else if(pLeft)      { char n[128]; str_format(n, sizeof(n), "* %s:\n", pLeft); str_append(aBuf, n, sizeof(aBuf)); }

	// Progress
	{
		const char *pSpeaker = IsAuthor ? "..." : (pRight ? pRight : (pLeft ? pLeft : m_aNpcName));
		char n[64];
		str_format(n, sizeof(n), "\u2500\u2500\u2500\u2500 | %d of %d | %s.\n", (m_Step + 1), maximum(1, (int)m_vSteps.size()), pSpeaker);
		str_append(aBuf, n, sizeof(aBuf));
	}

	// Dialog text
	char aText[1024];
	str_copy(aText, pDialog->m_Text, sizeof(aText));
	ReplacePlaceholders(aText, sizeof(aText));
	str_append(aBuf, "\u00ab", sizeof(aBuf));
	str_append(aBuf, aText, sizeof(aBuf));
	str_append(aBuf, "\u00bb", sizeof(aBuf));

	str_copy(m_aFormattedText, aBuf, sizeof(m_aFormattedText));
}

void CPlayerDialog::ReplacePlaceholders(char *pBuf, int BufSize) const
{
	if(!pBuf || !m_pPlayer || !m_pGS) return;

	const int ClientID = m_pPlayer->GetCID();
	StrReplace(pBuf, BufSize, "<player>", m_pGS->Server()->ClientName(ClientID));
	StrReplace(pBuf, BufSize, "<here>", m_pGS->Server()->GetWorldName(m_pGS->GetWorldID()));

	// <world_N>
	{
		const char *pPrefix = "<world_";
		const char *pSearch = str_find(pBuf, pPrefix);
		while(pSearch)
		{
			int id = 0;
			pSearch += str_length(pPrefix);
			if(sscanf(pSearch, "%d>", &id) == 1)
			{
				char aSearch[32];
				str_format(aSearch, sizeof(aSearch), "<world_%d>", id);
				StrReplace(pBuf, BufSize, aSearch, m_pGS->Server()->GetWorldName(id));
			}
			pSearch = str_find(pBuf, pPrefix);
		}
	}

	// <item_N>
	{
		const char *pPrefix = "<item_";
		const char *pSearch = str_find(pBuf, pPrefix);
		while(pSearch)
		{
			int id = 0;
			pSearch += str_length(pPrefix);
			if(sscanf(pSearch, "%d>", &id) == 1)
			{
				char aSearch[32];
				str_format(aSearch, sizeof(aSearch), "<item_%d>", id);
				StrReplace(pBuf, BufSize, aSearch, "Item");
			}
			pSearch = str_find(pBuf, pPrefix);
		}
	}
}
