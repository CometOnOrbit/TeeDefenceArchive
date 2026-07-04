#ifndef GAME_SERVER_CORE_TOOLS_PATH_FINDER_H
#define GAME_SERVER_CORE_TOOLS_PATH_FINDER_H

#include "path_finder_result.h"
#include <base/vmath.h>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <climits>

class CCollision;

class MapData
{
public:
	MapData() = default;
	MapData(int width, int height);
	~MapData() = default;

	bool IsCollide(int x, int y) const;
	void SetCollide(int x, int y, bool value);
	int Width() const { return m_Width; }
	int Height() const { return m_Height; }

private:
	int m_Width{};
	int m_Height{};
	std::vector<uint8_t> m_Bits;
};

class CPathFinder
{
public:
	CPathFinder(CCollision *pCollision);
	~CPathFinder();

	void Initialize();
	void RequestPath(PathRequestHandle &Handle, const vec2 &Start, const vec2 &End);
	void RequestRandomPath(PathRequestHandle &Handle, const vec2 &Start, float Radius);

private:
	void PathfindingThread();
	std::vector<vec2> FindPath(const ivec2 &Start, const ivec2 &End);
	vec2 GetRandomWaypointRadius(const vec2 &Pos, float Radius) const;

	int m_Width{};
	int m_Height{};
	MapData m_MapData{};
	std::vector<int> m_vCostSoFar{};
	std::vector<ivec2> m_vCameFrom{};

	std::queue<PathRequest> m_vRequestQueue{};
	std::condition_variable m_Condition{};
	std::atomic<bool> m_Running{};
	std::thread m_WorkerThread{};
	std::mutex m_QueueMutex{};

	CCollision *m_pCollision{};
};

#endif
