#include "path_finder.h"
#include <base/system.h>
#include <game/collision.h>
#include <base/math.h>
#include <cstring>
#include <algorithm>

MapData::MapData(int width, int height)
	: m_Width(width), m_Height(height)
{
	m_Bits.resize((width * height + 7) / 8, 0);
}

bool MapData::IsCollide(int x, int y) const
{
	if(x < 0 || x >= m_Width || y < 0 || y >= m_Height)
		return true;
	size_t index = (size_t)y * m_Width + x;
	return (m_Bits[index / 8] & (1 << (index % 8))) != 0;
}

void MapData::SetCollide(int x, int y, bool value)
{
	if(x < 0 || x >= m_Width || y < 0 || y >= m_Height)
		return;
	size_t index = (size_t)y * m_Width + x;
	if(value)
		m_Bits[index / 8] |= (1 << (index % 8));
	else
		m_Bits[index / 8] &= ~(1 << (index % 8));
}

static int ManhattanDistance(const ivec2 &a, const ivec2 &b)
{
	return std::abs(a.x - b.x) + std::abs(a.y - b.y);
}

CPathFinder::CPathFinder(CCollision *pCollision)
	: m_pCollision(pCollision)
{
	m_Width = pCollision->GetWidth();
	m_Height = pCollision->GetHeight();

	m_MapData = MapData(m_Width, m_Height);
	m_vCostSoFar.resize((size_t)m_Width * m_Height, std::numeric_limits<int>::max());
	m_vCameFrom.resize((size_t)m_Width * m_Height, ivec2{-1, -1});
	Initialize();
}

CPathFinder::~CPathFinder()
{
	{
		std::lock_guard<std::mutex> lock(m_QueueMutex);
		m_Running = false;
	}
	m_Condition.notify_one();
	if(m_WorkerThread.joinable())
		m_WorkerThread.join();
}

void CPathFinder::Initialize()
{
	for(int y = 0; y < m_Height; y++)
	{
		for(int x = 0; x < m_Width; x++)
		{
			vec2 Position((float)x * 32.f + 16.f, (float)y * 32.f + 16.f);
			m_MapData.SetCollide(x, y, m_pCollision->CheckPoint(Position));
		}
	}

	m_Running = true;
	m_WorkerThread = std::thread(&CPathFinder::PathfindingThread, this);
}

void CPathFinder::RequestPath(PathRequestHandle &Handle, const vec2 &Start, const vec2 &End)
{
	if(Handle.IsValid())
		return;

	ivec2 istart((int)Start.x / 32, (int)Start.y / 32);
	ivec2 iend((int)End.x / 32, (int)End.y / 32);

	PathRequest request;
	request.Start = istart;
	request.End = iend;
	Handle.Future = request.Promise.get_future();

	{
		std::lock_guard<std::mutex> lock(m_QueueMutex);
		m_vRequestQueue.push(std::move(request));
	}
	m_Condition.notify_one();
}

void CPathFinder::RequestRandomPath(PathRequestHandle &Handle, const vec2 &Start, float Radius)
{
	RequestPath(Handle, Start, GetRandomWaypointRadius(Start, Radius));
}

struct CompareNode
{
	bool operator()(const std::pair<int, ivec2> &a, const std::pair<int, ivec2> &b) const
	{
		return a.first > b.first;
	}
};

void CPathFinder::PathfindingThread()
{
	while(m_Running)
	{
		std::vector<PathRequest> currentRequestsBatch;

		{
			std::unique_lock<std::mutex> lock(m_QueueMutex);
			m_Condition.wait(lock, [this] { return !m_vRequestQueue.empty() || !m_Running.load(); });

			if(!m_Running.load() && m_vRequestQueue.empty())
				return;

			while(!m_vRequestQueue.empty())
			{
				currentRequestsBatch.push_back(std::move(m_vRequestQueue.front()));
				m_vRequestQueue.pop();
			}
		}

		for(auto &request : currentRequestsBatch)
		{
			std::vector<vec2> vPath = FindPath(request.Start, request.End);
			bool bSuccess = !vPath.empty();
			auto resultPtr = std::make_unique<PathResult>(PathResult{std::move(vPath), bSuccess});
			request.Promise.set_value(std::move(resultPtr));
		}
	}
}

