#include <engine/shared/jsonwriter.h>
#include <engine/shared/jsonparser.h>
#include <engine/shared/protocol.h>
#include <engine/server.h>
#include <game/server/player.h>
#include "quest_data.h"
#include "datafile_progress.h"

#define PRINT_QUEST_PREFIX "quest_system"

void QuestDatafile::Create() const
{
	if(!m_pQuest)
		return;

	CPlayer* pPlayer = m_pQuest->GetPlayer();
	if(!pPlayer)
		return;

	if(m_pQuest->m_State != QuestState_Accepted)
		return;

	char aBuf[4096];
	mem_zero(aBuf, sizeof(aBuf));

	str_format(aBuf, sizeof(aBuf), "{\"current_step\":%d,\"steps\":[", m_pQuest->m_Step);

	m_pQuest->Info()->PreparePlayerObjectives(m_pQuest->GS(), m_pQuest->m_Step, m_pQuest->m_ClientID, m_pQuest->m_vObjectives);

	bool FirstStep = true;
	for(auto* pStep : m_pQuest->m_vObjectives)
	{
		if(!FirstStep)
			str_append(aBuf, ",", sizeof(aBuf));
		FirstStep = false;

		str_format(aBuf, sizeof(aBuf), "%s{\"quest_bot_id\":%d,\"state\":false,\"defeat\":[", aBuf, pStep->m_Bot.m_ID);

		bool FirstDefeat = true;
		for(auto& p : pStep->m_Bot.m_vRequiredDefeats)
		{
			pStep->m_aMobProgress[p.m_BotID].m_Count = 0;
			if(!FirstDefeat)
				str_append(aBuf, ",", sizeof(aBuf));
			FirstDefeat = false;
			str_format(aBuf, sizeof(aBuf), "%s{\"id\":%d,\"count\":0,\"complete\":false}", aBuf, p.m_BotID);
		}

		str_append(aBuf, "],\"move_to\":[", sizeof(aBuf));
		bool FirstMove = true;
		for(unsigned i = 0; i < pStep->m_Bot.m_vRequiredMoveAction.size(); i++)
		{
			if(!FirstMove)
				str_append(aBuf, ",", sizeof(aBuf));
			FirstMove = false;
			str_append(aBuf, "{\"complete\":false}", sizeof(aBuf));
		}
		str_append(aBuf, "]}", sizeof(aBuf));
		pStep->m_aMoveActionProgress.resize(pStep->m_Bot.m_vRequiredMoveAction.size(), false);
	}

	str_append(aBuf, "]}", sizeof(aBuf));

	for(auto& pStep : m_pQuest->m_vObjectives)
		pStep->Update();

	char aFilename[512];
	GetFilename(aFilename, sizeof(aFilename));
	IOHANDLE File = io_open(aFilename, IOFLAG_WRITE);
	if(File)
	{
		io_write(File, aBuf, str_length(aBuf));
		io_close(File);
	}
}

void QuestDatafile::Load() const
{
	if(!m_pQuest || m_pQuest->m_State != QuestState_Accepted)
		return;

	char aFilename[512];
	GetFilename(aFilename, sizeof(aFilename));
	IOHANDLE File = io_open(aFilename, IOFLAG_READ);
	if(!File)
	{
		Create();
		return;
	}

	char aBuf[4096];
	int Size = io_read(File, aBuf, sizeof(aBuf) - 1);
	io_close(File);
	aBuf[Size] = 0;

	CJsonParser Parser;
	json_value* pRoot = Parser.ParseString(aBuf, "quest_save");
	if(!pRoot || pRoot->type != json_object)
	{
		Create();
		return;
	}

	const json_value& Root = *pRoot;
	m_pQuest->m_Step = Root["current_step"].type == json_integer ? (int)Root["current_step"].u.integer : 1;
	m_pQuest->Info()->PreparePlayerObjectives(m_pQuest->GS(), m_pQuest->m_Step, m_pQuest->m_ClientID, m_pQuest->m_vObjectives);

	const json_value& Steps = Root["steps"];
	if(Steps.type != json_array || Steps.u.array.length != m_pQuest->m_vObjectives.size())
	{
		dbg_msg(PRINT_QUEST_PREFIX, "Reinitialization... Player save file has a different size of steps!");
		Create();
		return;
	}

	for(unsigned i = 0; i < Steps.u.array.length; i++)
	{
		const json_value& Step = Steps[(int)i];
		auto* pWorkedNode = m_pQuest->m_vObjectives[i];
		pWorkedNode->m_StepComplete = Step["state"].type == json_boolean && Step["state"].u.boolean;
		if(pWorkedNode->m_StepComplete)
			continue;

		const json_value& Defeat = Step["defeat"];
		if(Defeat.type == json_array)
		{
			if(Defeat.u.array.length != pWorkedNode->m_Bot.m_vRequiredDefeats.size())
			{
				dbg_msg(PRINT_QUEST_PREFIX, "Reinitialization... Player save file has a defeat value, but it is not present in the data!");
				Create();
				return;
			}
			for(unsigned j = 0; j < Defeat.u.array.length; j++)
			{
				const json_value& D = Defeat[(int)j];
				int ID = D["id"].type == json_integer ? (int)D["id"].u.integer : 0;
				pWorkedNode->m_aMobProgress[ID].m_Count = D["count"].type == json_integer ? (int)D["count"].u.integer : 0;
				pWorkedNode->m_aMobProgress[ID].m_Complete = D["complete"].type == json_boolean && D["complete"].u.boolean;
			}
		}

		const json_value& MoveTo = Step["move_to"];
		if(MoveTo.type == json_array)
		{
			if(MoveTo.u.array.length != pWorkedNode->m_Bot.m_vRequiredMoveAction.size())
			{
				dbg_msg(PRINT_QUEST_PREFIX, "Reinitialization... Player save file has a move_to value, but it is not present in the data!");
				Create();
				return;
			}
			pWorkedNode->m_aMoveActionProgress.resize(MoveTo.u.array.length, false);
			for(unsigned j = 0; j < MoveTo.u.array.length; j++)
				pWorkedNode->m_aMoveActionProgress[j] = MoveTo[(int)j]["complete"].type == json_boolean && MoveTo[(int)j]["complete"].u.boolean;
		}

		pWorkedNode->m_MarkedForDestroy = false;
	}

	for(auto& pStep : m_pQuest->m_vObjectives)
	{
		if(!pStep->m_StepComplete)
			pStep->Update();
	}

	Save();
}

bool QuestDatafile::Save() const
{
	if(!m_pQuest || m_pQuest->m_State != QuestState_Accepted)
		return false;

	char aBuf[4096];
	mem_zero(aBuf, sizeof(aBuf));

	str_format(aBuf, sizeof(aBuf), "{\"current_step\":%d,\"steps\":[", m_pQuest->m_Step);

	bool FirstStep = true;
	for(auto* pStep : m_pQuest->m_vObjectives)
	{
		if(!FirstStep)
			str_append(aBuf, ",", sizeof(aBuf));
		FirstStep = false;

		str_format(aBuf, sizeof(aBuf), "%s{\"quest_bot_id\":%d,\"state\":%s,\"defeat\":[", aBuf, pStep->m_Bot.m_ID, pStep->m_StepComplete ? "true" : "false");

		bool FirstDefeat = true;
		for(auto& p : pStep->m_aMobProgress)
		{
			if(!FirstDefeat)
				str_append(aBuf, ",", sizeof(aBuf));
			FirstDefeat = false;
			str_format(aBuf, sizeof(aBuf), "%s{\"id\":%d,\"count\":%d,\"complete\":%s}", aBuf, p.first, p.second.m_Count, p.second.m_Complete ? "true" : "false");
		}

		str_append(aBuf, "],\"move_to\":[", sizeof(aBuf));
		bool FirstMove = true;
		for(bool Complete : pStep->m_aMoveActionProgress)
		{
			if(!FirstMove)
				str_append(aBuf, ",", sizeof(aBuf));
			FirstMove = false;
			str_format(aBuf, sizeof(aBuf), "%s{\"complete\":%s}", aBuf, Complete ? "true" : "false");
		}
		str_append(aBuf, "],\"dialogs\":[", sizeof(aBuf));
		bool FirstDialog = true;
		for(auto& dp : pStep->m_aDialogProgress)
		{
			if(!FirstDialog)
				str_append(aBuf, ",", sizeof(aBuf));
			FirstDialog = false;
			str_format(aBuf, sizeof(aBuf), "%s{\"npc\":\"%s\",\"done\":%s}", aBuf, dp.first.c_str(), dp.second.m_DialogDone ? "true" : "false");
		}
		str_append(aBuf, "]}", sizeof(aBuf));
	}

	str_append(aBuf, "]}", sizeof(aBuf));

	char aFilename[512];
	GetFilename(aFilename, sizeof(aFilename));
	IOHANDLE File = io_open(aFilename, IOFLAG_WRITE);
	if(!File)
		return false;
	io_write(File, aBuf, str_length(aBuf));
	io_close(File);
	return true;
}

void QuestDatafile::Delete() const
{
	if(!m_pQuest)
		return;
	char aFilename[512];
	GetFilename(aFilename, sizeof(aFilename));
	fs_remove(aFilename);
}

const char* QuestDatafile::GetFilename(char* pBuf, int BufSize) const
{
	if(!m_pQuest)
		return "";
	CPlayer* pPlayer = m_pQuest->GetPlayer();
	if(!pPlayer)
		return "";
	str_format(pBuf, BufSize, "server_data/account_quests/%d-%lld.json", m_pQuest->GetID(), pPlayer->GetAccountId());
	return pBuf;
}