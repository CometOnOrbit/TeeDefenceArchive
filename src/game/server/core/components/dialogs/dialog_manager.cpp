#include <engine/shared/jsonparser.h>
#include <engine/shared/config.h>

#include <game/server/core/components/dialogs/dialog_manager.h>
#include <game/server/core/components/npcs/npc_manager.h>
#include <game/server/core/components/vote/vote_menu_manager.h>
#include <game/server/core/components/quests/quest_manager.h>
#include <game/server/core/components/localization/localization_manager.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

// ══════════════════════════════════════════════════════════════
//  Dialog Chat Command Callback
// ══════════════════════════════════════════════════════════════

static void ComDialog(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->DialogManager()) return;

	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP) return;

	const char *pArg = pResult->GetString(0);
	pGame->Core()->DialogManager()->HandleDialogCommand(pP, pArg ? pArg : "");
}

// ══════════════════════════════════════════════════════════════
//  Dialog Condition Evaluation
// ══════════════════════════════════════════════════════════════

static bool StepConditionsMet(CPlayer *pPlayer, CGameContext *pGS, const CDialogStep &Step)
{
	if(Step.m_Conditions.size() == 0)
		return true;

	// OR of ANDs: at least one condition group must be fully satisfied.
	for(int gi = 0; gi < (int)Step.m_Conditions.size(); gi++)
	{
		const array<CDialogCondition> &Group = Step.m_Conditions[gi];
		bool GroupMet = true;
		for(int ci = 0; ci < (int)Group.size(); ci++)
		{
			if(!Group[ci].Evaluate(pPlayer, pGS))
			{
				GroupMet = false;
				break;
			}
		}
		if(GroupMet)
			return true;
	}
	return false;
}

// ══════════════════════════════════════════════════════════════
//  CDialogManager
// ══════════════════════════════════════════════════════════════

CDialogManager::CDialogManager()
{
	mem_zero(m_aNpcDialogs, sizeof(m_aNpcDialogs));
	m_NumNpcDialogs = 0;
}

void CDialogManager::OnInitWorld(const char *pWhereLocalWorld)
{
	(void)pWhereLocalWorld;
	LoadDialogs();
	for(int i = 0; i < MAX_CLIENTS; i++)
		if(GS()->m_apPlayers[i])
			m_aSessions[i].m_MrpgDialog.Init(GS(), GS()->m_apPlayers[i]);
}

void CDialogManager::OnTick()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pP = GS()->m_apPlayers[i];
		if(!pP) continue;

		SPlayerDialogSession &Session = m_aSessions[i];
		Session.m_MrpgDialog.Tick();

		if(!Session.m_Active) continue;
		if(!pP->GetCharacter() || !pP->GetCharacter()->IsAlive() ||
			distance(pP->GetCharacter()->GetPos(), vec2(Session.m_NpcPosX, Session.m_NpcPosY)) > 180.f)
		{
			Session.m_MrpgDialog.End();
			Session.Reset();
		}
	}
}

const SNpcDialogDef *CDialogManager::FindDialogDef(const char *pNpcId) const
{
	if(!pNpcId) return nullptr;
	for(int i = 0; i < m_NumNpcDialogs; i++)
		if(str_comp(m_aNpcDialogs[i].m_aNpcId, pNpcId) == 0)
			return &m_aNpcDialogs[i];
	return nullptr;
}

void CDialogManager::ExecuteActions(CPlayer *pPlayer, const CDialogStep &Step)
{
	for(const auto &Action : Step.m_Actions)
		Action.Execute(pPlayer, GS());
}

// ══════════════════════════════════════════════════════════════
//  TryTalk — B-1: MRPG-style MOTD dialog (no MotdMenu)
// ══════════════════════════════════════════════════════════════

