#include <game/commands.h>
#include <game/server/account.h>
#include <game/server/core/components/dialogs/dialog_manager.h>
#include <game/server/core/components/npcs/npc_manager.h>
#include <game/server/core/components/quests/quest_manager.h>
#include <game/server/core/components/mmo/mmo_manager.h>
#include <game/server/data_center.h>
#include <game/server/core/components/vote/vote_menu_manager.h>
#include <game/server/core/components/vote/vote_menu_types.h>
#include <game/server/core/components/vote/vote_wrapper.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <engine/shared/jsonparser.h>
#include <engine/storage.h>
#include <game/server/player.h>

void CQuestManager::OnPreInit()
{
}

void CQuestManager::OnInitWorld(const char* pWhereLocalWorld)
{
	(void)pWhereLocalWorld;
	Init();
}

void CQuestManager::Init()
{
	InitQuests();
	InitQuestBoards();
	m_pScenarioManager = new CScenarioManager(GS());
	m_pScenarioManager->Init();
}

void CQuestManager::Reset()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayerQuest::ms_aPlayerQuests[i].clear();
	}
	if(m_pScenarioManager)
	{
		m_pScenarioManager->Reset();
	}
}

void CQuestManager::InitQuestStepDefeats(CQuestDescription* pQuest, int Step, int BotID, int RequiredCount, const char *pZoneName)
{
	SQuestBotInfo Bot{};
	Bot.m_ID = BotID;
	Bot.m_QuestID = pQuest->GetID();
	Bot.m_BotID = BotID;
	Bot.m_StepPos = Step;

	SQuestBotInfo::SRequiredDefeat Req{};
	Req.m_BotID = BotID;
	Req.m_RequiredCount = RequiredCount;
	if(pZoneName && pZoneName[0])
		str_copy(Req.m_ZoneName, pZoneName, sizeof(Req.m_ZoneName));
	Bot.m_vRequiredDefeats.add(Req);

	CQuestStepBase StepBase{};
	StepBase.m_Bot = Bot;
	pQuest->m_vObjectives[Step].push_back(StepBase);
}

void CQuestManager::InitQuestStepMoveAction(CQuestDescription* pQuest, int Step, vec2 Pos, int WorldID, const char* pTaskName)
{
	SQuestBotInfo Bot{};
	Bot.m_ID = -1;
	Bot.m_QuestID = pQuest->GetID();
	Bot.m_StepPos = Step;
	Bot.m_Position = Pos;
	Bot.m_WorldID = WorldID;

	SQuestBotInfo::SMoveAction MoveAction{};
	MoveAction.m_Step = Step;
	str_copy(MoveAction.m_TaskName, pTaskName, sizeof(MoveAction.m_TaskName));
	MoveAction.m_Position = Pos;
	MoveAction.m_WorldID = WorldID;
	Bot.m_vRequiredMoveAction.add(MoveAction);

	CQuestStepBase StepBase{};
	StepBase.m_Bot = Bot;
	pQuest->m_vObjectives[Step].push_back(StepBase);
}

void CQuestManager::InitQuestStepItems(CQuestDescription* pQuest, int Step, int ItemID, int Value, int Type)
{
	SQuestBotInfo Bot{};
	Bot.m_ID = -1;
	Bot.m_QuestID = pQuest->GetID();
	Bot.m_StepPos = Step;

	SQuestBotInfo::SRequiredItem Req{};
	Req.m_ItemID = ItemID;
	Req.m_Value = Value;
	Req.m_Type = (decltype(Req.m_Type))Type;
	Bot.m_vRequiredItems.add(Req);

	CQuestStepBase StepBase{};
	StepBase.m_Bot = Bot;
	pQuest->m_vObjectives[Step].push_back(StepBase);
}

void CQuestManager::InitQuestStepDialog(CQuestDescription* pQuest, int Step, const char* pNpcId, vec2 NpcPos, int NpcWorld)
{
	SQuestBotInfo Bot{};
	Bot.m_ID = -1;
	Bot.m_QuestID = pQuest->GetID();
	Bot.m_StepPos = Step;
	Bot.m_Position = NpcPos;
	Bot.m_WorldID = NpcWorld;

	SQuestBotInfo::SRequiredDialog Req{};
	str_copy(Req.m_aNpcId, pNpcId, sizeof(Req.m_aNpcId));
	Bot.m_vRequiredDialogs.add(Req);

	CQuestStepBase StepBase{};
	StepBase.m_Bot = Bot;
	pQuest->m_vObjectives[Step].push_back(StepBase);
}

void CQuestManager::InitQuestStepScenario(CQuestDescription* pQuest, int Step, const char* pScenarioJson)
{
	SQuestBotInfo Bot{};
	Bot.m_ID = -1;
	Bot.m_QuestID = pQuest->GetID();
	Bot.m_StepPos = Step;
	str_copy(Bot.m_ScenarioJson, pScenarioJson, sizeof(Bot.m_ScenarioJson));
	// HasAction is now an inline method — no member to set

	CQuestStepBase StepBase{};
	StepBase.m_Bot = Bot;
	pQuest->m_vObjectives[Step].push_back(StepBase);
}

void CQuestManager::InitQuests()
{
	// Load data-driven quest definitions from quests.json
	LoadQuestDefs();
	// Legacy hardcoded quests (MMO daily/side quests)
	InitHardcodedQuests();
}

