/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_LAYERS_H
#define GAME_LAYERS_H

#include <engine/map.h>
#include <game/mapitems.h>

#include <vector>
#include <string>

class CLayers
{
	int m_GroupsNum = 0;
	int m_GroupsStart = 0;
	int m_LayersNum = 0;
	int m_LayersStart = 0;
	CMapItemGroup *m_pGameGroup = nullptr;
	CMapItemLayerTilemap *m_pGameLayer = nullptr;
	CMapItemLayerTilemap *m_pFrontLayer = nullptr;
	CMapItemLayerTilemap *m_pTeleLayer = nullptr;
	CMapItemLayerTilemap *m_pSwitchLayer = nullptr;
	CMapItemLayerTilemap *m_pSpeedupLayer = nullptr;
	class IMap *m_pMap = nullptr;
	std::vector<std::string> m_Settings;

	void InitTilemapSkip();
	void InitSettings();

public:
	CLayers() = default;
	void Init(class IKernel *pKernel, class IMap *pMap = nullptr);
	void InitBackground(class IMap *pMap);
	int NumGroups() const { return m_GroupsNum; }
	int NumLayers() const { return m_LayersNum; }
	class IMap *Map() const { return m_pMap; }
	CMapItemGroup *GameGroup() const { return m_pGameGroup; }
	CMapItemLayerTilemap *GameLayer() const { return m_pGameLayer; }
	CMapItemLayerTilemap *FrontLayer() const { return m_pFrontLayer; }
	CMapItemLayerTilemap *TeleLayer() const { return m_pTeleLayer; }
	CMapItemLayerTilemap *SwitchLayer() const { return m_pSwitchLayer; }
	CMapItemLayerTilemap *SpeedupLayer() const { return m_pSpeedupLayer; }
	std::vector<std::string> &GetSettings() { return m_Settings; }
	CMapItemGroup *GetGroup(int Index) const;
	CMapItemLayer *GetLayer(int Index) const;
};

#endif
