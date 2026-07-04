#ifndef GAME_SERVER_ENTITIES_PET_H
#define GAME_SERVER_ENTITIES_PET_H

#include <game/server/entity.h>

class CPlayer;

class CPet : public CEntity
{
public:
	CPet(CGameContext *pGameServer, int OwnerCID, int PetID);
	virtual ~CPet() {}

	virtual void Tick();
	virtual void Snap(int SnappingClient);

	int GetOwnerCID() const { return m_OwnerCID; }
	int GetPetID() const { return m_PetID; }
	void SetName(const char *pName) { str_copy(m_aName, pName, sizeof(m_aName)); }
	const char *GetName() const { return m_aName; }

private:
	int m_OwnerCID;
	int m_PetID;
	int m_SnapClientID;
	vec2 m_Pos;
	char m_aName[32];
};

#endif
