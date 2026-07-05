#ifndef GAME_SERVER_ENTITIES_SKILLS_SKILL_LINK_BEAM_H
#define GAME_SERVER_ENTITIES_SKILLS_SKILL_LINK_BEAM_H

#include "skill_cast_ring.h"
#include <game/server/entity.h>

class CSkillLinkBeam : public CChildEntity
{
	vec2 m_From;
	vec2 m_To;
	int m_LifeSpan;
	int m_StartTick;
	ESkillVisualStyle m_Style;
	int m_TargetCID;

public:
	CSkillLinkBeam(CGameWorld *pGameWorld, vec2 From, vec2 To, int DurationTicks, ESkillVisualStyle Style, int TargetCID = -1);

	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif
