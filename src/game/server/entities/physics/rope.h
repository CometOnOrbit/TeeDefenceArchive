#ifndef GAME_SERVER_ENTITIES_PHYSICS_ROPE_H
#define GAME_SERVER_ENTITIES_PHYSICS_ROPE_H

#include <base/vmath.h>

#include <vector>

class CCollision;

class RopePhysic
{
	vec2 m_StartPoint{};
	vec2 m_Force{};

public:
	std::vector<vec2> m_vPoints{};

	RopePhysic() = default;

	void Init(int NumPoints, const vec2 &Start, const vec2 &Force);
	void UpdatePhysics(CCollision *pCollision, float PointMass = 3.f, float Tension = 8.f, float MaxStretch = 64.f);
	void SetForce(const vec2 &Force);
};

#endif