// ─── JSON quest loader (server_content/quests.json) ───────────────

void CQuestManager::LoadQuestDefs()
{
	if(!Storage())
	{
		dbg_msg("quest", "No storage, skipping quests.json");
		return;
	}

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseFile("server_content/quests.json", Storage());
	if(!pRoot)
	{
		dbg_msg("quest", "quests.json: %s", Parser.Error());
		return;
	}

	const json_value &Quests = (*pRoot)["quests"];
	if(Quests.type != json_array)
	{
		dbg_msg("quest", "missing 'quests' array");
		return;
	}

	// Map string IDs → integer IDs (start from 1001 for JSON quests)
	int NextID = 1001;
	std::unordered_map<std::string, int> vQuestIDs;

	// First pass: assign IDs
	for(unsigned i = 0; i < Quests.u.array.length; i++)
	{
		const json_value &S = Quests[(int)i];
		if(S.type != json_object) continue;
		if(S["id"].type != json_string) continue;
		std::string StrID = S["id"].u.string.ptr;
		if(!vQuestIDs.count(StrID))
			vQuestIDs[StrID] = NextID++;
	}

	// Second pass: create quest objects
	int Loaded = 0;
	for(unsigned i = 0; i < Quests.u.array.length; i++)
	{
		const json_value &S = Quests[(int)i];
		if(S.type != json_object) continue;
		if(S["id"].type != json_string) continue;

		std::string StrID = S["id"].u.string.ptr;
		int ID = vQuestIDs[StrID];

		// Title
		const char *pTitleKey = "";
		if(S["title_key"].type == json_string)
			pTitleKey = S["title_key"].u.string.ptr;

		// Next quest
		std::optional<int> NextID = std::nullopt;
		if(S["next_quest"].type == json_string)
		{
			std::string NextStr = S["next_quest"].u.string.ptr;
			if(!NextStr.empty() && vQuestIDs.count(NextStr))
				NextID = vQuestIDs[NextStr];
		}

		// Rewards
		int GoldReward = 0, ExpReward = 0, RepReward = 0;
		// Temporary storage for item rewards (parsed after pQuest creation)
		struct { int m_ID; int m_Count; } aTmpItems[64];
		int NumTmpItems = 0;
		if(S["rewards"].type == json_object)
		{
			const json_value &Rewards = S["rewards"];
			if(Rewards["gold"].type == json_integer)
				GoldReward = (int)Rewards["gold"].u.integer;
			if(Rewards["exp"].type == json_integer)
				ExpReward = (int)Rewards["exp"].u.integer;
			if(Rewards["reputation"].type == json_integer)
				RepReward = (int)Rewards["reputation"].u.integer;
			// Parse item rewards (stored temporarily, added after pQuest creation)
			if(Rewards["items"].type == json_array)
			{
				for(unsigned r = 0; r < Rewards["items"].u.array.length && NumTmpItems < 64; r++)
				{
					const json_value &Item = Rewards["items"][(int)r];
					if(Item.type != json_object) continue;
					int ItemID = Item["id"].type == json_integer ? (int)Item["id"].u.integer : 0;
					int Num = Item["num"].type == json_integer ? (int)Item["num"].u.integer : 1;
					if(ItemID > 0)
					{
						aTmpItems[NumTmpItems].m_ID = ItemID;
						aTmpItems[NumTmpItems].m_Count = Num;
						NumTmpItems++;
					}
				}
			}
		}

		// Create quest (use title_key as name for now)
		const char *pName = pTitleKey[0] ? pTitleKey : StrID.c_str();
		CQuestDescription *pQuest = new CQuestDescription();
		pQuest->Init(ID, pName, GoldReward, ExpReward, NextID, RepReward);
		pQuest->AddFlag(QUEST_FLAG_TYPE_MAIN);
		pQuest->AddFlag(QUEST_FLAG_GRANTED_FROM_CHAIN);

		// Add item rewards to quest
		for(int t = 0; t < NumTmpItems; t++)
		{
			CQuestDescription::CReward::SRewardItem ri;
			ri.m_ItemID = aTmpItems[t].m_ID;
			ri.m_Count = aTmpItems[t].m_Count;
			pQuest->Reward().m_RewardItems.add(ri);
		}

		// Auto-grant flag
		if(S["auto_grant"].type == json_boolean && S["auto_grant"].u.boolean != 0)
			pQuest->AddFlag(QUEST_FLAG_CANT_REFUSE);

		// Parse steps
		if(S["steps"].type == json_array)
		{
			int StepNum = 0;
			for(unsigned s = 0; s < S["steps"].u.array.length; s++)
			{
				const json_value &Step = S["steps"][(int)s];
				if(Step.type != json_object) continue;
				StepNum++;

				const char *pType = Step["type"].type == json_string ? Step["type"].u.string.ptr : "";

				if(str_comp(pType, "kill_enemy") == 0)
				{
					int Count = Step["count"].type == json_integer ? (int)Step["count"].u.integer : 1;
					int BotID = Step["bot"].type == json_integer ? (int)Step["bot"].u.integer : 1;
					const char *pZoneName = Step["zone_name"].type == json_string ? Step["zone_name"].u.string.ptr : nullptr;
					InitQuestStepDefeats(pQuest, StepNum, BotID, Count, pZoneName);
				}
				else if(str_comp(pType, "collect_item") == 0)
				{
					int ItemID = Step["item"].type == json_integer ? (int)Step["item"].u.integer : 0;
					int Count = Step["count"].type == json_integer ? (int)Step["count"].u.integer : 1;
					InitQuestStepItems(pQuest, StepNum, ItemID, Count, SQuestBotInfo::SRequiredItem::TYPE_GIVE);
				}
				else if(str_comp(pType, "reach_zone") == 0)
				{
					float X = Step["x"].type == json_integer ? (float)Step["x"].u.integer : 0.f;
					float Y = Step["y"].type == json_integer ? (float)Step["y"].u.integer : 0.f;
					int World = Step["world"].type == json_integer ? (int)Step["world"].u.integer : 0;
					char aTask[64];
					str_format(aTask, sizeof(aTask), "到达指定区域");
					InitQuestStepMoveAction(pQuest, StepNum, vec2(X, Y), World, aTask);
				}
				else if(str_comp(pType, "talk_npc") == 0)
				{
					// Talk-to-NPC step: creates a dialog requirement
					// When the player talks to this NPC, the step auto-completes
					vec2 NpcPos = vec2(384.f, 256.f);
					int NpcWorld = 0;
					char aNpcId[64] = "";
					if(Step["npc"].type == json_string)
						str_copy(aNpcId, Step["npc"].u.string.ptr, sizeof(aNpcId));
					if(Step["x"].type == json_integer)
						NpcPos.x = (float)Step["x"].u.integer;
					if(Step["y"].type == json_integer)
						NpcPos.y = (float)Step["y"].u.integer;
					if(Step["world"].type == json_integer)
						NpcWorld = (int)Step["world"].u.integer;

					if(aNpcId[0] != '\0')
						InitQuestStepDialog(pQuest, StepNum, aNpcId, NpcPos, NpcWorld);
					else
					{
						// Fallback: move-to-position if no NPC ID
						char aTask[64];
						str_format(aTask, sizeof(aTask), "到达指定区域");
						InitQuestStepMoveAction(pQuest, StepNum, NpcPos, NpcWorld, aTask);
					}
				}
			}
		}

		CQuestDescription::ms_aData.add(pQuest);
		Loaded++;
	}

	dbg_msg("quest", "Loaded %d quest definitions from quests.json", Loaded);
}