bool CDialogManager::TryTalk(CPlayer *pPlayer, const char *pNpcId, float NpcPosX, float NpcPosY, int NpcWorld, int NpcClientID)
{
	if(!pPlayer || !pNpcId || !GS()) return false;

	const SNpcDialogDef *pDef = FindDialogDef(pNpcId);
	if(!pDef) { dbg_msg("dialog", "no dialog def for '%s'", pNpcId); return false; }

	const int CID = pPlayer->GetCID();
	SPlayerDialogSession &Session = m_aSessions[CID];
	if(Session.m_Active) { Session.m_MrpgDialog.End(); Session.Reset(); }

	// Save position
	Session.m_Active = true;
	Session.m_NpcPosX = NpcPosX;
	Session.m_NpcPosY = NpcPosY;

	// NPC display name
	char aNpcName[64] = "NPC";
	const SNpcDef *pNpcDef = Core() && Core()->NpcManager() ? Core()->NpcManager()->FindNpc(pNpcId) : nullptr;
	if(pNpcDef) str_copy(aNpcName, GS()->Loc(CID, pNpcDef->m_aNameKey, pNpcDef->m_aId), sizeof(aNpcName));

	// Find starting step (first with met conditions)
	int StartStep = 0;
	for(int si = 0; si < (int)pDef->m_Steps.size(); si++)
	{
		if(StepConditionsMet(pPlayer, GS(), pDef->m_Steps[si]))
		{
			StartStep = si;
			break;
		}
	}

	// Execute actions (heal, message, etc.) on start step
	ExecuteActions(pPlayer, pDef->m_Steps[StartStep]);

	// Shop/craft auto-nav
	for(const auto &Action : pDef->m_Steps[StartStep].m_Actions)
	{
		if(Action.m_Type == EDialogActionType::OPEN_SHOP ||
		   Action.m_Type == EDialogActionType::OPEN_CRAFT ||
		   Action.m_Type == EDialogActionType::OPEN_SKILLS ||
		   Action.m_Type == EDialogActionType::OPEN_QUESTS)
		{
			if(GS()->Core() && GS()->Core()->VoteMenuManager())
			{
				SPlayerVote *pV = GS()->Core()->VoteMenuManager()->GetPlayerVote(CID);
				if(pV)
				{
					pV->m_LastPage = PAGE_MENU;
					if(Action.m_Type == EDialogActionType::OPEN_SHOP)
					{
						pV->m_Page = PAGE_SHOP;
						// Save shop ID in m_aExtraText so PAGE_SHOP knows which shop
						str_copy(pV->m_aExtraText, Action.m_aKey, sizeof(pV->m_aExtraText));
					}
					else if(Action.m_Type == EDialogActionType::OPEN_CRAFT) pV->m_Page = PAGE_CRAFT;
					else if(Action.m_Type == EDialogActionType::OPEN_QUESTS) pV->m_Page = PAGE_QUESTS;
					else pV->m_Page = PAGE_SKILLS;
				}
				GS()->Core()->VoteMenuManager()->ClearVotes(CID);
			}
		}
	}

	// ── Build MRPG dialog steps ──
	array<CDialogStepMrpg> MrpgSteps;
	for(int si = 0; si < (int)pDef->m_Steps.size(); si++)
	{
		const CDialogStep &Step = pDef->m_Steps[si];
		if(!StepConditionsMet(pPlayer, GS(), Step))
			continue;

		CDialogStepMrpg Ms;
		Ms.m_Flags = DIALOGFLAG_RIGHT_BOT;
		Ms.m_RightSideID = 0;
		Ms.m_LeftSideID = -1;

		std::string FullText;
		for(int li = 0; li < Step.m_NumLines; li++)
		{
			if(!Step.m_aaTextLines[li][0]) continue;
			if(!FullText.empty()) FullText += "\n";
			char aKey[64];
			if(Step.m_aTextKey[0]) str_format(aKey, sizeof(aKey), "%s.%d", Step.m_aTextKey, li);
			else aKey[0] = '\0';
			FullText += GS()->Loc(CID, aKey, Step.m_aaTextLines[li]);
		}
		if(FullText.empty()) FullText = "...";
		str_copy(Ms.m_Text, FullText.c_str(), sizeof(Ms.m_Text));
		MrpgSteps.add(Ms);
	}

	// Start MRPG dialog (sends MOTD) — BotCID is the NPC server slot (MRPG convention)
	Session.m_MrpgDialog.Init(GS(), pPlayer);
	Session.m_MrpgDialog.Start(NpcClientID >= 0 ? NpcClientID : CID, pNpcId, aNpcName, MrpgSteps);
	dbg_msg("dialog", "TryTalk: started dialog for client %d with npc '%s', %d steps, active=%d",
		CID, pNpcId, MrpgSteps.size(), Session.m_Active ? 1 : 0);
	return true;
}

// ══════════════════════════════════════════════════════════════
//  HandleDialogCommand — MRPG-style: /dialog next / cancel
//  Delegates to HandleMotdMenuCommand for unified handling.
// ══════════════════════════════════════════════════════════════

