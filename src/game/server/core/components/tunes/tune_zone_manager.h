#ifndef GAME_SERVER_CORE_COMPONENTS_TUNES_TUNE_ZONE_MANAGER_H
#define GAME_SERVER_CORE_COMPONENTS_TUNES_TUNE_ZONE_MANAGER_H

#include <map>

class CTuningParams;

enum class ETuneZone
{
	DEFAULT = 0,
	SLOW,
	WALKING,
	WATER,
	NUM_TUNE_ZONES
};

class CTuneZoneManager
{
public:
	static CTuneZoneManager &GetInstance();

	int GetZoneID(ETuneZone Zone) const;
	const CTuningParams *GetParams(ETuneZone Zone) const;
	const CTuningParams *GetParams(int ZoneID) const;

	std::map<ETuneZone, CTuningParams> m_Zones;

private:
	CTuneZoneManager();
	~CTuneZoneManager() = default;

	CTuneZoneManager(const CTuneZoneManager &) = delete;
	CTuneZoneManager &operator=(const CTuneZoneManager &) = delete;
};

#endif