// ─── Hardcoded quest definitions (MMO daily/side) ─────────────────────

void CQuestManager::InitHardcodedQuests()
{
	CQuestDescription* pQuest1 = new CQuestDescription();
	pQuest1->Init(1, "初入战场", 50, 100, 2);
	pQuest1->AddFlag(QUEST_FLAG_TYPE_MAIN);
	pQuest1->AddFlag(QUEST_FLAG_GRANTED_FROM_CHAIN);
	InitQuestStepDefeats(pQuest1, 1, 1, 5);
	CQuestDescription::ms_aData.add(pQuest1);

	CQuestDescription* pQuest2 = new CQuestDescription();
	pQuest2->Init(2, "深入敌营", 100, 200, 3);
	pQuest2->InitPrevousQuestID(1);
	pQuest2->AddFlag(QUEST_FLAG_TYPE_MAIN);
	pQuest2->AddFlag(QUEST_FLAG_GRANTED_FROM_CHAIN);
	InitQuestStepDefeats(pQuest2, 1, 2, 10);
	InitQuestStepDefeats(pQuest2, 2, 3, 3);
	CQuestDescription::ms_aData.add(pQuest2);

	CQuestDescription* pQuest3 = new CQuestDescription();
	pQuest3->Init(3, "防线巩固", 200, 500, std::nullopt);
	pQuest3->InitPrevousQuestID(2);
	pQuest3->AddFlag(QUEST_FLAG_TYPE_MAIN);
	pQuest3->AddFlag(QUEST_FLAG_GRANTED_FROM_CHAIN);
	InitQuestStepDefeats(pQuest3, 1, 4, 15);
	CQuestDescription::ms_aData.add(pQuest3);

	CQuestDescription* pDaily1 = new CQuestDescription();
	pDaily1->Init(101, "日常清理", 100, 150, std::nullopt);
	pDaily1->AddFlag(QUEST_FLAG_TYPE_DAILY);
	pDaily1->AddFlag(QUEST_FLAG_GRANTED_FROM_BOARD);
	InitQuestStepDefeats(pDaily1, 1, 1, 20);
	CQuestDescription::ms_aData.add(pDaily1);

	CQuestDescription* pSide1 = new CQuestDescription();
	pSide1->Init(201, "物资收集", 150, 250, std::nullopt);
	pSide1->AddFlag(QUEST_FLAG_TYPE_SIDE);
	pSide1->AddFlag(QUEST_FLAG_GRANTED_FROM_NPC);
	InitQuestStepItems(pSide1, 1, 1, 10, SQuestBotInfo::SRequiredItem::TYPE_GIVE);
	CQuestDescription::ms_aData.add(pSide1);

	// MMO-specific quests with better exp/gold rewards
	CQuestDescription* pMMO1 = new CQuestDescription();
	pMMO1->Init(301, "冒险起步", 300, 500, std::nullopt);
	pMMO1->AddFlag(QUEST_FLAG_TYPE_SIDE);
	pMMO1->AddFlag(QUEST_FLAG_GRANTED_FROM_NPC);
	InitQuestStepDefeats(pMMO1, 1, 1, 10);
	InitQuestStepItems(pMMO1, 2, 1, 5, SQuestBotInfo::SRequiredItem::TYPE_GIVE);
	CQuestDescription::ms_aData.add(pMMO1);

	CQuestDescription* pMMO2 = new CQuestDescription();
	pMMO2->Init(302, "猎杀精英", 500, 1000, std::nullopt);
	pMMO2->AddFlag(QUEST_FLAG_TYPE_SIDE);
	pMMO2->AddFlag(QUEST_FLAG_GRANTED_FROM_NPC);
	InitQuestStepDefeats(pMMO2, 1, 3, 5);
	CQuestDescription::ms_aData.add(pMMO2);

	CQuestDescription* pMMO3 = new CQuestDescription();
	pMMO3->Init(303, "探险者", 800, 2000, std::nullopt);
	pMMO3->AddFlag(QUEST_FLAG_TYPE_SIDE);
	pMMO3->AddFlag(QUEST_FLAG_GRANTED_FROM_NPC);
	InitQuestStepDefeats(pMMO3, 1, 4, 10);
	InitQuestStepDefeats(pMMO3, 2, 2, 15);
	CQuestDescription::ms_aData.add(pMMO3);

	CQuestDescription* pDaily2 = new CQuestDescription();
	pDaily2->Init(104, "金币日结", 200, 300, std::nullopt);
	pDaily2->AddFlag(QUEST_FLAG_TYPE_DAILY);
	pDaily2->AddFlag(QUEST_FLAG_GRANTED_FROM_BOARD);
	InitQuestStepDefeats(pDaily2, 1, 1, 30);
	CQuestDescription::ms_aData.add(pDaily2);

	// ─── 主线剧情任务 ──────────────────────────────────────
	//
	// Prologue: 星的消逝
	//
	CQuestDescription* pPrologue = new CQuestDescription();
	pPrologue->Init(401, "星的消逝", 0, 0, std::nullopt, 20);
	pPrologue->AddFlag(QUEST_FLAG_TYPE_MAIN);
	pPrologue->AddFlag(QUEST_FLAG_GRANTED_FROM_CHAIN);
	pPrologue->AddFlag(QUEST_FLAG_CANT_REFUSE);
	InitQuestStepMoveAction(pPrologue, 1, vec2(384, 256), 0, "前往镇上的图书馆，与管理员Flower交谈");
	CQuestDescription::ms_aData.add(pPrologue);

	CQuestDescription* pRep1 = new CQuestDescription();
	pRep1->Init(402, "打听消息", 100, 200, std::nullopt, 30);
	pRep1->InitPrevousQuestID(401);
	pRep1->AddFlag(QUEST_FLAG_TYPE_MAIN);
	pRep1->AddFlag(QUEST_FLAG_GRANTED_FROM_CHAIN);
	InitQuestStepDefeats(pRep1, 1, 2, 10);
	InitQuestStepDefeats(pRep1, 2, 3, 5);
	CQuestDescription::ms_aData.add(pRep1);

	CQuestDescription* pRep2 = new CQuestDescription();
	pRep2->Init(403, "地区委托", 200, 400, std::nullopt, 50);
	pRep2->InitPrevousQuestID(402);
	pRep2->AddFlag(QUEST_FLAG_TYPE_MAIN);
	pRep2->AddFlag(QUEST_FLAG_GRANTED_FROM_CHAIN);
	InitQuestStepDefeats(pRep2, 1, 4, 15);
	InitQuestStepDefeats(pRep2, 2, 5, 8);
	CQuestDescription::ms_aData.add(pRep2);
}