std::vector<vec2> CPathFinder::FindPath(const ivec2 &Start, const ivec2 &End)
{
	std::vector<vec2> vPath;

	if(Start.x < 0 || Start.x >= m_Width || Start.y < 0 || Start.y >= m_Height ||
		End.x < 0 || End.x >= m_Width || End.y < 0 || End.y >= m_Height)
	{
		dbg_msg("path_finder", "invalid start/end coordinates: (%d,%d)->(%d,%d)", Start.x, Start.y, End.x, End.y);
		return vPath;
	}

	if(m_MapData.IsCollide(Start.x, Start.y) || m_MapData.IsCollide(End.x, End.y))
		return vPath;

	if(Start.x == End.x && Start.y == End.y)
	{
		vPath.emplace_back((float)Start.x * 32.f + 16.f, (float)Start.y * 32.f + 16.f);
		return vPath;
	}

	std::fill(m_vCostSoFar.begin(), m_vCostSoFar.end(), std::numeric_limits<int>::max());

	auto ToIndex = [this](const ivec2 &pos) -> size_t
	{
		return (size_t)pos.y * m_Width + (size_t)pos.x;
	};

	using Node = std::pair<int, ivec2>;
	std::priority_queue<Node, std::vector<Node>, CompareNode> vFrontier;

	vFrontier.emplace(0, Start);
	m_vCostSoFar[ToIndex(Start)] = 0;
	m_vCameFrom[ToIndex(Start)] = Start;
	m_vCameFrom[ToIndex(End)] = ivec2{-1, -1};

	const ivec2 directions[4] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};

	while(!vFrontier.empty())
	{
		ivec2 current = vFrontier.top().second;
		vFrontier.pop();

		if(current.x == End.x && current.y == End.y)
			break;

		size_t currentIndex = ToIndex(current);

		for(int d = 0; d < 4; d++)
		{
			ivec2 next = {current.x + directions[d].x, current.y + directions[d].y};
			if(next.x < 0 || next.x >= m_Width || next.y < 0 || next.y >= m_Height)
				continue;
			if(m_MapData.IsCollide(next.x, next.y))
				continue;

			size_t nextIndex = ToIndex(next);
			int newCost = m_vCostSoFar[currentIndex] + 1;
			if(newCost < m_vCostSoFar[nextIndex])
			{
				m_vCostSoFar[nextIndex] = newCost;
				int priority = newCost + ManhattanDistance(next, End);
				vFrontier.emplace(priority, next);
				m_vCameFrom[nextIndex] = current;
			}
		}
	}

	ivec2 endCheck = m_vCameFrom[ToIndex(End)];
	if(endCheck.x != -1 || endCheck.y != -1)
	{
		vPath.reserve(ManhattanDistance(Start, End));
		for(ivec2 current = End; current.x != Start.x || current.y != Start.y; current = m_vCameFrom[ToIndex(current)])
		{
			vPath.emplace_back((float)current.x * 32.f + 16.f, (float)current.y * 32.f + 16.f);
		}
		vPath.emplace_back((float)Start.x * 32.f + 16.f, (float)Start.y * 32.f + 16.f);
		std::reverse(vPath.begin(), vPath.end());
	}

	return vPath;
}

vec2 CPathFinder::GetRandomWaypointRadius(const vec2 &Pos, float Radius) const
{
	float RadiusSquared = Radius * Radius;
	int StartX = clamp((int)((Pos.x - Radius) / 32.0f), 0, m_Width - 1);
	int StartY = clamp((int)((Pos.y - Radius) / 32.0f), 0, m_Height - 1);
	int EndX = clamp((int)((Pos.x + Radius) / 32.0f), 0, m_Width - 1);
	int EndY = clamp((int)((Pos.y + Radius) / 32.0f), 0, m_Height - 1);

	vec2 selectedPoint = {-1.0f, -1.0f};
	int count = 0;

	for(int y = StartY; y <= EndY; ++y)
	{
		float yCenter = y * 32.0f + 16.0f;
		float deltaY = Pos.y - yCenter;

		for(int x = StartX; x <= EndX; ++x)
		{
			if(!m_MapData.IsCollide(x, y))
			{
				float xCenter = x * 32.0f + 16.0f;
				float deltaX = Pos.x - xCenter;
				if(deltaX * deltaX + deltaY * deltaY <= RadiusSquared)
				{
					++count;
					if(random_int() % count == 0)
						selectedPoint = vec2(xCenter, yCenter);
				}
			}
		}
	}

	return selectedPoint;
}
