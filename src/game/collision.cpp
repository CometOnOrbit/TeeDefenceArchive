/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/math.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/kernel.h>
#include <engine/map.h>
#include <math.h>

#include <game/collision.h>
#include <game/layers.h>
#include <game/mapitems.h>



constexpr float CAM_RADIUS_H = 380.f;
constexpr float CAM_RADIUS_W = 480.f;
constexpr float TILE_SIZE = 32.0f;

static vec2 CalculateTileCenter(int Index, int Width)
{
	if(Width <= 0)
		return vec2(0.0f, 0.0f);
	const float halfTileSize = TILE_SIZE / 2.0f;
	return vec2(
		static_cast<float>(Index % Width) * TILE_SIZE + halfTileSize,
		static_cast<float>(Index / Width) * TILE_SIZE + halfTileSize
	);
}

vec2 ClampVel(int MoveRestriction, vec2 Vel)
{
	if((Vel.x > 0 && (MoveRestriction & CANTMOVE_RIGHT)) || (Vel.x < 0 && (MoveRestriction & CANTMOVE_LEFT)))
		Vel.x = 0;
	if((Vel.y > 0 && (MoveRestriction & CANTMOVE_DOWN)) || (Vel.y < 0 && (MoveRestriction & CANTMOVE_UP)))
		Vel.y = 0;
	return Vel;
}

template <typename TileType>
inline TileType *TryLoadTileLayerData(IMap *pMap, const CMapItemLayerTilemap *pLayer, int DataIndex)
{
	if(!pLayer)
		return nullptr;
	unsigned int DataSize = pMap->GetDataSize(DataIndex);
	size_t ExpectedSize = (size_t)pLayer->m_Width * pLayer->m_Height * sizeof(TileType);
	if(DataSize < ExpectedSize)
		return nullptr;
	void *pRawData = pMap->GetData(DataIndex);
	return pRawData ? static_cast<TileType *>(pRawData) : nullptr;
}

CCollision::CCollision()
{
	m_pTiles = nullptr;
	m_pFront = nullptr;
	m_pExtraTiles = nullptr;
	m_pTele = nullptr;
	m_pSwitchExtra = nullptr;
	m_pSpeedupExtra = nullptr;
	m_pDoor = nullptr;
	m_pLayers = nullptr;
	m_Width = 0;
	m_Height = 0;
}

CCollision::~CCollision()
{
	delete[] m_pDoor;
}

void CCollision::Init(class CLayers *pLayers)
{
	m_pLayers = pLayers;
	const CMapItemLayerTilemap *pGameLayer = m_pLayers->GameLayer();
	m_Width = pGameLayer->m_Width;
	m_Height = pGameLayer->m_Height;

	// base game tiles
	IMap *pMap = m_pLayers->Map();
	m_pTiles = static_cast<CTile *>(pMap->GetData(pGameLayer->m_Data));
	InitTiles(m_pTiles);

	// front layer
	m_pFront = TryLoadTileLayerData<CTile>(pMap, m_pLayers->FrontLayer(),
		m_pLayers->FrontLayer() ? m_pLayers->FrontLayer()->m_Front : -1);
	if(m_pFront)
		InitTiles(m_pFront);

	// tele layer
	m_pTele = TryLoadTileLayerData<CTeleTile>(pMap, m_pLayers->TeleLayer(),
		m_pLayers->TeleLayer() ? m_pLayers->TeleLayer()->m_Tele : -1);
	if(m_pTele)
		InitTeleports();

	// switch layer
	m_pSwitchExtra = TryLoadTileLayerData<CSwitchTileExtra>(pMap, m_pLayers->SwitchLayer(),
		m_pLayers->SwitchLayer() ? m_pLayers->SwitchLayer()->m_Switch : -1);
	if(m_pSwitchExtra)
		InitSwitchExtra();

	// speedup layer
	m_pSpeedupExtra = TryLoadTileLayerData<CSpeedupTileExtra>(pMap, m_pLayers->SpeedupLayer(),
		m_pLayers->SpeedupLayer() ? m_pLayers->SpeedupLayer()->m_Speedup : -1);
	if(m_pSpeedupExtra)
		InitSpeedupExtra();

	// door layer - always allocate
	const int DoorLayerSize = m_Width * m_Height;
	m_pDoor = new CDoorTile[DoorLayerSize]();
	mem_zero(m_pDoor, DoorLayerSize * sizeof(CDoorTile));

	// TryLoadTileLayerData for extra tiles (TDA compat)
	m_pExtraTiles = TryLoadTileLayerData<CTile>(pMap, pGameLayer, pGameLayer->m_Data);
}

