/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_COLLISION_H
#define GAME_COLLISION_H

#include <string_view>
#include <vector>
#include <map>
#include <unordered_map>
#include <string>

#include <base/vmath.h>
#include <game/mapitems.h>

class CTile;
class CDoorTile;
class CSwitchTileExtra;
class CSpeedupTileExtra;
class CLayers;

enum
{
	CANTMOVE_LEFT = 1 << 0,
	CANTMOVE_RIGHT = 1 << 1,
	CANTMOVE_UP = 1 << 2,
	CANTMOVE_DOWN = 1 << 3,
};

struct GatheringNode
{
	std::string Name {};
	int Level {};
	int Health {};
	struct Items
	{
		std::vector<std::pair<int, float>> m_vItems;
		size_t size() const { return m_vItems.size(); }
		bool empty() const { return m_vItems.empty(); }
		void addElement(int ItemID, float Chance) { m_vItems.emplace_back(ItemID, Chance); }
		void normalizeChances()
		{
			if(m_vItems.empty()) return;
			float Total = 0.f;
			for(const auto& [_, Chance] : m_vItems) Total += Chance;
			if(Total > 0.f)
			{
				for(auto& [_, Chance] : m_vItems) Chance = (Chance * 100.f) / Total;
			}
		}
	} m_vItems;
};

class CCollision
{
public:
	enum
	{
		COLFLAG_SOLID = 1 << 0,
		COLFLAG_DEATH = 1 << 1,
		COLFLAG_NOHOOK = 1 << 2,
		COLFLAG_SAFE = 1 << 3,
		COLFLAG_UNHOOKABLE = (1 << 2), // alias for TDA compat
		COLFLAG_WATER = 1 << 5,
	};

	struct ZoneDetail
	{
		bool PVP{};
		std::string Name{};
	};
	struct TextZoneDetail
	{
		std::vector<vec2> vPositions {};
		std::string Text {};
	};
	struct ActionZoneDetail
	{
		std::string Name {};
	};
	struct FixedCamZoneDetail
	{
		vec2 Pos {};
		vec4 Rect {};
		bool Smooth {};
	};

private:
	int m_Width{};
	int m_Height{};
	CTile *m_pTiles{};
	CTile *m_pFront{};
	CTile *m_pExtraTiles{};
	CTeleTile *m_pTele{};
	CSwitchTileExtra *m_pSwitchExtra{};
	CSpeedupTileExtra *m_pSpeedupExtra{};
	CDoorTile *m_pDoor{};
	CLayers *m_pLayers{};

	std::map<int, std::vector<vec2>> m_vTeleOuts {};
	std::vector<FixedCamZoneDetail> m_vFixedCamZones {};
	std::map<int, ZoneDetail> m_vZoneDetail {};
	std::map<int, TextZoneDetail> m_vZoneTextDetail {};
	std::map<int, ActionZoneDetail> m_vActionZoneDetail {};
	std::unordered_map<int, GatheringNode> m_vOreNodes {};
	std::unordered_map<int, GatheringNode> m_vPlantNodes {};
	std::unordered_map<int, GatheringNode> m_vFishNodes {};

	void InitSettings();
	void InitTiles(CTile *pTiles);
	void InitTeleports();
	void InitSwitchExtra();
	void InitSpeedupExtra();

	int GetMainTileFlags(float x, float y) const;
	int GetFrontTileFlags(float x, float y) const;
	int GetExtraTileFlags(float x, float y) const;
	int GetMainTileCollisionFlags(int x, int y) const;
	int GetFrontTileCollisionFlags(int x, int y) const;

public:
	CCollision();
	~CCollision();

	void Init(class CLayers *pLayers);
	typedef void (*InitEntityCallback)(int, vec2, int, void *);
	void InitEntities(InitEntityCallback pfnCallback, void *pUser) const;
	void InitSwitchEntities(InitEntityCallback pfnCallback, void *pUser) const;

	int GetWidth() const { return m_Width; }
	int GetHeight() const { return m_Height; }
	CLayers *GetLayers() const { return m_pLayers; }
	vec2 GetRotateDirByFlags(int Flags);

	// tile pos
	vec2 GetPos(int Index) const;

	// tile index
	int GetMainTileIndex(int Index) const;
	int GetFrontTileIndex(int Index) const;
	int GetExtraTileIndex(int Index) const;

	// switch
	CSwitchTileExtra *GetSwitchTile(vec2 Pos) const;
	int GetSwitchNumber(vec2 Pos) const;
	int GetSwitchNumberAtTileIndex(vec2 Pos, int TileIndex) const;