void CQuestManager::InitQuestBoards()
{
}

void CQuestManager::OnPlayerLogin(CPlayer* pPlayer)
{
	if(!pPlayer)
		return;

	for(CQuestDescription* pDesc : CQuestDescription::ms_aData)
	{
		if(!pDesc || !pDesc->CanBeGranted())
			continue;

		if(pDesc->HasFlag(QUEST_FLAG_GRANTED_FROM_CHAIN))
			continue;

		if(pDesc->HasFlag(QUEST_FLAG_GRANTED_FROM_NPC))
			continue;

		CPlayerQuest* pQuest = new CPlayerQuest(GS(), pDesc->GetID(), pPlayer->GetCID());
		pQuest->Init(QuestState_NoAccepted);
	}

	// Auto-grant prologue quest (401) to all new players
	{
		CQuestDescription *pPrologue = CQuestDescription::Find(401);
		if(pPrologue && !GetQuest(pPlayer->GetCID(), 401))
		{
			CPlayerQuest* pQuest = new CPlayerQuest(GS(), 401, pPlayer->GetCID());
			pQuest->Init(QuestState_Accepted);
			GS()->SendChatTo(pPlayer->GetCID(), "=== 主线任务 ===");
			GS()->SendChatTo(pPlayer->GetCID(), "[星的消逝] - 前往图书馆与 Flower 交谈");
			GS()->SendChatTo(pPlayer->GetCID(), "================");
		}
	}
}

void CQuestManager::OnTick()
{
	UpdatePlayerQuests();
	UpdatePlayerObjectives();
	if(m_pScenarioManager)
		m_pScenarioManager->Update();
}

void CQuestManager::OnCharacterSpawn(CPlayer* pPlayer)
{
	if(!pPlayer)
		return;
}