static void initGatheringNode(const std::string &nodeType, const std::vector<std::string> &vSettings, int Number,
	std::unordered_map<int, GatheringNode> &vNodesContainer)
{
	std::string ItemsData {};
	GatheringNode detail;
	const auto Identity = nodeType + " " + std::to_string(Number);

	char aName[64] = {0};
	int Level = 0, Health = 0;
	char aItems[256] = {0};
	if(sscanf(Identity.c_str(), "%63[^ ] %d %d %255[^\n]", aName, &Level, &Health, aItems) >= 3)
	{
		detail.Name = aName;
		detail.Level = Level;
		detail.Health = Health;
		if(aItems[0])
		{
			// simple semicolon-delimited tokenizer
			char *pIter = aItems;
			while(*pIter)
			{
				while(*pIter == ';' || *pIter == ' ') pIter++;
				if(!*pIter) break;
				const char *pTokenStart = pIter;
				while(*pIter && *pIter != ';') pIter++;
				const char Saved = *pIter;
				*pIter = '\0';
				
				int ItemID;
				float Chance;
				if(sscanf(pTokenStart, "[%d/%f]", &ItemID, &Chance) == 2)
					detail.m_vItems.addElement(ItemID, Chance);
				
				*pIter = Saved;
				if(Saved) pIter++;
			}
		}
		detail.m_vItems.normalizeChances();
		vNodesContainer[Number] = detail;
		dbg_msg("map-init", "Gathering '%s' (switch: %d) initialized: Items='%d'", detail.Name.c_str(), Number, (int)detail.m_vItems.size());
	}
}

void CCollision::InitSettings()
{
	const auto &settings = m_pLayers->GetSettings();

	for(int i = 1; i < 255; i++)
	{
		// zone details
		{
			ZoneDetail detail;
			const auto Identity = std::string("#zone ") + std::to_string(i);
			for(const auto &s : settings)
			{
				char aBuf[1024];
				str_copy(aBuf, s.c_str(), sizeof(aBuf));
				if(str_startswith(aBuf, Identity.c_str()))
				{
					char aName[256];
					int PVP = 0;
					if(sscanf(aBuf, ("#zone " + std::to_string(i) + " %255[^;];%d").c_str(), aName, &PVP) >= 1)
					{
						detail.Name = aName;
						detail.PVP = (PVP != 0);
					}
					break;
				}
			}
			if(!detail.Name.empty())
			{
				dbg_msg("map-init", "Zone '%s' (switch: %d) initialized: PVP=%s", detail.Name.c_str(), i, detail.PVP ? "true" : "false");
				m_vZoneDetail[i] = detail;
			}
		}

		// text zone details
		{
			TextZoneDetail detail;
			const auto Identity = std::string("#text ") + std::to_string(i);
			for(const auto &s : settings)
			{
				if(str_startswith(s.c_str(), Identity.c_str()))
				{
					const char *pText = s.c_str() + Identity.length();
					while(*pText == ' ') pText++;
					detail.Text = pText;
					break;
				}
			}
			if(!detail.Text.empty())
			{
				dbg_msg("map-init", "Text (switch: %d) initialized: String='%s'", i, detail.Text.c_str());
				m_vZoneTextDetail[i] = detail;
			}
		}

		// gathering nodes
		initGatheringNode("#node_ore", settings, i, m_vOreNodes);
		initGatheringNode("#node_plant", settings, i, m_vPlantNodes);
		initGatheringNode("#node_fish", settings, i, m_vFishNodes);

		// action zones
		{
			ActionZoneDetail detail;
			const auto Identity = std::string("#action_zone ") + std::to_string(i);
			for(const auto &s : settings)
			{
				if(str_startswith(s.c_str(), Identity.c_str()))
				{
					const char *pName = s.c_str() + Identity.length();
					while(*pName == ' ') pName++;
					detail.Name = pName;
					break;
				}
			}
			if(!detail.Name.empty())
			{
				dbg_msg("map-init", "Action zone '%s' (switch: %d) initialized", detail.Name.c_str(), i);
				m_vActionZoneDetail[i] = detail;
			}
		}
	}
}

