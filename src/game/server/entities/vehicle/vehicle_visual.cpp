#include "vehicle_visual.h"

#include <base/math.h>
#include <engine/server.h>
#include <generated/protocol.h>

#include "vehicle_util.h"

namespace VehicleVisual
{
static vec2 At(vec2 Pos, float X, float Y)
{
	return vec2(Pos.x + VehicleScale(X), Pos.y + VehicleScale(Y));
}

bool SnapPickup(IServer *pServer, int WorldId, int Id, vec2 Pos, int Type, int Subtype)
{
	CNetObj_Pickup *pP = static_cast<CNetObj_Pickup *>(pServer->SnapNewItem(NETOBJTYPE_PICKUP, Id, sizeof(CNetObj_Pickup)));
	if(!pP)
		return false;

	pP->m_X = (int)Pos.x;
	pP->m_Y = (int)Pos.y;
	pP->m_Type = Type;
	(void)Subtype;
	(void)WorldId;
	return true;
}

void SnapAircraft(IServer *pServer, int WorldId, int BodyId, const int *pPartIds, int NumParts, vec2 Pos)
{
	if(NumParts < 2 || !pPartIds)
		return;

	const float Step = VehicleScale(8.f);
	SnapPickup(pServer, WorldId, BodyId, At(Pos, -Step, 0.f), PICKUP_HEALTH);
	SnapPickup(pServer, WorldId, pPartIds[0], At(Pos, 0.f, 0.f), PICKUP_ARMOR);
	SnapPickup(pServer, WorldId, pPartIds[1], At(Pos, Step, 0.f), PICKUP_HEALTH);
}
}
