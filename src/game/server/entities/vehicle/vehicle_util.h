#ifndef GAME_SERVER_ENTITIES_VEHICLE_VEHICLE_UTIL_H
#define GAME_SERVER_ENTITIES_VEHICLE_VEHICLE_UTIL_H

#include <base/vmath.h>

class CCharacter;
class CEntity;
class CGameContext;
class CGameWorld;
class CVehicle;

enum
{
	VEHICLE_SEAT_NONE = 0,
	VEHICLE_SEAT_DRIVER = 1,
};

static const float VEHICLE_SIZE_SCALE = 1.35f;

inline float VehicleScale(float Value)
{
	return Value * VEHICLE_SIZE_SCALE;
}

bool VehicleInputPressed(int Prev, int Cur);
bool VehicleSpotBlocked(CGameWorld *pWorld, vec2 Pos, float Radius = VehicleScale(56.f));
void VehicleClearOccupant(CCharacter *pChr);
void VehicleResetCharacterHook(CCharacter *pChr);
void VehicleDismount(int &Owner, vec2 &Vel, CCharacter *pChr);
void VehicleSyncCharacter(CCharacter *pChr, vec2 Pos, vec2 Vel, float RiderOffsetY = -8.f);
void VehicleApplyGravity(CGameContext *pGS, vec2 &Vel);
void VehicleApplyFriction(vec2 &Vel, float Friction);
bool VehicleTryAutoBoard(CGameWorld *pWorld, CEntity *pVehicle, int &Owner, int Team, float BoardRadius = VehicleScale(32.f));
void VehicleOnCharacterDie(CGameContext *pGS, int ClientId);
CVehicle *VehicleFindByOccupant(CGameWorld *pWorld, int ClientId);
void VehicleSpawnShrapnel(CGameContext *pGS, CGameWorld *pWorld, vec2 Pos, int Owner, int Team);

#endif