bool CDialogManager::HandleDialogCommand(CPlayer *pPlayer, const char *pArgs)
{
	if(!pPlayer || !pArgs) return false;

	// Map /dialog next → dialog_next, /dialog cancel → dialog_end
	if(str_comp(pArgs, "next") == 0 || str_comp(pArgs, "continue") == 0)
		return HandleMotdMenuCommand(pPlayer, "dialog_next");

	if(str_comp(pArgs, "cancel") == 0 || str_comp(pArgs, "end") == 0 || str_comp(pArgs, "close") == 0)
		return HandleMotdMenuCommand(pPlayer, "dialog_end");

	return false;
}

bool CDialogManager::HandleVoteInput(CPlayer *pPlayer, int Vote)
{
	if(!pPlayer || !GS() || GS()->m_VoteCloseTime)
		return false;

	const int CID = pPlayer->GetCID();
	if(Vote != -1 && Vote != 0)
		return false;

	if(m_aSessions[CID].m_Active)
		return false;

	if(pPlayer->GetAccountId() < 0 && !pPlayer->IsGuest())
	{
		pPlayer->SetGuest(true);
		pPlayer->SetAccountId(-2);
		GS()->SendChat(CID, CHAT_ALL, -1, "🚀 欢迎！正在以游客身份进入游戏...");
		GS()->SendChat(CID, CHAT_ALL, -1, "💡 输入 /register <用户名> <密码> 注册可保存进度");
		if(Core())
			Core()->OnPlayerLogin(pPlayer);
		pPlayer->ChangeWorld(GS()->Config()->m_SvGuestWorld, nullptr);
		return true;
	}

	return false;
}

// ══════════════════════════════════════════════════════════════
//  HandleMotdMenuCommand — B-1: just advance/end dialog
// ══════════════════════════════════════════════════════════════

bool CDialogManager::HandleMotdMenuCommand(CPlayer *pPlayer, const char *pCommand)
{
	if(!pPlayer || !pCommand) return false;
	const int CID = pPlayer->GetCID();
	SPlayerDialogSession &Session = m_aSessions[CID];
	if(!Session.m_Active) return false;

	if(str_comp(pCommand, "dialog_next") == 0)
	{
		int OldStep = Session.m_MrpgDialog.GetCurrentStep();
		// MRPG: play pickup sound on F4 dialog advance
		if(pPlayer->GetCharacter())
			GS()->m_World.CreateSound(pPlayer->GetCharacter()->GetPos(), SOUND_PICKUP_ARMOR, CmaskOne(CID));
		Session.m_MrpgDialog.Next();
		dbg_msg("dialog", "HandleMotdMenuCommand(next): CID=%d step %d->%d, active=%d",
			CID, OldStep, Session.m_MrpgDialog.GetCurrentStep(), Session.m_MrpgDialog.IsActive() ? 1 : 0);
		// MrpgDialog.Next() sends new MOTD via ShowCurrentDialog()
		if(!Session.m_MrpgDialog.IsActive())
			Session.Reset();
		return true;
	}

	if(str_comp(pCommand, "dialog_end") == 0 || str_comp(pCommand, "CLOSE") == 0)
	{
		Session.m_MrpgDialog.End();
		Session.Reset();
		return true;
	}

	return false;
}

void CDialogManager::ResetSession(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS) return;
	m_aSessions[ClientID].m_MrpgDialog.End();
	m_aSessions[ClientID].Reset();
	if(GS()->m_apPlayers[ClientID])
		m_aSessions[ClientID].m_MrpgDialog.Init(GS(), GS()->m_apPlayers[ClientID]);
}

// ══════════════════════════════════════════════════════════════
//  LoadDialogs
// ══════════════════════════════════════════════════════════════

