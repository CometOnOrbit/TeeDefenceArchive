#include "tune_zone_manager.h"
#include <game/gamecore.h>
#include <engine/shared/datafile.h>

CTuneZoneManager &CTuneZoneManager::GetInstance()
{
	static CTuneZoneManager instance;
	return instance;
}

CTuneZoneManager::CTuneZoneManager()
{
	CTuningParams params;
	m_Zones[ETuneZone::DEFAULT] = params;

	params = CTuningParams();
	params.m_Gravity = 0.25f;
	params.m_GroundJumpImpulse = 8.0f;
	params.m_AirFriction = 0.75f;
	params.m_AirControlAccel = 1.0f;
	params.m_AirControlSpeed = 3.75f;
	params.m_AirJumpImpulse = 8.0f;
	params.m_HookFireSpeed = 30.0f;
	params.m_HookDragAccel = 1.5f;
	params.m_HookDragSpeed = 8.0f;
	params.m_PlayerHooking = 0;
	m_Zones[ETuneZone::SLOW] = params;

	params = CTuningParams();
	params.m_GroundControlSpeed = 5.0f;
	params.m_GroundControlAccel = 1.0f;
	m_Zones[ETuneZone::WALKING] = params;

	params = CTuningParams();
	params.m_Gravity = 0.15f;
	params.m_GroundFriction = 0.95f;
	params.m_GroundControlSpeed = 5.f;
	params.m_GroundControlAccel = 1.5f;
	params.m_AirFriction = 0.95f;
	params.m_AirControlSpeed = 5.f;
	params.m_AirControlAccel = 1.5f;
	m_Zones[ETuneZone::WATER] = params;
}

int CTuneZoneManager::GetZoneID(ETuneZone Zone) const
{
	return static_cast<int>(Zone);
}

const CTuningParams *CTuneZoneManager::GetParams(ETuneZone Zone) const
{
	auto it = m_Zones.find(Zone);
	if(it != m_Zones.end())
		return &it->second;
	return nullptr;
}

const CTuningParams *CTuneZoneManager::GetParams(int ZoneID) const
{
	if(ZoneID < 0 || ZoneID >= static_cast<int>(ETuneZone::NUM_TUNE_ZONES))
		return nullptr;
	return GetParams(static_cast<ETuneZone>(ZoneID));
}
