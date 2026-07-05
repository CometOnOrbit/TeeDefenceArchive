#ifndef GAME_SERVER_ENTITIES_VEHICLE_VEHICLE_VISUAL_H
#define GAME_SERVER_ENTITIES_VEHICLE_VEHICLE_VISUAL_H

#include <base/vmath.h>

class IServer;

namespace VehicleVisual
{
bool SnapPickup(IServer *pServer, int WorldId, int Id, vec2 Pos, int Type, int Subtype = 0);
void SnapAircraft(IServer *pServer, int WorldId, int BodyId, const int *pPartIds, int NumParts, vec2 Pos);
}

#endif
