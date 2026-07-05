#include "skill_super_nova.h"

#include <base/math.h>

#include <generated/server_data.h>
#include <game/server/gamecontext.h>

#include "../character.h"
#include "../mmo/mmo_weapon_common.h"

CSkillSuperNova::CSkillSuperNova(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, int Damage, int MaxLife)
	: CChildEntity(pGameWorld, CGameWorld::ENTTYPE_GROWINGEXPLOSION, 0, Pos, 256)
{
	m_Pos = Pos;
	m_Owner = OwnerCID;
	m_Damage = maximum(1, Damage);
	m_MaxLife = maximum(1, MaxLife);
	m_Life = 0;
	m_NextIn = 0;

	GameWorld()->InsertEntity(this);
}

void CSkillSuperNova::Tick()
{
	CCharacter *pOwnerChar = MMOWeaponOwnerChar(GameServer(), m_Owner);
	if(!pOwnerChar)
	{
		MarkForDestroy();
		return;
	}

	if(m_NextIn-- > 0)
		return;
	m_NextIn = 2;

	GameWorld()->CreateSound(m_Pos, SOUND_GRENADE_EXPLODE);

	if(m_Life == 0)
	{
		GameWorld()->CreateExplosion(m_Pos, pOwnerChar, WEAPON_GRENADE, m_Damage);
	}
	else
	{
		const int Steps = m_Life * 4;
		const float StepAngle = 360.f / (float)Steps;
		float Angle = StepAngle / 2.f;

		for(int i = 0; i < Steps; i++)
		{
			const vec2 Dir = vec2(cosf(Angle * pi / 180.f), sinf(Angle * pi / 180.f));
			const vec2 To = m_Pos + Dir * (float)(m_Life * 64);

			if(!GameServer()->Collision()->IntersectLine(m_Pos + Dir * 48.f, To, nullptr, nullptr))
				GameWorld()->CreateExplosion(To, pOwnerChar, WEAPON_GRENADE, maximum(1, m_Damage / 2));

			Angle += StepAngle;
		}
	}

	m_Life++;
	if(m_Life > m_MaxLife)
		MarkForDestroy();
}