void CCollision::InitTiles(CTile *pTiles)
{
	if(!pTiles)
		return;

	static const std::unordered_map<int, int> TILE_COLFLAG_MAP =
	{
		{ TILE_SOLID, COLFLAG_SOLID },
		{ TILE_DEATH, COLFLAG_DEATH },
		{ TILE_NOHOOK, COLFLAG_SOLID | COLFLAG_NOHOOK },
		{ TILE_WATER, COLFLAG_WATER },
	};

	const int NumTiles = m_Width * m_Height;
	for(int i = 0; i < NumTiles; ++i)
	{
		CTile &currentTile = pTiles[i];
		const int tileIndex = currentTile.m_Index;
		currentTile.m_Reserved = tileIndex;
		currentTile.m_Skip = 0;

		if(tileIndex > 128)
			continue;

		if(tileIndex == TILE_FIXED_CAM || tileIndex == TILE_SMOOTH_FIXED_CAM)
		{
			FixedCamZoneDetail camZoneData;
			camZoneData.Pos = CalculateTileCenter(i, m_Width);
			camZoneData.Rect = {
				camZoneData.Pos.x - CAM_RADIUS_W, camZoneData.Pos.y - CAM_RADIUS_H,
				camZoneData.Pos.x + CAM_RADIUS_W, camZoneData.Pos.y + CAM_RADIUS_H
			};
			camZoneData.Smooth = (tileIndex == TILE_SMOOTH_FIXED_CAM);
			m_vFixedCamZones.emplace_back(std::move(camZoneData));
			continue;
		}

		if(tileIndex == TILE_DESTROYER_PROJECTILE)
		{
			vec2 Pos = CalculateTileCenter(i, m_Width);
			SetDoorCollisionAt(Pos.x, Pos.y, TILE_STOPA, 0, 0);
		}

		if(auto it = TILE_COLFLAG_MAP.find(tileIndex); it != TILE_COLFLAG_MAP.end())
		{
			currentTile.m_Index = it->second;
		}
		else if(tileIndex == TILE_WATER)
		{
			currentTile.m_Index = COLFLAG_WATER;
		}
		else
		{
			currentTile.m_Index = 0;
		}
	}
}

void CCollision::InitTeleports()
{
	// collect teleport out positions
	const int NumTiles = m_Width * m_Height;
	for(int i = 0; i < NumTiles; ++i)
	{
		if(m_pTele[i].m_Type == TILE_TELE_OUT)
		{
			vec2 Pos = CalculateTileCenter(i, m_Width);
			m_vTeleOuts[m_pTele[i].m_Number].push_back(Pos);
		}
	}
}

void CCollision::InitSwitchExtra()
{
	const int NumTiles = m_Width * m_Height;
	for(int i = 0; i < NumTiles; ++i)
	{
		const int tileIndex = m_pSwitchExtra[i].m_Type;
		const int number = m_pSwitchExtra[i].m_Number;
		vec2 Pos = CalculateTileCenter(i, m_Width);

		// text zones store positions
		if(m_vZoneTextDetail.find(number) != m_vZoneTextDetail.end())
		{
			m_vZoneTextDetail[number].vPositions.push_back(Pos);
		}
	}
}

void CCollision::InitSpeedupExtra()
{
	// speedup areas processed at runtime via GetSpeedupTile
}

void CCollision::InitEntities(InitEntityCallback pfnCallback, void *pUser) const
{
	if(!pfnCallback) return;
	const int NumTiles = m_Width * m_Height;
	for(int i = 0; i < NumTiles; ++i)
	{
		if(m_pTiles[i].m_Index <= 128)
		{
			const int EntityIndex = m_pTiles[i].m_Reserved;
			if(EntityIndex >= ENTITY_SPAWN && EntityIndex < NUM_ENTITIES)
			{
				vec2 Pos = CalculateTileCenter(i, m_Width);
				pfnCallback(EntityIndex, Pos, i, pUser);
			}
		}
	}
}

void CCollision::InitSwitchEntities(InitEntityCallback pfnCallback, void *pUser) const
{
	if(!m_pSwitchExtra || !pfnCallback)
		return;
	const int NumTiles = m_Width * m_Height;
	for(int i = 0; i < NumTiles; ++i)
	{
		const int number = m_pSwitchExtra[i].m_Number;
		const int type = m_pSwitchExtra[i].m_Type;
		if(number > 0 || type > 0)
		{
			vec2 Pos = CalculateTileCenter(i, m_Width);
			pfnCallback(number, Pos, i, pUser);
		}
	}
}

vec2 CCollision::GetPos(int Index) const
{
	return CalculateTileCenter(Index, m_Width);
}

int CCollision::GetMainTileIndex(int Index) const
{
	if(Index < 0 || Index >= m_Width * m_Height) return 0;
	return m_pTiles[Index].m_Index;
}

