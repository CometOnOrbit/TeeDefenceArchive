#include "tiles_handler.h"
#include <game/collision.h>
#include <game/server/entities/character.h>

void CTileHandler::Handle(int Index)
{
	const int Indices[TILES_LAYER_NUM] = {
		m_pCollision->GetMainTileIndex(Index),
		m_pCollision->GetFrontTileIndex(Index),
		m_pCollision->GetExtraTileIndex(Index)
	};

	for(int i = 0; i < TILES_LAYER_NUM; ++i)
	{
		const int TileIndex = Indices[i];
		if(TileIndex >= 0)
		{
			if(m_MarkedTiles[i] != TileIndex)
			{
				m_MarkEnter[i] = TileIndex;
				m_MarkExit[i] = m_MarkedTiles[i];
				m_MarkedTiles[i] = TileIndex;
			}
		}
	}

	// position tracking
	m_PrevPlayerPos = m_CurPlayerPos;
	m_CurPlayerPos = m_pCharacter->GetPos();
	if(!m_PosInitialized)
	{
		m_PrevPlayerPos = m_CurPlayerPos;
		m_PosInitialized = true;
	}
}

bool CTileHandler::IsEnter(int TileIndex)
{
	bool Entered = false;
	for(int i = 0; i < TILES_LAYER_NUM; ++i)
	{
		if(TileIndex == m_MarkEnter[i])
		{
			m_MarkEnter[i] = -1;
			Entered = true;
		}
	}
	return Entered;
}

bool CTileHandler::IsExit(int TileIndex)
{
	bool Exited = false;
	for(int i = 0; i < TILES_LAYER_NUM; ++i)
	{
		if(TileIndex == m_MarkExit[i])
		{
			m_MarkExit[i] = -1;
			Exited = true;
		}
	}
	return Exited;
}
