/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include "layers.h"
#include "mapitems.h"

#include <engine/map.h>

void CLayers::Init(class IKernel* pKernel, IMap* pMap)
{
	m_pMap = pMap ? pMap : pKernel->RequestInterface<IMap>();
	m_pMap->GetType(MAPITEMTYPE_GROUP, &m_GroupsStart, &m_GroupsNum);
	m_pMap->GetType(MAPITEMTYPE_LAYER, &m_LayersStart, &m_LayersNum);

	for(int g = 0; g < NumGroups(); g++)
	{
		CMapItemGroup* pGroup = GetGroup(g);
		for(int l = 0; l < pGroup->m_NumLayers; l++)
		{
			CMapItemLayer* pLayer = GetLayer(pGroup->m_StartLayer + l);
			if(pLayer->m_Type == LAYERTYPE_TILES)
			{
				bool IsEntities = false;
				CMapItemLayerTilemap* pTilemap = reinterpret_cast<CMapItemLayerTilemap*>(pLayer);

				if(pTilemap->m_Flags & TILESLAYERFLAG_GAME)
				{
					m_pGameLayer = pTilemap;
					m_pGameGroup = pGroup;

					m_pGameGroup->m_OffsetX = 0;
					m_pGameGroup->m_OffsetY = 0;
					m_pGameGroup->m_ParallaxX = 100;
					m_pGameGroup->m_ParallaxY = 100;

					if(m_pGameGroup->m_Version >= 2)
					{
						m_pGameGroup->m_UseClipping = 0;
						m_pGameGroup->m_ClipX = 0;
						m_pGameGroup->m_ClipY = 0;
						m_pGameGroup->m_ClipW = 0;
						m_pGameGroup->m_ClipH = 0;
					}

					IsEntities = true;
				}

				if(pTilemap->m_Flags & TILESLAYERFLAG_FRONT)
				{
					if(pTilemap->m_Version <= 2)
						pTilemap->m_Front = *((int*)(pTilemap) + 17);
					m_pFrontLayer = pTilemap;
					IsEntities = true;
				}

				if(pTilemap->m_Flags & TILESLAYERFLAG_TELE)
				{
					if(pTilemap->m_Version <= 2)
						pTilemap->m_Tele = *((int*)(pTilemap) + 15);
					m_pTeleLayer = pTilemap;
					IsEntities = true;
				}

				if(pTilemap->m_Flags & TILESLAYERFLAG_SWITCH)
				{
					if(pTilemap->m_Version <= 2)
						pTilemap->m_Switch = *((int*)(pTilemap) + 18);
					m_pSwitchLayer = pTilemap;
					IsEntities = true;
				}

				if(pTilemap->m_Flags & TILESLAYERFLAG_SPEEDUP)
				{
					if(pTilemap->m_Version <= 2)
						pTilemap->m_Speedup = *((int*)(pTilemap) + 16);
					m_pSpeedupLayer = pTilemap;
					IsEntities = true;
				}

				if(IsEntities)
				{
					pTilemap->m_Color = CColor{255, 255, 255, 255};
				}
			}
		}
	}

	InitTilemapSkip();
	InitSettings();
}

void CLayers::InitBackground(class IMap* pMap)
{
	m_pMap = pMap;
	m_pMap->GetType(MAPITEMTYPE_GROUP, &m_GroupsStart, &m_GroupsNum);
	m_pMap->GetType(MAPITEMTYPE_LAYER, &m_LayersStart, &m_LayersNum);

	for(int g = 0; g < NumGroups(); g++)
	{
		CMapItemGroup* pGroup = GetGroup(g);
		for(int l = 0; l < pGroup->m_NumLayers; l++)
		{
			CMapItemLayer* pLayer = GetLayer(pGroup->m_StartLayer + l);
			if(pLayer->m_Type == LAYERTYPE_TILES)
			{
				CMapItemLayerTilemap* pTilemap = reinterpret_cast<CMapItemLayerTilemap*>(pLayer);
				if(pTilemap->m_Flags & TILESLAYERFLAG_GAME)
				{
					m_pGameLayer = pTilemap;
					m_pGameGroup = pGroup;

					m_pGameGroup->m_OffsetX = 0;
					m_pGameGroup->m_OffsetY = 0;
					m_pGameGroup->m_ParallaxX = 100;
					m_pGameGroup->m_ParallaxY = 100;

					if(m_pGameGroup->m_Version >= 2)
					{
						m_pGameGroup->m_UseClipping = 0;
						m_pGameGroup->m_ClipX = 0;
						m_pGameGroup->m_ClipY = 0;
						m_pGameGroup->m_ClipW = 0;
						m_pGameGroup->m_ClipH = 0;
					}
				}
			}
		}
	}

	InitTilemapSkip();
}

void CLayers::InitTilemapSkip()
{
	for(int g = 0; g < NumGroups(); g++)
	{
		const CMapItemGroup* pGroup = GetGroup(g);
		for(int l = 0; l < pGroup->m_NumLayers; l++)
		{
			const CMapItemLayer* pLayer = GetLayer(pGroup->m_StartLayer + l);
			if(pLayer->m_Type == LAYERTYPE_TILES)
			{
				const CMapItemLayerTilemap* pTilemap = (CMapItemLayerTilemap*)pLayer;
				CTile* pTiles = (CTile*)m_pMap->GetData(pTilemap->m_Data);
				for(int y = 0; y < pTilemap->m_Height; y++)
				{
					for(int x = 1; x < pTilemap->m_Width;)
					{
						int SkippedX;
						for(SkippedX = 1; x + SkippedX < pTilemap->m_Width && SkippedX < 255; SkippedX++)
						{
							if(pTiles[y * pTilemap->m_Width + x + SkippedX].m_Index)
								break;
						}
						pTiles[y * pTilemap->m_Width + x].m_Skip = SkippedX - 1;
						x += SkippedX;
					}
				}
			}
		}
	}
}

void CLayers::InitSettings()
{
	int Start = 0, Num = 0;
	m_pMap->GetType(MAPITEMTYPE_INFO, &Start, &Num);
	for(int i = Start; i < Start + Num; i++)
	{
		int ItemID = 0;
		auto* pItem = static_cast<CMapItemInfoSettings*>(m_pMap->GetItem(i, nullptr, &ItemID));
		if(!pItem || ItemID != 0)
			continue;

		// Check if this is actually CMapItemInfoSettings by verifying m_Settings
		if(pItem->m_Settings <= 0)
			continue;

		const int Size = m_pMap->GetDataSize(pItem->m_Settings);
		if(Size <= 0)
			continue;

		const auto pSettings = static_cast<char*>(m_pMap->GetData(pItem->m_Settings));
		if(!pSettings)
			continue;

		const char* pNext = pSettings;
		while(pNext < pSettings + Size)
		{
			const int StrSize = str_length(pNext) + 1;
			if(StrSize <= 1) break;
			m_Settings.emplace_back(pNext);
			pNext += StrSize;
		}
		break;
	}
}

CMapItemGroup* CLayers::GetGroup(int Index) const
{
	return static_cast<CMapItemGroup*>(m_pMap->GetItem(m_GroupsStart + Index, 0, 0));
}

CMapItemLayer* CLayers::GetLayer(int Index) const
{
	return static_cast<CMapItemLayer*>(m_pMap->GetItem(m_LayersStart + Index, 0, 0));
}