int CCollision::GetFrontTileIndex(int Index) const
{
	if(!m_pFront || Index < 0 || Index >= m_Width * m_Height) return 0;
	return m_pFront[Index].m_Index;
}

int CCollision::GetExtraTileIndex(int Index) const
{
	if(!m_pExtraTiles || Index < 0 || Index >= m_Width * m_Height) return 0;
	return m_pExtraTiles[Index].m_Index;
}

int CCollision::GetMainTileCollisionFlags(int x, int y) const
{
	const int Nx = clamp(x / 32, 0, m_Width - 1);
	const int Ny = clamp(y / 32, 0, m_Height - 1);
	const int Index = Ny * m_Width + Nx;
	if(Index < 0 || Index >= m_Width * m_Height) return 0;
	return m_pTiles[Index].m_Index;
}

int CCollision::GetFrontTileCollisionFlags(int x, int y) const
{
	const int Nx = clamp(x / 32, 0, m_Width - 1);
	const int Ny = clamp(y / 32, 0, m_Height - 1);
	const int Index = Ny * m_Width + Nx;
	if(!m_pFront || Index < 0 || Index >= m_Width * m_Height) return 0;
	return m_pFront[Index].m_Index;
}

int CCollision::GetMainTileFlags(float x, float y) const
{
	const int Nx = clamp(round_to_int(x) / 32, 0, m_Width - 1);
	const int Ny = clamp(round_to_int(y) / 32, 0, m_Height - 1);
	const int Index = Ny * m_Width + Nx;
	if(Index < 0 || Index >= m_Width * m_Height) return 0;
	return m_pTiles[Index].m_Flags;
}

int CCollision::GetFrontTileFlags(float x, float y) const
{
	const int Nx = clamp(round_to_int(x) / 32, 0, m_Width - 1);
	const int Ny = clamp(round_to_int(y) / 32, 0, m_Height - 1);
	const int Index = Ny * m_Width + Nx;
	if(!m_pFront || Index < 0 || Index >= m_Width * m_Height) return 0;
	return m_pFront[Index].m_Flags;
}

int CCollision::GetExtraTileFlags(float x, float y) const
{
	const int Nx = clamp(round_to_int(x) / 32, 0, m_Width - 1);
	const int Ny = clamp(round_to_int(y) / 32, 0, m_Height - 1);
	const int Index = Ny * m_Width + Nx;
	if(!m_pExtraTiles || Index < 0 || Index >= m_Width * m_Height) return 0;
	return m_pExtraTiles[Index].m_Flags;
}

vec2 CCollision::GetRotateDirByFlags(int Flags)
{
	if(Flags & TILEFLAG_ROTATE)
		return Flags & TILEFLAG_VFLIP ? vec2(0, -1) : vec2(0, 1);
	return Flags & TILEFLAG_VFLIP ? vec2(-1, 0) : vec2(1, 0);
}

bool CCollision::TileExists(int Index) const
{
	if(Index < 0 || Index >= m_Width * m_Height) return false;
	return m_pTiles[Index].m_Index > 0 || (m_pFront && m_pFront[Index].m_Index > 0);
}

bool CCollision::TileExistsNext(int Index) const
{
	// check if adjacent tiles exist (for pathfinding)
	const int Indices[] = {
		Index - 1, Index + 1,
		Index - m_Width, Index + m_Width,
	};
	for(int i = 0; i < 4; i++)
	{
		const int Idx = Indices[i];
		if(Idx >= 0 && Idx < m_Width * m_Height && m_pTiles[Idx].m_Index > 0)
			return true;
	}
	return false;
}

int CCollision::GetMapIndex(vec2 Pos) const
{
	const int Nx = clamp(round_to_int(Pos.x) / 32, 0, m_Width - 1);
	const int Ny = clamp(round_to_int(Pos.y) / 32, 0, m_Height - 1);
	return Ny * m_Width + Nx;
}

int CCollision::GetPureMapIndex(float x, float y) const
{
	int nx = clamp(round_to_int(x), 0, m_Width - 1);
	int ny = clamp(round_to_int(y), 0, m_Height - 1);
	return ny * m_Width + nx;
}

