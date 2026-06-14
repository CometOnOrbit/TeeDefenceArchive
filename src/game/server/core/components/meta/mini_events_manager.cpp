#include <engine/shared/jsonparser.h>

#include <game/server/core/components/meta/mini_events_manager.h>
#include <game/server/gamecontext.h>

CMiniEventsManager::CMiniEventsManager()
{
	m_NumEvents = 0;
	m_RollIntervalSec = 480;
	m_ChancePercent = 30;
	m_ActiveIdx = -1;
	m_EndTick = 0;
	m_NextRollTick = 0;
	mem_zero(m_aEvents, sizeof(m_aEvents));
}

void CMiniEventsManager::LoadDefs()
{
	m_NumEvents = 0;
	if(!Storage())
		return;

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseFile("server_content/mini_events.json", Storage());
	if(!pRoot)
	{
		dbg_msg("mini", "mini_events.json: %s", Parser.Error());
		return;
	}

	if((*pRoot)["roll_interval_sec"].type == json_integer)
		m_RollIntervalSec = maximum(60, (int)(*pRoot)["roll_interval_sec"].u.integer);
	if((*pRoot)["chance_percent"].type == json_integer)
		m_ChancePercent = clamp((int)(*pRoot)["chance_percent"].u.integer, 1, 100);

	const json_value &Arr = (*pRoot)["events"];
	if(Arr.type != json_array)
		return;

	for(unsigned i = 0; i < Arr.u.array.length && m_NumEvents < 8; i++)
	{
		const json_value &E = Arr[(int)i];
		if(E.type != json_object)
			continue;
		SMiniEventDef &Def = m_aEvents[m_NumEvents++];
		mem_zero(&Def, sizeof(Def));
		if(E["id"].type == json_string)
			str_copy(Def.m_aId, E["id"].u.string.ptr, sizeof(Def.m_aId));
		if(E["title"].type == json_string)
			str_copy(Def.m_aTitle, E["title"].u.string.ptr, sizeof(Def.m_aTitle));
		if(E["type"].type == json_string)
			str_copy(Def.m_aType, E["type"].u.string.ptr, sizeof(Def.m_aType));
		if(E["bonus_percent"].type == json_integer)
			Def.m_BonusPercent = (int)E["bonus_percent"].u.integer;
		if(E["duration_sec"].type == json_integer)
			Def.m_DurationSec = maximum(30, (int)E["duration_sec"].u.integer);
	}
	dbg_msg("mini", "loaded %d mini events", m_NumEvents);
}

void CMiniEventsManager::OnInitWorld(const char *pWhereLocalWorld)
{
	(void)pWhereLocalWorld;
	LoadDefs();
	if(GS() && GS()->Server())
		m_NextRollTick = GS()->Server()->Tick() + GS()->Server()->TickSpeed() * m_RollIntervalSec;
}

void CMiniEventsManager::StartEvent(int Idx)
{
	if(Idx < 0 || Idx >= m_NumEvents || !GS())
		return;
	m_ActiveIdx = Idx;
	const int DurTicks = GS()->Server()->TickSpeed() * m_aEvents[Idx].m_DurationSec;
	m_EndTick = GS()->Server()->Tick() + DurTicks;
	GS()->SendChatAllLocF("mini.event.start", u8"限时活动：%s（%d 秒）", m_aEvents[Idx].m_aTitle, m_aEvents[Idx].m_DurationSec);
}

void CMiniEventsManager::TryRollEvent()
{
	if(m_NumEvents <= 0 || !GS())
		return;
	if(m_ActiveIdx >= 0 && GS()->Server()->Tick() < m_EndTick)
		return;
	if((rand() % 100) + 1 > m_ChancePercent)
		return;
	StartEvent(rand() % m_NumEvents);
}

void CMiniEventsManager::OnTick()
{
	if(!GS() || !GS()->Server() || m_NumEvents <= 0)
		return;
	const int Now = GS()->Server()->Tick();
	if(m_ActiveIdx >= 0 && Now >= m_EndTick)
	{
		GS()->SendChatAllLoc("mini.event.end", u8"限时活动已结束。");
		m_ActiveIdx = -1;
	}
	if(Now >= m_NextRollTick)
	{
		TryRollEvent();
		m_NextRollTick = Now + GS()->Server()->TickSpeed() * m_RollIntervalSec;
	}
}

const char *CMiniEventsManager::ActiveTitle() const
{
	if(m_ActiveIdx < 0 || m_ActiveIdx >= m_NumEvents)
		return "";
	return m_aEvents[m_ActiveIdx].m_aTitle;
}

int CMiniEventsManager::GetLootBonusPercent() const
{
	if(m_ActiveIdx < 0 || str_comp(m_aEvents[m_ActiveIdx].m_aType, "loot") != 0)
		return 0;
	return m_aEvents[m_ActiveIdx].m_BonusPercent;
}

int CMiniEventsManager::GetMiningBonusPercent() const
{
	if(m_ActiveIdx < 0 || str_comp(m_aEvents[m_ActiveIdx].m_aType, "mining") != 0)
		return 0;
	return m_aEvents[m_ActiveIdx].m_BonusPercent;
}

int CMiniEventsManager::GetRepairDiscountPercent() const
{
	if(m_ActiveIdx < 0 || str_comp(m_aEvents[m_ActiveIdx].m_aType, "repair") != 0)
		return 0;
	return m_aEvents[m_ActiveIdx].m_BonusPercent;
}
