/* TeeDefense — server-side helpers for generic ExtraHud snap slots. */
#ifndef GAME_EXTRA_HUD_H
#define GAME_EXTRA_HUD_H

#include <base/math.h>
#include <engine/server.h>
#include <generated/protocol.h>

enum
{
	EXTRAHUD_SLOT_PRIMARY = 0,
	EXTRAHUD_SLOT_SECONDARY = 1,

	EXTRAHUD_PROGRESS_CUR = 0,
	EXTRAHUD_PROGRESS_MAX = 1,
	EXTRAHUD_PROGRESS_EXTRA = 2,
};

inline void ExtraHudSnapProgress(IServer *pServer, int Slot, int Cur, int Max, int Extra = 0, int Flags = EXTRAHUDFLAG_ACTIVE | EXTRAHUDFLAG_TOP)
{
	CNetObj_ExtraHud *pHud = static_cast<CNetObj_ExtraHud *>(pServer->SnapNewItem(NETOBJTYPE_EXTRAHUD, Slot, sizeof(CNetObj_ExtraHud)));
	if(!pHud)
		return;
	pHud->m_Type = EXTRAHUD_PROGRESS;
	pHud->m_Flags = Flags;
	pHud->m_aData[EXTRAHUD_PROGRESS_CUR] = Cur;
	pHud->m_aData[EXTRAHUD_PROGRESS_MAX] = maximum(1, Max);
	pHud->m_aData[EXTRAHUD_PROGRESS_EXTRA] = Extra;
	for(int i = 3; i < 8; i++)
		pHud->m_aData[i] = 0;
}

inline void ExtraHudSnapNumeric(IServer *pServer, int Slot, int Value, int Max = 0, int Flags = EXTRAHUDFLAG_ACTIVE | EXTRAHUDFLAG_TOP_LEFT)
{
	CNetObj_ExtraHud *pHud = static_cast<CNetObj_ExtraHud *>(pServer->SnapNewItem(NETOBJTYPE_EXTRAHUD, Slot, sizeof(CNetObj_ExtraHud)));
	if(!pHud)
		return;
	pHud->m_Type = EXTRAHUD_NUMERIC;
	pHud->m_Flags = Flags;
	pHud->m_aData[0] = Value;
	pHud->m_aData[1] = Max;
	for(int i = 2; i < 8; i++)
		pHud->m_aData[i] = 0;
}

inline void ExtraHudSnapStack(IServer *pServer, int Slot, int Filled, int Total, int Flags = EXTRAHUDFLAG_ACTIVE | EXTRAHUDFLAG_TOP)
{
	CNetObj_ExtraHud *pHud = static_cast<CNetObj_ExtraHud *>(pServer->SnapNewItem(NETOBJTYPE_EXTRAHUD, Slot, sizeof(CNetObj_ExtraHud)));
	if(!pHud)
		return;
	pHud->m_Type = EXTRAHUD_STACK;
	pHud->m_Flags = Flags;
	pHud->m_aData[0] = Filled;
	pHud->m_aData[1] = maximum(1, Total);
	for(int i = 2; i < 8; i++)
		pHud->m_aData[i] = 0;
}

#endif