std::vector<int> CCollision::GetMapIndices(vec2 PrevPos, vec2 Pos, unsigned MaxIndices) const
{
	std::vector<int> Indices;
	vec2 Dir = Pos - PrevPos;
	const float Dist = length(Dir);
	if(Dist < 1.f)
	{
		Indices.push_back(GetMapIndex(Pos));
		return Indices;
	}
	Dir = normalize(Dir);
	const int Steps = maximum((int)Dist, 1);
	for(int i = 0; i <= Steps; i++)
	{
		vec2 P = PrevPos + Dir * ((float)i / (float)Steps) * Dist;
		int Idx = GetMapIndex(P);
		if(Indices.empty() || Indices.back() != Idx)
			Indices.push_back(Idx);
		if(MaxIndices > 0 && Indices.size() >= MaxIndices)
			break;
	}
	return Indices;
}

CSwitchTileExtra *CCollision::GetSwitchTile(vec2 Pos) const
{
	if(!m_pSwitchExtra) return nullptr;
	const int Index = GetMapIndex(Pos);
	if(Index < 0 || Index >= m_Width * m_Height) return nullptr;
	return &m_pSwitchExtra[Index];
}

int CCollision::GetSwitchNumber(vec2 Pos) const
{
	const auto *pTile = GetSwitchTile(Pos);
	return pTile ? pTile->m_Number : 0;
}

int CCollision::GetSwitchNumberAtTileIndex(vec2 Pos, int TileIndex) const
{
	const auto *pTile = GetSwitchTile(Pos);
	return (pTile && (int)pTile->m_Type == TileIndex) ? pTile->m_Number : 0;
}

void CCollision::SetDoorCollisionAt(float x, float y, int Type, int Flags, int Number)
{
	const int Nx = clamp(round_to_int(x) / 32, 0, m_Width - 1);
	const int Ny = clamp(round_to_int(y) / 32, 0, m_Height - 1);
	const int Index = Ny * m_Width + Nx;
	if(Index < 0 || Index >= m_Width * m_Height) return;

	m_pDoor[Index].m_Index = Type;
	m_pDoor[Index].m_Flags = Flags;
	m_pDoor[Index].m_Number = Number;
}

void CCollision::SetDoorFromToCollisionAt(vec2 From, vec2 To, int Type, int Flags, int Number)
{
	const float Dist = distance(From, To);
	if(Dist < 1.f)
	{
		SetDoorCollisionAt(From.x, From.y, Type, Flags, Number);
		return;
	}
	vec2 Dir = normalize(To - From);
	const int Steps = (int)Dist;
	for(int i = 0; i <= Steps; i++)
	{
		vec2 Pos = From + Dir * (float)i;
		SetDoorCollisionAt(Pos.x, Pos.y, Type, Flags, Number);
	}
}

void CCollision::GetDoorTile(int Index, CDoorTile *pDoorTile) const
{
	if(!m_pDoor || Index < 0 || Index >= m_Width * m_Height)
	{
		if(pDoorTile)
		{
			pDoorTile->m_Index = 0;
			pDoorTile->m_Flags = 0;
			pDoorTile->m_Number = 0;
		}
		return;
	}
	*pDoorTile = m_pDoor[Index];
}

bool CCollision::IntersectLineDoor(vec2 From, vec2 To) const
{
	vec2 Dir = To - From;
	const float Dist = length(Dir);
	if(Dist < 1.f) return false;
	Dir = normalize(Dir);
	for(int i = 0; i <= (int)Dist; i++)
	{
		vec2 Pos = From + Dir * (float)i;
		const int Index = GetMapIndex(Pos);
		if(Index < 0 || Index >= m_Width * m_Height) continue;
		if(m_pDoor[Index].m_Index != 0)
			return true;
	}
	return false;
}

bool CCollision::GetTeleportOut(vec2 currentPos, vec2 *pOut) const
{
	const int Index = GetMapIndex(currentPos);
	if(!m_pTele || Index < 0 || Index >= m_Width * m_Height)
		return false;

	const CTeleTile &tile = m_pTele[Index];
	if(tile.m_Type != TILE_TELE_FROM)
		return false;

	auto it = m_vTeleOuts.find(tile.m_Number);
	if(it == m_vTeleOuts.end() || it->second.empty())
		return false;

	vec2 Best;
	float BestDist = 1e9;
	for(const auto &Out : it->second)
	{
		const float D = distance(currentPos, Out);
		if(D < BestDist)
		{
			BestDist = D;
			Best = Out;
		}
	}
	if(pOut) *pOut = Best;
	return true;
}

bool CCollision::GetFixedCamPos(vec2 currentPos, vec2 *pOutPos, bool *pOutSmooth) const
{
	for(const auto &Zone : m_vFixedCamZones)
	{
		if(currentPos.x >= Zone.Rect.x && currentPos.x <= Zone.Rect.z &&
			currentPos.y >= Zone.Rect.y && currentPos.y <= Zone.Rect.w)
		{
			if(pOutPos) *pOutPos = Zone.Pos;
			if(pOutSmooth) *pOutSmooth = Zone.Smooth;
			return true;
		}
	}
	return false;
}

