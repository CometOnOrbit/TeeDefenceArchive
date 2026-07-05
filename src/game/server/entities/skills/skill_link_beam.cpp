#include "skill_link_beam.h"
#include "skill_vfx_common.h"

#include <generated/protocol.h>

#include <game/server/gamecontext.h>

#include "../character.h"

CSkillLinkBeam::CSkillLinkBeam(CGameWorld *pGameWorld, vec2 From, vec2 To, int DurationTicks, ESkillVisualStyle Style, int TargetCID)
	: CChildEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, 0, From, 64)
{
	m_From = From;
	m_To = To;
	m_Pos = From;
	m_LifeSpan = maximum(1, DurationTicks);
	m_StartTick = Server()->Tick();
	m_Style = Style;
	m_TargetCID = TargetCID;

	AddSnappingGroupIds(SNAP_GROUP_CENTER, 1);

	GameWorld()->InsertEntity(this);
}

void CSkillLinkBeam::Tick()
{
	if(m_TargetCID >= 0)
	{
		CCharacter *pTarget = GameServer()->GetPlayerChar(m_TargetCID);
		if(!pTarget || !pTarget->IsAlive())
		{
			MarkForDestroy();
			return;
		}
		m_To = pTarget->GetPos();
	}

	m_Pos = (m_From + m_To) * 0.5f;

	if(--m_LifeSpan <= 0)
		MarkForDestroy();
}

void CSkillLinkBeam::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient, m_Pos))
		return;

	const array<int> *pIds = FindSnappingGroupIds(SNAP_GROUP_CENTER);
	if(!pIds || pIds->size() == 0)
		return;

	SnapLaserSegment(Server(), (*pIds)[0], m_From, m_To, m_StartTick);
}