void CQuestManager::ShowQuestList(CPlayer* pPlayer) const
{
	if(!pPlayer)
		return;

	GS()->SendChatLoc(pPlayer->GetCID(), "quest.list_title", "==== 任务列表 ====");

	bool HasQuests = false;
	for(auto* pQuest : GetQuests(pPlayer->GetCID()))
	{
		if(!pQuest)
			continue;

		CQuestDescription* pInfo = pQuest->Info();
		if(!pInfo)
			continue;

		HasQuests = true;
		const char* pStatus = "";
		if(pQuest->IsCompleted())
			pStatus = "[✓]";
		else if(pQuest->IsAccepted())
			pStatus = "[▶]";
		else
			pStatus = "[○]";

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "%s %s (步骤 %d/%d)",
			pStatus, pInfo->GetName(), pQuest->GetStepPos(), pInfo->GetChainLength());
		GS()->SendChatTo(pPlayer->GetCID(), aBuf);

		// Show step details for accepted quests
		if(pQuest->IsAccepted())
		{
			for(CQuestStep* pStep : pQuest->m_vObjectives)
			{
				if(!pStep)
					continue;

				char aTasks[512];
				pStep->FormatStringTasks(aTasks, sizeof(aTasks));

				if(aTasks[0])
				{
					// Split by newlines and print each line
					char aLine[256];
					int LineStart = 0;
					int Len = str_length(aTasks);
					for(int c = 0; c <= Len; c++)
					{
						if(aTasks[c] == '\n' || aTasks[c] == '\0')
						{
							int LineLen = c - LineStart;
							if(LineLen > 0)
							{
								str_copy(aLine, aTasks + LineStart,
									minimum(LineLen + 1, (int)sizeof(aLine)));

								char aLinePadded[256];
								str_format(aLinePadded, sizeof(aLinePadded), "  %s", aLine);
								GS()->SendChatTo(pPlayer->GetCID(), aLinePadded);
							}
							LineStart = c + 1;
						}
					}
				}
			}
		}
	}

	if(!HasQuests)
		GS()->SendChatLoc(pPlayer->GetCID(), "quest.list_empty", "暂无任务");

	GS()->SendChatLoc(pPlayer->GetCID(), "quest.list_end", "==================");
}

void CQuestManager::ShowQuestBoardList(CPlayer* pPlayer) const
{
	if(!pPlayer)
		return;

	GS()->SendChatLoc(pPlayer->GetCID(), "quest.board_title", "==== 任务公告板 ====");

	for(CQuestDescription* pDesc : CQuestDescription::ms_aData)
	{
		if(!pDesc || !pDesc->CanBeGrantedByBoard())
			continue;

		CPlayerQuest* pQuest = GetQuest(pPlayer->GetCID(), pDesc->GetID());
		if(!pQuest)
			continue;

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "%s - 奖励: %d金币 %d经验",
			pDesc->GetName(), pDesc->Reward().GetGold(), pDesc->Reward().GetExperience());
		GS()->SendChatTo(pPlayer->GetCID(), aBuf);
	}

	GS()->SendChatLoc(pPlayer->GetCID(), "quest.board_end", "======================");
}

void CQuestManager::AcceptQuest(CPlayer* pPlayer, int QuestID)
{
	if(!pPlayer)
		return;

	CPlayerQuest* pQuest = GetQuest(pPlayer->GetCID(), QuestID);
	if(!pQuest)
		return;

	pQuest->Accept();

	// Spawn quest mobs for the first step if it has defeat objectives
	if(CQuestDescription* pInfo = pQuest->Info())
	{
		for(CQuestStep* pStep : pQuest->m_vObjectives)
		{
			if(!pStep || pStep->m_StepComplete)
				continue;

			// Spawn quest mobs for each defeat requirement in this step
			for(auto& Defeat : pStep->m_Bot.m_vRequiredDefeats)
			{
				CMMOManager* pMMO = GS()->Core()->GetMMOManager();
				if(!pMMO)
					continue;

				vec2 SpawnPos = pPlayer->GetCharacter()
					? pPlayer->GetCharacter()->GetPos() + vec2(200.f, 0.f)
					: vec2(400.f, 400.f);

				// Try to spawn near zone center if zone is specified
				if(Defeat.m_ZoneName[0])
				{
					const SMMOZoneDef *pZone = SMMOZoneDef::Find(
						pPlayer->GetCurrentWorldID(), Defeat.m_ZoneName);
					if(pZone)
					{
						int ZoneW = maximum(1, pZone->m_X2 - pZone->m_X1);
						int ZoneH = maximum(1, pZone->m_Y2 - pZone->m_Y1);
						SpawnPos = vec2(
							(float)(pZone->m_X1 + (random_int() % ZoneW)),
							(float)(pZone->m_Y1 + (random_int() % ZoneH))
						);
					}
				}

				int CID = pMMO->SpawnQuestMob(
					Defeat.m_BotID,
					SpawnPos,
					QuestID,
					pQuest->GetStepPos(),
					pPlayer->GetCID());

				if(CID >= 0)
				{
					dbg_msg("quest", "Spawned quest mob for quest %d, def %d",
						QuestID, Defeat.m_BotID);
				}
			}
		}
	}
}