bool CCollision::GetZonedetail(vec2 Pos, ZoneDetail *pOut) const
{
	if(!m_pSwitchExtra)
		return false;
	const int Index = GetMapIndex(Pos);
	if(Index < 0 || Index >= m_Width * m_Height)
		return false;
	const int Number = m_pSwitchExtra[Index].m_Number;
	auto it = m_vZoneDetail.find(Number);
	if(it != m_vZoneDetail.end())
	{
		if(pOut) *pOut = it->second;
		return true;
	}
	return false;
}

bool CCollision::IsActiveActionZone(const char *pActionZoneName, vec2 Pos) const
{
	if(!m_pSwitchExtra)
		return false;
	const int Index = GetMapIndex(Pos);
	if(Index < 0 || Index >= m_Width * m_Height)
		return false;
	const int Number = m_pSwitchExtra[Index].m_Number;
	auto it = m_vActionZoneDetail.find(Number);
	return it != m_vActionZoneDetail.end() && it->second.Name == pActionZoneName;
}

GatheringNode *CCollision::GetOreNode(int SwitchNumber)
{
	auto it = m_vOreNodes.find(SwitchNumber);
	return it != m_vOreNodes.end() ? &it->second : nullptr;
}

GatheringNode *CCollision::GetPlantNode(int SwitchNumber)
{
	auto it = m_vPlantNodes.find(SwitchNumber);
	return it != m_vPlantNodes.end() ? &it->second : nullptr;
}

GatheringNode *CCollision::GetFishNode(int SwitchNumber)
{
	auto it = m_vFishNodes.find(SwitchNumber);
	return it != m_vFishNodes.end() ? &it->second : nullptr;
}

int CCollision::GetTile(int x, int y) const
{
	int Nx = clamp(x / 32, 0, m_Width - 1);
	int Ny = clamp(y / 32, 0, m_Height - 1);
	return m_pTiles[Ny * m_Width + Nx].m_Index > 128 ? 0 : m_pTiles[Ny * m_Width + Nx].m_Index;
}

bool CCollision::IsTile(int x, int y, int Flag) const
{
	return GetTile(x, y) & Flag;
}

int CCollision::FastIntersectLine(vec2 Pos0, vec2 Pos1, vec2 *pOutCollision, vec2 *pOutBeforeCollision) const
{
	const int Tile0X = round_to_int(Pos0.x) / 32;
	const int Tile0Y = round_to_int(Pos0.y) / 32;
	const int Tile1X = round_to_int(Pos1.x) / 32;
	const int Tile1Y = round_to_int(Pos1.y) / 32;

	const float Ratio = (Tile0X == Tile1X) ? 1.f : (Pos1.y - Pos0.y) / (Pos1.x - Pos0.x);
	const float DetPos = Pos0.x * Pos1.y - Pos0.y * Pos1.x;
	const int DeltaTileX = (Tile0X <= Tile1X) ? 1 : -1;
	const int DeltaTileY = (Tile0Y <= Tile1Y) ? 1 : -1;
	const float DeltaError = DeltaTileY * DeltaTileX * Ratio;

	int CurTileX = Tile0X;
	int CurTileY = Tile0Y;
	vec2 Pos = Pos0;
	bool Vertical = false;

	float Error = 0;
	if(Tile0Y != Tile1Y && Tile0X != Tile1X)
	{
		Error = (CurTileX * Ratio - CurTileY - DetPos / (32 * (Pos1.x - Pos0.x))) * DeltaTileY;
		if(Tile0X < Tile1X)
			Error += Ratio * DeltaTileY;
		if(Tile0Y < Tile1Y)
			Error -= DeltaTileY;
	}

	auto TileBlocks = [this](int Tx, int Ty) {
		return CheckPoint((float)(Tx * 32 + 16), (float)(Ty * 32 + 16));
	};

	while(CurTileX != Tile1X || CurTileY != Tile1Y)
	{
		if(TileBlocks(CurTileX, CurTileY))
			break;
		if(CurTileY != Tile1Y && (CurTileX == Tile1X || Error > 0))
		{
			CurTileY += DeltaTileY;
			Error -= 1;
			Vertical = false;
		}
		else
		{
			CurTileX += DeltaTileX;
			Error += DeltaError;
			Vertical = true;
		}
	}
	if(TileBlocks(CurTileX, CurTileY))
	{
		if(CurTileX != Tile0X || CurTileY != Tile0Y)
		{
			if(Vertical)
			{
				Pos.x = 32 * (CurTileX + ((Tile0X < Tile1X) ? 0 : 1));
				Pos.y = (Pos.x * (Pos1.y - Pos0.y) - DetPos) / (Pos1.x - Pos0.x);
			}
			else
			{
				Pos.y = 32 * (CurTileY + ((Tile0Y < Tile1Y) ? 0 : 1));
				Pos.x = (Pos.y * (Pos1.x - Pos0.x) + DetPos) / (Pos1.y - Pos0.y);
			}
		}
		if(pOutCollision)
			*pOutCollision = Pos;
		if(pOutBeforeCollision)
		{
			vec2 Dir = normalize(Pos1 - Pos0);
			if(Vertical)
				Dir *= 0.5f / absolute(Dir.x) + 1.f;
			else
				Dir *= 0.5f / absolute(Dir.y) + 1.f;
			*pOutBeforeCollision = Pos - Dir;
		}
		return GetTile(CurTileX * 32 + 16, CurTileY * 32 + 16);
	}
	if(pOutCollision)
		*pOutCollision = Pos1;
	if(pOutBeforeCollision)
		*pOutBeforeCollision = Pos1;
	return 0;
}

