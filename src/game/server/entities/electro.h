/* (c) Siile / TeeDefenceArchive */
#ifndef GAME_SERVER_ENTITIES_ELECTRO_H
#define GAME_SERVER_ENTITIES_ELECTRO_H

#include <game/server/entity.h>

class CElectro : public CEntity
{
public:
	CElectro(CGameWorld *pGameWorld, vec2 Start, vec2 End, vec2 Offset, int Left);

	void Reset() override;
	void Tick() override;
	void TickPaused() override;
	void Snap(int SnappingClient) override;

private:
	vec2 m_End;
	bool m_Render;
	int m_EvalTick;
};

#endif