void CQuestManager::RefuseQuest(CPlayer* pPlayer, int QuestID)
{
	if(!pPlayer)
		return;

	CPlayerQuest* pQuest = GetQuest(pPlayer->GetCID(), QuestID);
	if(!pQuest)
		return;

	pQuest->Refuse();
}

void CQuestManager::RestartQuest(CPlayer* pPlayer, int QuestID)
{
	if(!pPlayer)
		return;

	CPlayerQuest* pQuest = GetQuest(pPlayer->GetCID(), QuestID);
	if(!pQuest)
		return;

	pQuest->Restart();
}

void CQuestManager::TryAcceptNextQuestChain(CPlayer* pPlayer, int BaseQuestID) const
{
	if(!pPlayer)
		return;

	CQuestDescription* pBaseDesc = CQuestDescription::Find(BaseQuestID);
	if(!pBaseDesc)
		return;

	CQuestDescription* pNextDesc = pBaseDesc->GetNextQuest();
	if(!pNextDesc)
		return;

	if(!pNextDesc->CanBeGrantedByChain())
		return;

	CPlayerQuest* pQuest = GetQuest(pPlayer->GetCID(), pNextDesc->GetID());
	if(pQuest)
		return;

	pQuest = new CPlayerQuest(GS(), pNextDesc->GetID(), pPlayer->GetCID());
	pQuest->Init(QuestState_NoAccepted);
	pQuest->Accept();
}

CPlayerQuest* CQuestManager::GetQuest(int ClientID, int QuestID) const
{
	CPlayerQuest** ppQuest = CPlayerQuest::ms_aPlayerQuests[ClientID].get(QuestID);
	return ppQuest ? *ppQuest : nullptr;
}

array<CPlayerQuest*> CQuestManager::GetQuests(int ClientID) const
{
	array<CPlayerQuest*> vQuests{};
	CPlayerQuest::ms_aPlayerQuests[ClientID].for_each([](CPlayerQuest*& pQuest, void* pUser) {
		array<CPlayerQuest*>* pArr = static_cast<array<CPlayerQuest*>*>(pUser);
		if(pQuest)
			pArr->add(pQuest);
	}, &vQuests);
	return vQuests;
}

void CQuestManager::AddBoard(CEntityQuestBoard* pBoard)
{
	if(pBoard)
		m_vpBoards.add(pBoard);
}

void CQuestManager::RemoveBoard(CEntityQuestBoard* pBoard)
{
	for(unsigned i = 0; i < m_vpBoards.size(); i++)
	{
		if(m_vpBoards[i] == pBoard)
		{
			m_vpBoards.remove_index(i);
			break;
		}
	}
}

void CQuestManager::OnPlayerKill(CPlayer* pPlayer, int VictimID, const char *pZoneName)
{
	if(!pPlayer)
		return;

	for(auto* pQuest : GetQuests(pPlayer->GetCID()))
	{
		if(!pQuest || !pQuest->IsAccepted())
			continue;

		for(auto* pStep : pQuest->m_vObjectives)
		{
			if(pStep)
				pStep->AppendDefeatProgress(VictimID, pZoneName);
		}
	}

	// NOTE: scenario manager not yet updated for the VictimID→DefID change
	// The old code GS()->m_apPlayers[VictimID] assumed VictimID was a CID, but now
	// it's a mob definition ID. This path is unreachable until scenarios are active.
	//if(m_pScenarioManager)
	//	m_pScenarioManager->OnPlayerKill(GS()->m_apPlayers[VictimID], pPlayer, 0);
}

CPlayerQuest* CQuestManager::FindPlayerQuest(int ClientID, int QuestID)
{
	CPlayerQuest** ppQuest = CPlayerQuest::ms_aPlayerQuests[ClientID].get(QuestID);
	return ppQuest ? *ppQuest : nullptr;
}

void CQuestManager::UpdatePlayerQuests()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		for(auto* pQuest : GetQuests(i))
		{
			if(pQuest)
				pQuest->Update();
		}
	}
}

void CQuestManager::UpdatePlayerObjectives()
{
}

void CQuestManager::RequestPersist(int ClientID)
{
	for(auto* pQuest : GetQuests(ClientID))
	{
		if(pQuest && pQuest->IsAccepted())
			pQuest->Datafile().Save();
	}
}

static void ConQuestList(IConsole::IResult* pResult, void* pUser)
{
	CCommandManager::SCommandContext* pCtx = static_cast<CCommandManager::SCommandContext*>(pUser);
	CGameContext* pGame = static_cast<CGameContext*>(pCtx->m_pContext);
	if(!pGame || !pGame->Core() || !pGame->Core()->QuestManager())
		return;

	CPlayer* pPlayer = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pPlayer)
		return;

	pGame->Core()->QuestManager()->ShowQuestList(pPlayer);
}

static void ConQuestAccept(IConsole::IResult* pResult, void* pUser)
{
	CCommandManager::SCommandContext* pCtx = static_cast<CCommandManager::SCommandContext*>(pUser);
	CGameContext* pGame = static_cast<CGameContext*>(pCtx->m_pContext);
	if(!pGame || !pGame->Core() || !pGame->Core()->QuestManager())
		return;

	CPlayer* pPlayer = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pPlayer)
		return;

	int QuestID = pResult->GetInteger(0);
	pGame->Core()->QuestManager()->AcceptQuest(pPlayer, QuestID);
}

