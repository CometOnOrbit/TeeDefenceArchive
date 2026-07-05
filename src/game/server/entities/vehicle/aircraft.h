#ifndef GAME_SERVER_ENTITIES_VEHICLE_AIRCRAFT_H
#define GAME_SERVER_ENTITIES_VEHICLE_AIRCRAFT_H

#include "vehicle.h"

enum
{
	AIRCRAFT_PART_IDS = 2,
	AIRCRAFT_MAX_SPEED = 18,
	AIRCRAFT_ACCEL = 2,
};

class CAircraft : public CVehicle
{
	int m_PartIds[AIRCRAFT_PART_IDS];
	int m_PrevDriverJump;

protected:
	void TickDriver(CCharacter *pDriver);
	float DriverOffsetY() const { return VehicleScale(-8.f); }
	vec2 CollisionSize() const { return vec2(VehicleScale(48.f), VehicleScale(24.f)); }
	void TickIdle();

public:
	CAircraft(CGameWorld *pGameWorld, vec2 Pos, int Team, int OwnerClientID = -1);
	~CAircraft();

	void Snap(int SnappingClient);
};

#endif