void CDialogManager::LoadDialogs()
{
	m_NumNpcDialogs = 0;
	if(!Storage()) { dbg_msg("dialog", "no storage"); return; }

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseFile("server_content/npcs.json", Storage());
	if(!pRoot || pRoot->type != json_object) { dbg_msg("dialog", "could not parse npcs.json"); return; }

	auto Js = [](const json_value &Obj, const char *pKey) -> const json_value& {
		return Obj.operator[](pKey);
	};

	const json_value &Npcs = Js(*pRoot, "npcs");
	if(Npcs.type != json_array) { return; }

	for(unsigned n = 0; n < Npcs.u.array.length && m_NumNpcDialogs < MAX_NPC_DIALOGS; n++)
	{
		const json_value &Npc = *Npcs.u.array.values[n];
		if(Npc.type != json_object) continue;

		SNpcDialogDef &Def = m_aNpcDialogs[m_NumNpcDialogs];
		const json_value &IdF = Js(Npc, "id");
		str_copy(Def.m_aNpcId, IdF.type == json_string ? IdF.u.string.ptr : "", sizeof(Def.m_aNpcId));

		const json_value &Dialogs = Js(Npc, "dialogs");
		if(Dialogs.type == json_array)
		{
			for(unsigned d = 0; d < Dialogs.u.array.length; d++)
			{
				const json_value &Dialog = *Dialogs.u.array.values[d];
				if(Dialog.type != json_object) continue;

				CDialogStep Step;
				auto Jd = [&](const char *p) -> const json_value& { return Js(Dialog, p); };

				const json_value &IdD = Jd("id");
				if(IdD.type == json_string) str_copy(Step.m_aId, IdD.u.string.ptr, sizeof(Step.m_aId));

				const json_value &Tk = Jd("text_key");
				if(Tk.type == json_string) str_copy(Step.m_aTextKey, Tk.u.string.ptr, sizeof(Step.m_aTextKey));

				const json_value &Tv = Jd("text");
				if(Tv.type == json_string)
				{
					str_copy(Step.m_aaTextLines[0], Tv.u.string.ptr, sizeof(Step.m_aaTextLines[0]));
					Step.m_NumLines = 1;
				}
				else if(Tv.type == json_array)
				{
					Step.m_NumLines = 0;
					for(unsigned t = 0; t < Tv.u.array.length && t < 8; t++)
					{
						const json_value &Line = *Tv.u.array.values[t];
						if(Line.type == json_string)
						{
							str_copy(Step.m_aaTextLines[Step.m_NumLines], Line.u.string.ptr, sizeof(Step.m_aaTextLines[0]));
							Step.m_NumLines++;
						}
					}
				}

				// actions
				const json_value &Acts = Jd("actions");
				if(Acts.type == json_array)
				{
					for(unsigned a = 0; a < Acts.u.array.length; a++)
					{
						const json_value &ActVal = *Acts.u.array.values[a];
						if(ActVal.type != json_object) continue;

						CDialogAction Action;
						const json_value &TypeF = Js(ActVal, "type");
						const char *pType = "";
						if(TypeF.type == json_string) pType = TypeF.u.string.ptr;

						if(str_comp(pType, "set_flag") == 0) Action.m_Type = EDialogActionType::SET_FLAG;
						else if(str_comp(pType, "add_reputation") == 0) Action.m_Type = EDialogActionType::ADD_REPUTATION;
						else if(str_comp(pType, "heal") == 0) Action.m_Type = EDialogActionType::HEAL;
						else if(str_comp(pType, "message") == 0) Action.m_Type = EDialogActionType::MESSAGE;
						else if(str_comp(pType, "open_shop") == 0) Action.m_Type = EDialogActionType::OPEN_SHOP;
						else if(str_comp(pType, "open_craft") == 0) Action.m_Type = EDialogActionType::OPEN_CRAFT;
						else if(str_comp(pType, "open_quests") == 0) Action.m_Type = EDialogActionType::OPEN_QUESTS;
						else if(str_comp(pType, "open_skills") == 0) Action.m_Type = EDialogActionType::OPEN_SKILLS;

						const json_value &KeyF = Js(ActVal, "key");
						if(KeyF.type == json_string) str_copy(Action.m_aKey, KeyF.u.string.ptr, sizeof(Action.m_aKey));
						const json_value &ValF = Js(ActVal, "value");
						if(ValF.type == json_integer) Action.m_Value = (int)ValF.u.integer;
						const json_value &MsgF = Js(ActVal, "message");
						if(MsgF.type == json_string) str_copy(Action.m_aStrValue, MsgF.u.string.ptr, sizeof(Action.m_aStrValue));

						Step.m_Actions.add(Action);
					}
				}
				Def.m_Steps.add(Step);
			}
		}
		m_NumNpcDialogs++;
	}

	dbg_msg("dialog", "loaded %d NPC dialog definitions", m_NumNpcDialogs);
	// Note: CJsonParser destructor frees the JSON memory
}

void CDialogManager::RegisterChatCommands(CCommandManager *pManager)
{
	if(!pManager) return;
	pManager->AddCommand("dialog", "<next/cancel> - NPC dialog control", "s", ComDialog, GS());
}
