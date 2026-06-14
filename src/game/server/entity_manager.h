#ifndef GAME_SERVER_ENTITY_MANAGER_H
#define GAME_SERVER_ENTITY_MANAGER_H

#include <base/vmath.h>

class CGameContext;
class CGameWorld;

class CEntityManager
{
	CGameContext *m_pGS;

public:
	explicit CEntityManager(CGameContext *pGS);

	void Text(vec2 Pos, const char *pText, int LifeTicks = 50) const;
	void TextForClient(int ClientID, vec2 Pos, const char *pText) const;
	void DropItem(vec2 Pos, int ClientID, int ItemId, int Num, vec2 Force = vec2(0.f, 0.f)) const;
};

#endif
