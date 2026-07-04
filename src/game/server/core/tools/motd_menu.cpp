#include "motd_menu.h"

#include <game/server/core/components/dialogs/dialog_manager.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/entities/character.h>
#include <engine/server.h>

// ══════════════════════════════════════════════════════════════
//  UTF-8 helpers (ported from MRPG)
// ══════════════════════════════════════════════════════════════

static int Utf8ByteOffset(const std::string &Text, int CharIndex)
{
	int Offset = 0;
	const char *pStr = Text.c_str();
	for(int i = 0; i < CharIndex && pStr[Offset]; ++i)
		Offset = str_utf8_forward(pStr, Offset);
	return Offset;
}

static int Utf8StrLen(const char *pStr)
{
	int Len = 0;
	while(*pStr)
	{
		const char *pTmp = pStr;
		int Code = str_utf8_decode(&pTmp);
		if(!Code) break;
		pStr = pTmp;
		Len++;
	}
	return Len;
}

static std::string BuildScrollableText(MotdOption &Opt, int Tick, int TickSpeed)
{
	const int Visible = maximum(0, MOTD_MENU_TEXT_LIMIT - MOTD_MENU_TEXT_ELLIPSIS);
	const int MaxOff = maximum(0, Opt.m_FullDescLength - Visible);
	if(MaxOff <= 0)
		return Opt.m_FullDesc;

	const int FastDelay = maximum(1, TickSpeed / 3);
	if(Opt.m_NextScrollTick < 0)
		Opt.m_NextScrollTick = Tick + FastDelay;

	if(Tick >= Opt.m_NextScrollTick)
	{
		Opt.m_ScrollOffset = (Opt.m_ScrollOffset + 1) % (MaxOff + 1);
		const int RevealIdx = Opt.m_ScrollOffset + Visible;
		int Delay = FastDelay;
		if(RevealIdx >= Opt.m_FullDescLength)
			Delay = TickSpeed;
		else
		{
			const int RevealByte = Utf8ByteOffset(Opt.m_FullDesc, RevealIdx);
			if(Opt.m_FullDesc[RevealByte] == ' ')
				Delay = TickSpeed;
		}
		Opt.m_NextScrollTick = Tick + Delay;
	}

	const int Start = Utf8ByteOffset(Opt.m_FullDesc, Opt.m_ScrollOffset);
	const int End = Utf8ByteOffset(Opt.m_FullDesc, Opt.m_ScrollOffset + Visible);
	std::string Out = Opt.m_FullDesc.substr(Start, End - Start);

	if(Opt.m_ScrollOffset < MaxOff)
		Out += "..";

	return Out;
}

// ══════════════════════════════════════════════════════════════
//  MotdMenu implementation (MRPG-style with engine input tracking)
// ══════════════════════════════════════════════════════════════

CGameContext *MotdMenu::GS() const { return m_pGS; }

CPlayer *MotdMenu::GetPlayer() const
{
	int CID = m_ClientID;
	if(CID < 0 || CID >= MAX_CLIENTS)
		return nullptr;
	return GS()->m_apPlayers[CID];
}

MotdOption &MotdMenu::AddImpl(const char *pCommand, const char *pDescription)
{
	MotdOption Opt;
	str_copy(Opt.m_aDesc, pDescription, sizeof(Opt.m_aDesc));
	Opt.m_Command = pCommand ? pCommand : "NULL";
	m_Points.push_back(std::move(Opt));

	if(auto *pPlayer = GetPlayer())
		pPlayer->m_MotdData.m_ScrollManager.SetMaxScrollPos((int)m_Points.size());

	return m_Points.back();
}

void MotdMenu::AddText(const char *pText)
{
	auto &Opt = AddImpl("NULL", pText ? pText : "");
	Opt.m_FullDesc = pText ? pText : "";
	Opt.m_FullDescLength = Utf8StrLen(Opt.m_FullDesc.c_str());
}

MotdOption &MotdMenu::AddOption(const char *pCommand, const char *pDescription)
{
	return AddImpl(pCommand, pDescription);
}

void MotdMenu::AddMenu(int MenuID, int Extra, const char *pDescription)
{
	auto &Opt = AddImpl("MENU", pDescription);
	Opt.m_MenuID = MenuID;
	Opt.m_MenuExtra = Extra;
}