	// collision flags
	bool CheckPoint(float x, float y, int Flag = COLFLAG_SOLID) const { return (GetCollisionFlagsAt(x, y) & Flag) != 0; }
	int GetCollisionFlagsAt(float x, float y) const
	{
		const ivec2 roundPos(round_to_int(x), round_to_int(y));
		const int TileCF = GetMainTileCollisionFlags(roundPos.x, roundPos.y);
		const int FrontCF = GetFrontTileCollisionFlags(roundPos.x, roundPos.y);
		return TileCF | FrontCF;
	}
	bool CheckPoint(vec2 Pos, int Flag = COLFLAG_SOLID) const { return CheckPoint(Pos.x, Pos.y, Flag); }
	int GetCollisionFlagsAt(vec2 Pos) const { return GetCollisionFlagsAt(Pos.x, Pos.y); }

	// tiles
	bool TileExists(int Index) const;
	bool TileExistsNext(int Index) const;
	int GetMapIndex(vec2 Pos) const;
	int GetPureMapIndex(float x, float y) const;
	int GetPureMapIndex(vec2 pos) const { return GetPureMapIndex(pos.x, pos.y); }
	std::vector<int> GetMapIndices(vec2 PrevPos, vec2 Pos, unsigned MaxIndices = 0) const;

	// doors
	void SetDoorCollisionAt(float x, float y, int Type, int Flags, int Number = -1);
	void SetDoorFromToCollisionAt(vec2 From, vec2 To, int Type, int Flags, int Number = -1);
	void GetDoorTile(int Index, CDoorTile *pDoorTile) const;
	bool IntersectLineDoor(vec2 From, vec2 To) const;

	// zone / text / action
	std::map<int, TextZoneDetail> &GetTextZones() { return m_vZoneTextDetail; }
	bool GetZonedetail(vec2 Pos, ZoneDetail *pOut) const;
	bool IsActiveActionZone(const char *pActionZoneName, vec2 Pos) const;

	// teleport
	bool GetTeleportOut(vec2 currentPos, vec2 *pOut) const;
	const CTeleTile &GetTeleTile(int Index) const { return m_pTele[Index]; }

	// fixed camera
	bool GetFixedCamPos(vec2 currentPos, vec2 *pOutPos, bool *pOutSmooth) const;

	// gathering nodes
	const std::unordered_map<int, GatheringNode> &GetOreNodes() const { return m_vOreNodes; }
	const std::unordered_map<int, GatheringNode> &GetPlantNodes() const { return m_vPlantNodes; }
	const std::unordered_map<int, GatheringNode> &GetFishNodes() const { return m_vFishNodes; }
	GatheringNode *GetOreNode(int SwitchNumber);
	GatheringNode *GetPlantNode(int SwitchNumber);
	GatheringNode *GetFishNode(int SwitchNumber);

	// line intersection
	int IntersectLine(vec2 Pos0, vec2 Pos1, vec2 *pOutCollision, vec2 *pOutBeforeCollision, int ColFlag = COLFLAG_SOLID) const;
	int FastIntersectLine(vec2 Pos0, vec2 Pos1, vec2 *pOutCollision, vec2 *pOutBeforeCollision) const;
	bool IntersectLineWithInvisible(vec2 Pos0, vec2 Pos1, vec2 *pOutCollision, vec2 *pOutBeforeCollision) const
	{
		return IntersectLineColFlag(Pos0, Pos1, pOutCollision, pOutBeforeCollision, COLFLAG_SOLID);
	}
	bool IntersectLineColFlag(vec2 Pos0, vec2 Pos1, vec2 *pOutCollision, vec2 *pOutBeforeCollision, int ColFlag) const;

	// movement
	void MovePoint(vec2 *pInoutPos, vec2 *pInoutVel, float Elasticity, int *pBounces) const;
	void MoveBox(vec2 *pInoutPos, vec2 *pInoutVel, vec2 Size, float Elasticity, bool *pDeath = 0) const;
	void MovePhysicalAngleBox(vec2 *pPos, vec2 *pVel, vec2 Size, float *pAngle, float *pAngleForce, float Elasticity, float Gravity = 0.5f) const;
	void MovePhysicalBox(vec2 *pPos, vec2 *pVel, vec2 Size, float Elasticity, float Gravity = 0.5f) const;

	// tests
	bool TestBox(vec2 Pos, vec2 Size, int Flag = COLFLAG_SOLID) const;
	int TestBoxAt(vec2 Pos, vec2 Size) const;
	int TestBoxMoveAt(vec2 LastPos, vec2 NewPos, vec2 Size) const;

	// movement restrictions
	int GetMoveRestrictions(vec2 Pos, float Distance = 18.0f) const;

	// hooks for gamecontroller
	void SetFlagFor(float x, float y, int Flag);
	void SetFlagFor(vec2 Pos, int Flag) { SetFlagFor(Pos.x, Pos.y, Flag); }

	// TDA legacy helpers
	int GetTile(int x, int y) const;
	int GetCollisionAt(float x, float y) const;

private:
	bool IsTile(int x, int y, int Flag) const;
};

#endif