static void ConQuestRefuse(IConsole::IResult* pResult, void* pUser)
{
	CCommandManager::SCommandContext* pCtx = static_cast<CCommandManager::SCommandContext*>(pUser);
	CGameContext* pGame = static_cast<CGameContext*>(pCtx->m_pContext);
	if(!pGame || !pGame->Core() || !pGame->Core()->QuestManager())
		return;

	CPlayer* pPlayer = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pPlayer)
		return;

	int QuestID = pResult->GetInteger(0);
	pGame->Core()->QuestManager()->RefuseQuest(pPlayer, QuestID);
}

static void ConScenarioStart(IConsole::IResult* pResult, void* pUser)
{
	CCommandManager::SCommandContext* pCtx = static_cast<CCommandManager::SCommandContext*>(pUser);
	CGameContext* pGame = static_cast<CGameContext*>(pCtx->m_pContext);
	if(!pGame || !pGame->Core() || !pGame->Core()->QuestManager())
		return;

	CPlayer* pPlayer = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pPlayer)
		return;

	int ScenarioID = pResult->GetInteger(0);
	CScenarioManager* pMgr = pGame->Core()->QuestManager()->GetScenarioManager();
	if(!pMgr)
	{
		pGame->SendChatTo(pCtx->m_ClientID, "Scenario system not available");
		return;
	}

	// Check if player is already in a scenario
	CScenarioInstance* pExisting = pMgr->GetInstance(pPlayer);
	if(pExisting)
	{
		pGame->SendChatTo(pCtx->m_ClientID, "你已经在一个副本中");
		return;
	}

	CScenarioInstance* pInstance = pMgr->CreateInstance(ScenarioID, pPlayer);
	if(!pInstance)
	{
		pGame->SendChatTo(pCtx->m_ClientID, "副本不存在");
		return;
	}

	pInstance->Start();
}

void CQuestManager::RegisterChatCommands(class CCommandManager* pManager)
{
	if(!pManager)
		return;

	CGameContext* pGame = GS();
	pManager->AddCommand("quest", "cmd.quest.help", "", ConQuestList, pGame);
	// quest_accept/refuse 仅通过投票菜单 ccv_questaccept / menusetquest
	pManager->AddCommand("scenario", "启动副本", "i", ConScenarioStart, pGame);
}

static void ConVoteMenuSetQuest(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = static_cast<CCommandManager::SCommandContext *>(pUser);
	CGameContext *pGame = static_cast<CGameContext *>(pCtx->m_pContext);
	if(!pGame || !pGame->Core() || !pGame->Core()->VoteMenuManager())
		return;
	SPlayerVote *pV = pGame->Core()->VoteMenuManager()->GetPlayerVote(pCtx->m_ClientID);
	pV->m_QuestIdx = pResult->GetInteger(0);
	pV->m_LastPage = PAGE_QUESTS;
	pV->m_Page = PAGE_QUEST_DETAIL;
	pGame->Core()->VoteMenuManager()->ClearVotes(pCtx->m_ClientID);
}

static void ConVoteQuestAccept(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = static_cast<CCommandManager::SCommandContext *>(pUser);
	CGameContext *pGame = static_cast<CGameContext *>(pCtx->m_pContext);
	if(!pGame || !pGame->Core() || !pGame->Core()->QuestManager())
		return;
	CPlayer *pPlayer = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pPlayer)
		return;
	const int QuestID = pResult->GetInteger(0);
	pGame->Core()->QuestManager()->AcceptQuest(pPlayer, QuestID);
	SPlayerVote *pV = pGame->Core()->VoteMenuManager()->GetPlayerVote(pCtx->m_ClientID);
	pV->m_QuestIdx = QuestID;
	pV->m_Page = PAGE_QUEST_DETAIL;
	pGame->Core()->VoteMenuManager()->ClearVotes(pCtx->m_ClientID);
}

void CQuestManager::RegisterVoteCommands(class CCommandManager* pManager)
{
	if(!pManager || !GS())
		return;
	pManager->AddVoteCommand("menusetquest", "", "i", ConVoteMenuSetQuest, GS());
	pManager->AddVoteCommand("questaccept", "", "i", ConVoteQuestAccept, GS());
}

bool CQuestManager::OnVoteMenuPage(int ClientID, int Page)
{
	if(Page != PAGE_QUESTS && Page != PAGE_QUEST_DETAIL)
		return false;
	if(!GS() || !Core() || !Core()->VoteMenuManager())
		return false;

	CVoteMenuManager *pVote = Core()->VoteMenuManager();
	SPlayerVote *pSVote = pVote->GetPlayerVote(ClientID);

	if(Page == PAGE_QUESTS)
	{
		pVote->SetVoteLastPage(PAGE_MENU);
		BuildQuestListPage(ClientID);
	}
	else if(Page == PAGE_QUEST_DETAIL)
	{
		pVote->SetVoteLastPage(pSVote->m_LastPage >= 0 ? pSVote->m_LastPage : PAGE_QUESTS);
		BuildQuestDetailPage(ClientID, pSVote->m_QuestIdx);
	}
	return true;
}