void MotdMenu::AddField(int TextID, int64_t Flags)
{
	auto *pPlayer = GetPlayer();
	if(!pPlayer) return;

	auto &MotdData = pPlayer->m_MotdData;
	auto &InputField = MotdData.m_CurrentInputField;
	auto &vFields = MotdData.m_vFields;
	const char *pActive = (InputField.Active && TextID == InputField.TextID) ? "\xe2\x9c\x8e " : "";
	size_t LengthSide = vFields[TextID].Message.empty() ? 18 : 0;
	std::string Spaces(LengthSide, '-');

	std::string EndText;
	vFields[TextID].Flags = Flags;
	if(vFields[TextID].Flags & MTTEXTINPUTFLAG_PASSWORD)
		EndText = std::string(vFields[TextID].Message.length(), '*');
	else
		EndText = vFields[TextID].Message;

	std::string Result;
	Result += pActive;
	Result += "[-";
	Result += EndText;
	Result += Spaces;
	Result += "-]";

	auto &Opt = AddImpl("TEXT_FIELD", Result.c_str());
	Opt.m_MenuID = TextID;
}

void MotdMenu::AddLine() { AddImpl("NULL", ""); }

void MotdMenu::AddSeparateLine()
{
	AddImpl("NULL", "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
		"\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
		"\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
		"\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
		"\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80");
}

void MotdMenu::AddBackpage() { AddImpl("BACKPAGE", "<<< Backpage"); }

void MotdMenu::ClearOptions() { m_Points.clear(); }

void MotdMenu::SetDescription(const char *pDesc)
{
	m_Description = pDesc ? pDesc : "";
}

