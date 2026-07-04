#ifndef GAME_SERVER_CORE_TOOLS_TILES_HANDLER_H
#define GAME_SERVER_CORE_TOOLS_TILES_HANDLER_H

#include <base/vmath.h>
#include <base/system.h>
#include <string_view>

constexpr int TILES_LAYER_NUM = 3;

class CCollision;
class CCharacter;

class CTileHandler
{
	CCollision *m_pCollision{};
	CCharacter *m_pCharacter{};
	int m_MarkedTiles[TILES_LAYER_NUM]{};
	int m_MarkEnter[TILES_LAYER_NUM]{};
	int m_MarkExit[TILES_LAYER_NUM]{};
	vec2 m_PrevPlayerPos{};
	vec2 m_CurPlayerPos{};
	bool m_PosInitialized{};
	int m_MoveRestrictions{};

public:
	explicit CTileHandler(CCollision *pCollision, CCharacter *pCharacter)
		: m_pCollision(pCollision), m_pCharacter(pCharacter) {}

	void Handle(int Index);

	bool IsEnter(int TileIndex);
	bool IsExit(int TileIndex);
	bool IsActive(int TileIndex) const
	{
		return m_MarkedTiles[0] == TileIndex || m_MarkedTiles[1] == TileIndex || m_MarkedTiles[2] == TileIndex;
	}

	template <typename... Ts>
	bool AreAnyEnter(Ts... args) { return ((IsEnter(args)) || ...); }
	template <typename... Ts>
	bool AreAnyExit(Ts... args) { return ((IsExit(args)) || ...); }
	template <typename... Ts>
	bool AreAnyActive(Ts... args) const { return ((IsActive(args)) || ...); }
};

#endif