void CQuestManager::BuildQuestListPage(int ClientID)
{
	CPlayer* pPlayer = GS()->m_apPlayers[ClientID];
	if(!pPlayer || !Core() || !Core()->VoteMenuManager())
		return;

	CVoteMenuManager* pVote = Core()->VoteMenuManager();
	pVote->SetVoteBuildClientID(ClientID);
	pVote->ClearVoteOptions(ClientID);

	CVoteWrapper V(ClientID, GS(), pVote);
	V.GroupTitle("任务列表");

	array<CPlayerQuest*> vQuests = GetQuests(ClientID);
	if(vQuests.size() == 0)
	{
		V.Info("暂无任务");
	}
	else
	{
		for(unsigned i = 0; i < vQuests.size(); i++)
		{
			CPlayerQuest* pQuest = vQuests[i];
			if(!pQuest)
				continue;

			CQuestDescription* pInfo = pQuest->Info();
			if(!pInfo)
				continue;

			char aDesc[VOTE_DESC_LENGTH];
			char aCmd[VOTE_CMD_LENGTH];

			const char* pStatus = "";
			if(pQuest->IsCompleted())
				pStatus = "✓";
			else if(pQuest->IsAccepted())
				pStatus = "▶";
			else
				pStatus = "○";

			str_format(aDesc, sizeof(aDesc), "%s %s (步骤 %d/%d)",
				pStatus, pInfo->GetName(), pQuest->GetStepPos(), pInfo->GetChainLength());
			str_format(aCmd, sizeof(aCmd), "ccv_menusetquest %d", pInfo->GetID());
			V.Option(aCmd, aDesc);
		}
	}

	V.Footer();
}

void CQuestManager::BuildQuestDetailPage(int ClientID, int QuestIdx)
{
	CPlayer* pPlayer = GS()->m_apPlayers[ClientID];
	if(!pPlayer || !Core() || !Core()->VoteMenuManager())
		return;

	CPlayerQuest* pQuest = GetQuest(ClientID, QuestIdx);
	if(!pQuest)
		return;

	CQuestDescription* pInfo = pQuest->Info();
	if(!pInfo)
		return;

	CVoteMenuManager* pVote = Core()->VoteMenuManager();
	pVote->SetVoteBuildClientID(ClientID);
	pVote->ClearVoteOptions(ClientID);

	CVoteWrapper V(ClientID, GS(), pVote);

	char aHeader[VOTE_DESC_LENGTH];
	str_format(aHeader, sizeof(aHeader), "任务: %s", pInfo->GetName());
	V.GroupTitle(aHeader);

	char aReward[VOTE_DESC_LENGTH];
	str_format(aReward, sizeof(aReward), "奖励: 金币 %d | 经验 %d",
		pInfo->Reward().GetGold(), pInfo->Reward().GetExperience());
	V.Info(aReward);

	V.GroupLine();
	V.GroupTitle("当前步骤");

	if(pQuest->IsAccepted())
	{
		for(CQuestStep* pStep : pQuest->m_vObjectives)
		{
			if(!pStep)
				continue;

			char aFormattedTasks[VOTE_DESC_LENGTH * 4];
			pStep->FormatStringTasks(aFormattedTasks, sizeof(aFormattedTasks));

			if(aFormattedTasks[0])
			{
				char aLine[VOTE_DESC_LENGTH];
				int LineStart = 0;
				int Len = str_length(aFormattedTasks);
				for(int c = 0; c <= Len; c++)
				{
					if(aFormattedTasks[c] == '\n' || aFormattedTasks[c] == '\0')
					{
						int LineLen = c - LineStart;
						if(LineLen > 0)
						{
							str_copy(aLine, aFormattedTasks + LineStart,
								minimum((int)(LineLen + 1), (int)VOTE_DESC_LENGTH));
							V.Info(aLine);
						}
						LineStart = c + 1;
					}
				}
			}
		}
	}
	else if(pQuest->IsCompleted())
	{
		V.Info("任务已完成");
	}
	else
	{
		V.Info("任务未接受");
		char aCmd[VOTE_CMD_LENGTH];
		str_format(aCmd, sizeof(aCmd), "ccv_questaccept %d", pInfo->GetID());
		V.Option(aCmd, "接受任务");
	}

	V.Footer();
}

void CQuestManager::TryTalkNpc(class CPlayer* pPlayer, const char* pNpcId, float NpcPosX, float NpcPosY, int NpcWorld)
{
	if(!pPlayer || !pNpcId || !GS())
		return;

	// Show dialog (data-driven from npcs.json)
	if(Core() && Core()->DialogManager())
		Core()->DialogManager()->TryTalk(pPlayer, pNpcId, NpcPosX, NpcPosY, NpcWorld);

	// Check active quest steps that require a dialog with this NPC
	for(auto* pQuest : GetQuests(pPlayer->GetCID()))
	{
		if(!pQuest || !pQuest->IsAccepted())
			continue;

		for(auto* pStep : pQuest->m_vObjectives)
		{
			if(!pStep || pStep->m_StepComplete)
				continue;

			pStep->CompleteDialog(pNpcId);

			// Auto-finish if the step is now complete
			if(pStep->IsComplete())
				pStep->Finish();
		}
	}
}

bool CQuestManager::HasTravelUnlock(class CPlayer* pPlayer, const char* pQuestName)
{
	if(!pPlayer)
		return true;

	for(auto* pQuest : GetQuests(pPlayer->GetCID()))
	{
		if(pQuest && pQuest->IsCompleted())
		{
			CQuestDescription* pInfo = pQuest->Info();
			if(pInfo && str_comp(pInfo->GetName(), pQuestName) == 0)
				return true;
		}
	}
	return false;
}