// ── Tick: MRPG-style input tracking ──────────────────────────
//
// Uses Server()->Input()->IsKeyClicked() (tracked via ProcessCharacterInput
// in CPlayer::OnDirectInput) and Server()->Input()->BlockInputGroup().
//
// Mouse coordinates come from the character's latest input TargetX/Y.
// The MOTD renders at fixed Y positions matching MRPG's layout.
//
void MotdMenu::Tick()
{
	if(m_Points.empty())
		return;

	auto *pPlayer = GetPlayer();
	if(!pPlayer || !pPlayer->GetCharacter())
		return;

	const int lineSizeY = 20;
	const int startLineY = -278;
	constexpr int linePosStart = 2;
	auto *pServer = GS()->Server();
	auto *pChar = pPlayer->GetCharacter();
	const int TargetX = pChar->LatestInput().m_TargetX;
	const int TargetY = pChar->LatestInput().m_TargetY;
	auto &MotdData = pPlayer->m_MotdData;
	auto &InputField = MotdData.m_CurrentInputField;
	const int VisibleItems = MotdData.m_ScrollManager.GetMaxVisibleItems();

	// ── Build buffer ──
	int linePos = linePosStart;
	std::string Buffer;
	Buffer.reserve(2048);
	Buffer += "* Press Esc to close MOTD\n\n";

	// ── Mouse hover area detection ──
	const int startWorkedAreaY = startLineY + linePosStart * lineSizeY;
	const int endWorkedAreaY = startLineY + (linePosStart + VisibleItems) * lineSizeY;
	const bool IsInMenuArea = (TargetX > -196 && TargetX < 196 && TargetY >= startWorkedAreaY && TargetY < endWorkedAreaY);

	if(IsInMenuArea)
	{
		// Scroll detection via engine-level key clicks
		if(pServer->Input()->IsKeyClicked(m_ClientID, KEY_EVENT_NEXT_WEAPON))
		{
			MotdData.m_ScrollManager.ScrollUp();
			m_ResendMotdTick = pServer->Tick() + 5;
		}
		else if(pServer->Input()->IsKeyClicked(m_ClientID, KEY_EVENT_PREV_WEAPON))
		{
			MotdData.m_ScrollManager.ScrollDown();
			m_ResendMotdTick = pServer->Tick() + 5;
		}

		// Block fire/hammer while hovering the menu (MRPG-style)
		pServer->Input()->BlockInputGroup(m_ClientID, BLOCK_INPUT_FIRE | BLOCK_INPUT_FREEZE_HAMMER);
	}

	// ── Render menu items ──
	int i = MotdData.m_ScrollManager.GetScrollPos();
	for(; i < MotdData.m_ScrollManager.GetEndScrollPos() && i < (int)m_Points.size(); ++i, ++linePos)
	{
		ApplyScrollbar(pPlayer, i, Buffer);

		bool UpdatedMotd = false;
		const int CheckYStart = startLineY + linePos * lineSizeY;
		const int CheckYEnd = startLineY + (linePos + 1) * lineSizeY;
		const bool IsSelected = (TargetX > -196 && TargetX < 196 && TargetY >= CheckYStart && TargetY < CheckYEnd);
		const bool IsClicked = IsInMenuArea && pServer->Input()->IsKeyClicked(m_ClientID, KEY_EVENT_FIRE);

		auto &Opt = m_Points[i];
		const auto &Cmd = Opt.m_Command;

		// ── Text-only lines (no interaction) ──
		if(Cmd == "NULL")
		{
			if(IsSelected)
				MotdData.m_HoveredItemIndex = NOPE;

			if(Opt.m_FullDescLength > MOTD_MENU_TEXT_LIMIT)
				Buffer += BuildScrollableText(Opt, pServer->Tick(), pServer->TickSpeed());
			else
				Buffer += Opt.m_aDesc;
			Buffer += '\n';
			continue;
		}

		// Hover tracking
		if(IsSelected && MotdData.m_HoveredItemIndex != i)
		{
			MotdData.m_HoveredItemIndex = i;
		}

		// ── Click handling ──
		if(IsClicked && IsSelected && InputField.Active)
		{
			InputField.Active = false;
			MotdData.m_HoveredItemIndex = NOPE;
			UpdatedMotd = true;
		}
		else if(IsSelected && IsClicked)
		{
			MotdData.m_pCurrent = &m_Points[i];

			if(Cmd == "CLOSE")
			{
				ClearMotd();
				return;
			}

			if(Cmd == "TEXT_FIELD")
			{
				InputField.Active = true;
				InputField.TextID = Opt.m_MenuID;
				MotdData.m_HoveredItemIndex = NOPE;
				UpdatedMotd = true;
			}

			if(Cmd == "MENU")
			{
				const int NewMenu = Opt.m_MenuID;
				const auto NewExtra = (Opt.m_MenuExtra <= NOPE) ? std::nullopt : std::make_optional<int>(Opt.m_MenuExtra);
				const bool Changed = NewMenu != m_Menulist || NewExtra != m_MenuExtra;
				if(Changed)
				{
					UpdatedMotd = true;
					m_MenuHistory.push_back({m_Menulist, m_MenuExtra});
					m_Menulist = NewMenu;
					m_MenuExtra = NewExtra;
					MotdData.m_HoveredItemIndex = NOPE;
					MotdData.m_vFields.clear();
					MotdData.m_ScrollManager.Reset();
				}
			}

			if(Cmd == "BACKPAGE")
			{
				UpdatedMotd = true;
				if(!m_MenuHistory.empty())
				{
					const auto Back = m_MenuHistory.back();
					m_MenuHistory.pop_back();
					m_Menulist = Back.Menulist;
					m_MenuExtra = Back.MenuExtra;
				}
				else
				{
					m_Menulist = m_LastMenulist;
					m_MenuExtra = std::nullopt;
				}
				MotdData.m_HoveredItemIndex = NOPE;
				MotdData.m_vFields.clear();
				MotdData.m_ScrollManager.Reset();
			}

			// Dispatch custom command
			if(GS()->Core() && GS()->Core()->DialogManager())
			{
				if(GS()->Core()->DialogManager()->HandleMotdMenuCommand(pPlayer, Cmd.c_str()))
				{
					if(m_Flags & MTFLAG_CLOSE_ON_SELECT)
					{
						ClearMotd();
						return;
					}
					UpdatedMotd = true;
				}
			}
		}

		if(UpdatedMotd)
		{
			UpdateMotd();
			return;
		}

		Buffer += IsSelected ? "\xe2\x9e\x9c " : "\xe2\x95\xbe ";
		Buffer += Opt.m_aDesc;
		Buffer += '\n';
	}

	// Reset hover when outside area
	if(!IsInMenuArea)
		MotdData.m_HoveredItemIndex = NOPE;

	if(!m_Description.empty())
	{
		Buffer += '\n';
		Buffer += m_Description;
	}

	// Resend if buffer changed
	if(m_LastBuffer != Buffer)
	{
		m_LastBuffer = Buffer;
		m_ResendMotdTick = pServer->Tick() + pServer->TickSpeed();
		CNetMsg_Sv_Motd Msg;
		Msg.m_pMessage = Buffer.c_str();
		pServer->SendPackMsg(&Msg, MSGFLAG_VITAL, m_ClientID);
	}
	else if(pServer->Tick() >= m_ResendMotdTick)
	{
		m_ResendMotdTick = pServer->Tick() + pServer->TickSpeed();
		UpdateMotd();
	}
}

void MotdMenu::Send(int Menulist)
{
	if(m_Flags & MTFLAG_CLOSE_BUTTON)
		AddImpl("CLOSE", "Close");

	if(auto *pPlayer = GetPlayer())
	{
		m_Menulist = Menulist;
		if(pPlayer->m_pMotdMenu)
		{
			m_MenuHistory = pPlayer->m_pMotdMenu->m_MenuHistory;
			m_LastMenulist = (pPlayer->m_pMotdMenu->m_Menulist != Menulist)
				? pPlayer->m_pMotdMenu->m_Menulist
				: pPlayer->m_pMotdMenu->m_LastMenulist;
		}
		pPlayer->m_pMotdMenu = std::make_unique<MotdMenu>(*this);
	}
}