int CCollision::IntersectLine(vec2 Pos0, vec2 Pos1, vec2 *pOutCollision, vec2 *pOutBeforeCollision, int ColFlag) const
{
	const int End = distance(Pos0, Pos1) + 1;
	const float InverseEnd = 1.0f / End;
	vec2 Last = Pos0;

	for(int i = 0; i <= End; i++)
	{
		vec2 Pos = mix(Pos0, Pos1, i * InverseEnd);
		if(CheckPoint(Pos.x, Pos.y, ColFlag))
		{
			if(pOutCollision)
				*pOutCollision = Pos;
			if(pOutBeforeCollision)
				*pOutBeforeCollision = Last;
			return GetCollisionAt(Pos.x, Pos.y);
		}
		Last = Pos;
	}
	if(pOutCollision)
		*pOutCollision = Pos1;
	if(pOutBeforeCollision)
		*pOutBeforeCollision = Pos1;
	return 0;
}

bool CCollision::IntersectLineColFlag(vec2 Pos0, vec2 Pos1, vec2 *pOutCollision, vec2 *pOutBeforeCollision, int ColFlag) const
{
	return IntersectLine(Pos0, Pos1, pOutCollision, pOutBeforeCollision, ColFlag) != 0;
}

void CCollision::MovePoint(vec2 *pInoutPos, vec2 *pInoutVel, float Elasticity, int *pBounces) const
{
	if(pBounces)
		*pBounces = 0;

	vec2 Pos = *pInoutPos;
	vec2 Vel = *pInoutVel;
	if(CheckPoint(Pos + Vel))
	{
		int Affected = 0;
		if(CheckPoint(Pos.x + Vel.x, Pos.y))
		{
			pInoutVel->x *= -Elasticity;
			if(pBounces)
				(*pBounces)++;
			Affected++;
		}

		if(CheckPoint(Pos.x, Pos.y + Vel.y))
		{
			pInoutVel->y *= -Elasticity;
			if(pBounces)
				(*pBounces)++;
			Affected++;
		}

		if(Affected == 0)
		{
			pInoutVel->x *= -Elasticity;
			pInoutVel->y *= -Elasticity;
		}
	}
	else
	{
		*pInoutPos = Pos + Vel;
	}
}

int CCollision::TestBoxAt(vec2 Pos, vec2 Size) const
{
	Size *= 0.5f;
	int Flag = 0;
	Flag |= GetCollisionAt(Pos.x - Size.x, Pos.y - Size.y);
	Flag |= GetCollisionAt(Pos.x + Size.x, Pos.y - Size.y);
	Flag |= GetCollisionAt(Pos.x - Size.x, Pos.y + Size.y);
	Flag |= GetCollisionAt(Pos.x + Size.x, Pos.y + Size.y);
	return Flag;
}

bool CCollision::TestBox(vec2 Pos, vec2 Size, int Flag) const
{
	return TestBoxAt(Pos, Size) & Flag;
}

