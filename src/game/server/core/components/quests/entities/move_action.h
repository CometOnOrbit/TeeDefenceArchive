#ifndef GAME_SERVER_COMPONENT_QUEST_MOVE_ACTION_H
#define GAME_SERVER_COMPONENT_QUEST_MOVE_ACTION_H

#include <game/server/entity.h>

class CEntityMoveAction : public CEntity
{
vec2 m_Position{};
int m_ClientID{};
int m_WorldID{};
int m_Cooldown{};
unsigned m_TypeFlags{};
int m_SpawnTick{};

public:
CEntityMoveAction(CGameContext* pGameServer, vec2 Position, int WorldID, int ClientID, int Cooldown, unsigned TypeFlags);
~CEntityMoveAction() override = default;

void Reset() override;
void Tick() override;
void Snap(int SnappingClient) override;
};

#endif