void MotdMenu::ClearMotd()
{
	auto *pPlayer = GetPlayer();
	if(pPlayer && pPlayer->m_pMotdMenu)
	{
		m_Points.clear();
		CNetMsg_Sv_Motd Msg;
		Msg.m_pMessage = "";
		GS()->Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, m_ClientID);

		pPlayer->m_MotdData.m_ScrollManager.Reset();
		pPlayer->m_MotdData.m_HoveredItemIndex = NOPE;
		pPlayer->m_MotdData.m_vFields.clear();
		pPlayer->m_pMotdMenu.reset();
	}
}

void MotdMenu::UpdateMotd()
{
	auto *pPlayer = GetPlayer();
	if(pPlayer)
	{
		m_ResendMotdTick = GS()->Server()->Tick() + GS()->Server()->TickSpeed();
		m_LastBuffer.clear();
	}
}

void MotdMenu::ApplyScrollbar(CPlayer *pPlayer, int Index, std::string &rBuffer)
{
	const auto &SM = pPlayer->m_MotdData.m_ScrollManager;
	const int TotalItems = (int)m_Points.size();
	const int VisibleItems = SM.GetMaxVisibleItems();
	const int CurScroll = SM.GetScrollPos();

	if(TotalItems <= VisibleItems)
	{
		rBuffer += "\xe2\x96\x8d";
		return;
	}

	float VP = (float)VisibleItems / (float)TotalItems;
	int SBH = round_to_int(VP * VisibleItems);
	if(SBH < 1) SBH = 1;
	if(SBH > VisibleItems) SBH = VisibleItems;

	float Prog = (float)CurScroll / (float)maximum(1, TotalItems - VisibleItems);
	int SBP = (int)(Prog * (VisibleItems - SBH));
	int Bi = Index - CurScroll;

	rBuffer += (Bi >= SBP && Bi < SBP + SBH) ? "\xe2\x96\x8d" : "\xe2\x96\x8f";
}

bool MotdMenu::ApplyFieldEdit(const std::string &Message)
{
	auto *pPlayer = GetPlayer();
	if(!pPlayer) return false;

	auto &InputField = pPlayer->m_MotdData.m_CurrentInputField;
	auto &vFields = pPlayer->m_MotdData.m_vFields;

	if(!InputField.Active) return false;

	std::string_view MsgView = Message;
	if(MsgView.empty() || MsgView.front() != '/')
	{
		GS()->SendChatTo(m_ClientID, "[&] Use /<text> to edit the field.");
		return true;
	}

	MsgView.remove_prefix(1);
	while(!MsgView.empty() && MsgView.front() == ' ')
		MsgView.remove_prefix(1);

	std::string FieldMessage(MsgView);
	auto &FieldData = vFields[InputField.TextID];
	if(FieldData.Flags & MTTEXTINPUTFLAG_ONLY_NUMERIC)
	{
		bool AllDigit = true;
		for(char c : FieldMessage)
			if(!(c >= '0' && c <= '9')) { AllDigit = false; break; }
		if(!AllDigit)
		{
			GS()->SendChatTo(m_ClientID, "[&] Only numeric values allowed.");
			return true;
		}
	}

	InputField.Active = false;
	pPlayer->m_MotdData.m_HoveredItemIndex = NOPE;
	FieldData.Message = FieldMessage;
	GS()->SendChatTo(m_ClientID, "[&] Field has been updated!");
	UpdateMotd();
	return true;
}

const char *MotdMenu::HandleSelect(int Index, int &OutMenuID)
{
	if(Index < 0 || Index >= (int)m_Points.size())
		return "NULL";

	auto &Opt = m_Points[Index];
	OutMenuID = Opt.m_MenuID;

	if(auto *pPlayer = GetPlayer())
	{
		pPlayer->m_MotdData.m_pCurrent = &m_Points[Index];
		pPlayer->m_MotdData.m_HoveredItemIndex = Index;
	}

	if(Opt.m_Command == "CLOSE")     return "CLOSE";
	if(Opt.m_Command == "TEXT_FIELD") return "TEXT_FIELD";
	if(Opt.m_Command == "MENU")      return "MENU";
	if(Opt.m_Command == "BACKPAGE")  return "BACKPAGE";

	return Opt.m_Command.c_str();
}