int CCollision::TestBoxMoveAt(vec2 LastPos, vec2 NewPos, vec2 Size) const
{
	vec2 Pos = LastPos;
	const vec2 Direction = normalize(NewPos - LastPos);
	const float Distance = distance(NewPos, LastPos);
	const int Max = (int)Distance;

	int Flag = 0;
	if(Distance > 0.00001f)
	{
		const float Fraction = 1.0f / (Max + 1);
		for(int i = 0; i <= Max; i++)
		{
			Flag |= TestBoxAt(Pos, Size);
			Pos = Pos + Direction * Fraction;
		}
	}
	else
		return TestBoxAt(Pos, Size);

	return Flag;
}

void CCollision::SetFlagFor(float x, float y, int Flag)
{
	int Nx = clamp(round_to_int(x) / 32, 0, m_Width - 1);
	int Ny = clamp(round_to_int(y) / 32, 0, m_Height - 1);
	m_pTiles[Ny * m_Width + Nx].m_Index = Flag;
}

void CCollision::MoveBox(vec2 *pInoutPos, vec2 *pInoutVel, vec2 Size, float Elasticity, bool *pDeath) const
{
	vec2 Pos = *pInoutPos;
	vec2 Vel = *pInoutVel;

	const float Distance = length(Vel);
	const int Max = (int)Distance;

	if(pDeath)
		*pDeath = false;

	if(Distance > 0.00001f)
	{
		const float Fraction = 1.0f / (Max + 1);
		for(int i = 0; i <= Max; i++)
		{
			vec2 NewPos = Pos + Vel * Fraction;

			if(pDeath && TestBox(vec2(NewPos.x, NewPos.y), Size * (2.0f / 3.0f), COLFLAG_DEATH))
			{
				*pDeath = true;
			}

			if(TestBox(vec2(NewPos.x, NewPos.y), Size))
			{
				int Hits = 0;

				if(TestBox(vec2(Pos.x, NewPos.y), Size))
				{
					NewPos.y = Pos.y;
					Vel.y *= -Elasticity;
					Hits++;
				}

				if(TestBox(vec2(NewPos.x, Pos.y), Size))
				{
					NewPos.x = Pos.x;
					Vel.x *= -Elasticity;
					Hits++;
				}

				if(Hits == 0)
				{
					NewPos.y = Pos.y;
					Vel.y *= -Elasticity;
					NewPos.x = Pos.x;
					Vel.x *= -Elasticity;
				}
			}

			Pos = NewPos;
		}
	}

	*pInoutPos = Pos;
	*pInoutVel = Vel;
}

void CCollision::MovePhysicalAngleBox(vec2 *pPos, vec2 *pVel, vec2 Size, float *pAngle, float *pAngleForce, float Elasticity, float Gravity) const
{
	// simplified physics: move with gravity, then bounce
	pVel->y += Gravity;
	MoveBox(pPos, pVel, Size, Elasticity, nullptr);
	*pAngle += *pAngleForce;
}

void CCollision::MovePhysicalBox(vec2 *pPos, vec2 *pVel, vec2 Size, float Elasticity, float Gravity) const
{
	pVel->y += Gravity;
	MoveBox(pPos, pVel, Size, Elasticity, nullptr);
}

int CCollision::GetMoveRestrictions(vec2 Pos, float Distance) const
{
	int Restrictions = 0;
	const int StartX = round_to_int(Pos.x - Distance);
	const int EndX = round_to_int(Pos.x + Distance);
	const int StartY = round_to_int(Pos.y - Distance);
	const int EndY = round_to_int(Pos.y + Distance);

	for(int y = StartY; y <= EndY; y++)
	{
		for(int x = StartX; x <= EndX; x++)
		{
			if(!CheckPoint((float)x, (float)y))
				continue;
			const int Index = GetPureMapIndex((float)x, (float)y);
			const int TileIndex = m_pTiles[Index].m_Reserved;
			if(TileIndex == TILE_STOP || TileIndex == TILE_STOPS || TileIndex == TILE_STOPA)
			{
				// stop tiles restrict movement in specific directions
				if(TileIndex == TILE_STOP) { Restrictions |= CANTMOVE_LEFT | CANTMOVE_RIGHT; }
				if(TileIndex == TILE_STOPS) { Restrictions |= CANTMOVE_UP | CANTMOVE_DOWN; }
				if(TileIndex == TILE_STOPA) { Restrictions |= CANTMOVE_LEFT | CANTMOVE_RIGHT | CANTMOVE_UP | CANTMOVE_DOWN; }
			}
		}
	}
	return Restrictions;
}

// Legacy TDA helper: get collision value at tile
int CCollision::GetCollisionAt(float x, float y) const
{
	return GetTile(round_to_int(x), round_to_int(y));
}
