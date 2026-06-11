// Comet: AI wrote this AI but base on nheir bot
#include <base/math.h>
#include <base/system.h>
#include <cstdlib>
#include <game/gamecore.h>
#include <engine/shared/config.h>
#include <game/layers.h>
#include <game/mapitems.h>
#include <game/collision.h>
#include "gamecontext.h"

#include "botengine.h"
#include "botengine_tables.inc"

CGraph::CGraph()
{
	m_NumVertices = 0;
	m_NumEdges = 0;
	m_pEdges = 0;
	m_pVertices = 0;
	m_pClosestPath = 0;
	m_Diameter = 0;
}
CGraph::~CGraph()
{
	Free();
}
void CGraph::Reset()
{
	Free();
	m_NumVertices = 0;
	m_NumEdges = 0;
	m_Diameter = 0;
}
void CGraph::Free()
{
	if(m_pEdges)
		mem_free(m_pEdges);
	if(m_pVertices)
		mem_free(m_pVertices);
	if(m_pClosestPath)
		mem_free(m_pClosestPath);
	m_pVertices = 0;
	m_pEdges = 0;
	m_pClosestPath = 0;
}

void CGraph::ComputeClosestPath()
{
	if(!m_pEdges || !m_pVertices || m_NumVertices <= 0)
		return;
	if(m_pClosestPath)
		mem_free(m_pClosestPath);
	m_pClosestPath = (int*)mem_alloc(m_NumVertices*m_NumVertices*sizeof(int));
	if(!m_pClosestPath)
		return;
	int* Dist = (int*)mem_alloc(m_NumVertices*m_NumVertices*sizeof(int));
	if(!Dist)
		return;
	for(int i = 0 ; i < m_NumVertices*m_NumVertices ; i++)
	{
		m_pClosestPath[i] = -1;
		Dist[i] = 1000000;
	}
	for(int i=0; i < m_NumVertices ; i++)
		Dist[i*(1+m_NumVertices)] = 0;
	for(int i=0; i < m_NumEdges ; i++)
	{
		if(m_pEdges[i].m_Size < Dist[m_pEdges[i].m_StartID + m_pEdges[i].m_EndID * m_NumVertices])
			Dist[m_pEdges[i].m_StartID + m_pEdges[i].m_EndID * m_NumVertices] = m_pEdges[i].m_Size-1;
		m_pClosestPath[m_pEdges[i].m_StartID + m_pEdges[i].m_EndID * m_NumVertices] = m_pEdges[i].m_EndID;
	}

	for(int k = 0; k < m_NumVertices ; k++)
	{
		for(int i = 0; i < m_NumVertices ; i++)
		{
			for(int j = 0; j < m_NumVertices ; j++)
			{
				if(Dist[i + k * m_NumVertices] + Dist[k + j * m_NumVertices] < Dist[i + j * m_NumVertices])
				{
					Dist[i + j * m_NumVertices] = Dist[i + k * m_NumVertices] + Dist[k + j * m_NumVertices];
					m_pClosestPath[i + j * m_NumVertices] = m_pClosestPath[i + k * m_NumVertices];
				}
			}
		}
	}
	m_Diameter = 1;
	for(int i = 0 ; i < m_NumVertices*m_NumVertices ; i++)
		if(Dist[i] < 1000000 && m_Diameter < Dist[i])
			m_Diameter = Dist[i];
	dbg_msg("botengine","closest path computed, diameter=%d",m_Diameter);
	mem_free(Dist);
}

int CGraph::GetPath(int Start, int End, vec2 *pVertices, int MaxInner)
{
	Start = clamp(Start,0,m_NumVertices-1);
	End = clamp(End,0,m_NumVertices-1);
	if(m_pClosestPath[Start + End * m_NumVertices] < 0)
		return 0;
	int Size = 0;
	int Cur = Start;
	while(Cur != End)
	{
		if(Size >= MaxInner)
			return 0;
		pVertices[Size++] = m_pVertices[Cur].m_Pos;
		Cur = m_pClosestPath[Cur + End * m_NumVertices];
	}
	if(Size >= MaxInner)
		return 0;
	pVertices[Size++] = m_pVertices[Cur].m_Pos;

	return Size;
}


CBotEngine::CBotEngine(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
	m_pGrid = 0;
	m_Triangulation.m_pTriangles = 0;
	m_Triangulation.m_Size = 0;
	m_pCorners = 0;
	m_CornerCount = 0;
	m_pSegments = 0;
	m_SegmentCount = 0;
	mem_zero(m_aPaths,sizeof(m_aPaths));
	mem_zero(m_apBotPlaceholder, sizeof(m_apBotPlaceholder));
}

void CBotEngine::Free()
{
	if(m_pGrid)
		mem_free(m_pGrid);

	for (int i = 0; i < m_Triangulation.m_Size; i++)
		for(int k = 0 ; k < 3; k++)
			GameServer()->Server()->SnapFreeID(m_Triangulation.m_pTriangles[i].m_aSnapID[k], GameServer()->GetWorldID());
	if(m_Triangulation.m_pTriangles)
		mem_free(m_Triangulation.m_pTriangles);
	m_Triangulation.m_Size = 0;
	m_Triangulation.m_pTriangles = 0;

	if(m_pCorners)
		mem_free(m_pCorners);
	m_CornerCount = 0;
	m_pCorners = 0;

	for(int k = 0; k < m_SegmentCount; k++)
	{
		CSegment *pSegment = m_pSegments + k;
		GameServer()->Server()->SnapFreeID(pSegment->m_SnapID, GameServer()->GetWorldID());
	}
	if(m_pSegments)
		mem_free(m_pSegments);
	m_SegmentCount = 0;
	m_pSegments = 0;

	for(int k = 0; k < m_Graph.m_NumEdges; k++)
	{
		CEdge *pEdge = m_Graph.m_pEdges + k;
		GameServer()->Server()->SnapFreeID(pEdge->m_SnapID, GameServer()->GetWorldID());
	}
	m_Graph.Free();

	for(int c = 0; c < MAX_CLIENTS; c++)
	{
		if(m_aPaths[c].m_pVertices)
			mem_free(m_aPaths[c].m_pVertices);
	}
	mem_zero(m_aPaths,sizeof(m_aPaths));
}

static int GridTileFromCollision(CCollision *pCol, int i, int j)
{
	const int f = pCol->GetCollisionAt((float)(i * 32 + 16), (float)(j * 32 + 16));
	if(f & CCollision::COLFLAG_DEATH)
		return GTILE_DEATH;
	if(f & CCollision::COLFLAG_SOLID)
	{
		if(f & CCollision::COLFLAG_UNHOOKABLE)
			return GTILE_NOHOOK;
		return GTILE_SOLID;
	}
	return GTILE_AIR;
}

CBotEngine::~CBotEngine()
{
	Free();
}

void CBotEngine::Init(CCollision *pCollision)
{
	if(!pCollision)
		return;

	m_Width = pCollision->GetWidth();
	m_Height = pCollision->GetHeight();

	Free();

	m_pGrid = (int *)mem_alloc(m_Width * m_Height * sizeof(int));
	if(m_pGrid)
	{
		mem_zero(m_pGrid, m_Width * m_Height * sizeof(int));

		int j = m_Height - 1;
		int Margin = 6;
		for(int i = 0; i < m_Width; i++)
		{
			m_pGrid[i + j * m_Width] = GridTileFromCollision(pCollision, i, j);

			if(m_pGrid[i + j * m_Width] == GTILE_SOLID || m_pGrid[i + j * m_Width] == GTILE_NOHOOK)
				m_pGrid[i + j * m_Width] |= BTILE_SAFE;
			else
				m_pGrid[i + j * m_Width] |= BTILE_HOLE;
		}
		for(int i = Margin; i < m_Width; i++)
		{
			if(m_pGrid[i + j * m_Width] & BTILE_SAFE)
				break;
			else
				m_pGrid[i - Margin + j * m_Width] |= BTILE_LHOLE;
		}
		for(int i = m_Width - 1; i >= Margin; i--)
		{
			if(m_pGrid[i - Margin + j * m_Width] & BTILE_SAFE)
				break;
			else
				m_pGrid[i + j * m_Width] |= BTILE_RHOLE;
		}
		for(j = m_Height - 2; j >= 0; j--)
		{
			for(int i = 0; i < m_Width; i++)
			{
				m_pGrid[i + j * m_Width] = GridTileFromCollision(pCollision, i, j);

				if(m_pGrid[i + j * m_Width] == GTILE_SOLID || m_pGrid[i + j * m_Width] == GTILE_NOHOOK)
					m_pGrid[i + j * m_Width] |= BTILE_SAFE;
				else if(m_pGrid[i + j * m_Width] == GTILE_DEATH)
					m_pGrid[i + j * m_Width] |= BTILE_HOLE;
				else
				{
					bool Left = i > 0 && m_pGrid[i - 1 + (j + 1) * m_Width] & BTILE_SAFE && !(m_pGrid[i + (j + 1) * m_Width] & BTILE_RHOLE) && (m_pGrid[i - 1 + (j + 1) * m_Width] & GTILE_MASK) <= GTILE_AIR;
					bool Right = i < m_Width - 1 && m_pGrid[i + 1 + (j + 1) * m_Width] & BTILE_SAFE && !(m_pGrid[i + (j + 1) * m_Width] & BTILE_LHOLE) && (m_pGrid[i + 1 + (j + 1) * m_Width] & GTILE_MASK) <= GTILE_AIR;

					if(Left || Right || m_pGrid[i + (j + 1) * m_Width] & BTILE_SAFE)
						m_pGrid[i + j * m_Width] |= BTILE_SAFE;
					else
						m_pGrid[i + j * m_Width] |= BTILE_HOLE;
				}
			}
			for(int i = Margin; i < m_Width; i++)
			{
				if(!((m_pGrid[i + j * m_Width] & GTILE_MASK) <= GTILE_AIR))
					break;
				else
					m_pGrid[i - Margin + j * m_Width] |= BTILE_LHOLE;
			}
			for(int i = m_Width - 1; i >= Margin; i--)
			{
				if(!((m_pGrid[i - Margin + j * m_Width] & GTILE_MASK) <= GTILE_AIR))
					break;
				else
					m_pGrid[i + j * m_Width] |= BTILE_RHOLE;
			}
		}

		GenerateCorners();
		GenerateSegments();
		GenerateTriangles();

		GenerateGraphFromTriangles();

		for(int c = 0; c < MAX_CLIENTS; c++)
		{
			/* Diameter is sum of edge weights; hop count (vertices) can be larger. Bound by |V|. */
			m_aPaths[c].m_MaxSize =
				maximum(maximum(4, m_Graph.m_NumVertices + 4), m_Graph.m_Diameter + 2);
			m_aPaths[c].m_pVertices = (vec2 *)mem_alloc(m_aPaths[c].m_MaxSize * sizeof(vec2));
			m_aPaths[c].m_Size = 0;
		}
	}
}

void CBotEngine::GenerateSegments()
{
	int VSegmentCount = 0;
	int HSegmentCount = 0;
	// Vertical segments
	for(int i = 1;i < m_Width-1; i++)
	{
		bool right = false;
		bool left = false;
		for(int j = 0; j < m_Height; j++)
		{
			if((m_pGrid[i+j*m_Width] & GTILE_MASK) > GTILE_AIR && (m_pGrid[i+1+j*m_Width] & GTILE_MASK) <= GTILE_AIR)
				left = true;
			else if(left)
			{
				VSegmentCount++;
				left = false;
			}
			if((m_pGrid[i+j*m_Width] & GTILE_MASK) > GTILE_AIR && (m_pGrid[i-1+j*m_Width] & GTILE_MASK) <= GTILE_AIR)
				right = true;
			else if(right)
			{
				VSegmentCount++;
				right = false;
			}
		}
		if(left)
			VSegmentCount++;
		if(right)
			VSegmentCount++;
	}
	// Horizontal segments
	for(int j = 1; j < m_Height-1; j++)
	{
		bool up = false;
		bool down = false;
		for(int i = 0; i < m_Width; i++)
		{
			if(up && ((m_pGrid[i+j*m_Width] & GTILE_MASK) <= GTILE_AIR || (m_pGrid[i+(j+1)*m_Width] & GTILE_MASK) > GTILE_AIR))
			{
				HSegmentCount++;
				up = false;
			}
			if(down && ((m_pGrid[i+j*m_Width] & GTILE_MASK) <= GTILE_AIR || (m_pGrid[i+(j-1)*m_Width] & GTILE_MASK) > GTILE_AIR))
			{
				HSegmentCount++;
				down = false;
			}

			if(!up && (m_pGrid[i+j*m_Width] & GTILE_MASK) > GTILE_AIR && (m_pGrid[i+(j+1)*m_Width] & GTILE_MASK) <= GTILE_AIR)
				up = true;
			if(!down && (m_pGrid[i+j*m_Width] & GTILE_MASK) > GTILE_AIR && (m_pGrid[i+(j-1)*m_Width] & GTILE_MASK) <= GTILE_AIR)
				down = true;
		}
		if(up)
			HSegmentCount++;
		if(down)
			HSegmentCount++;
	}
	dbg_msg("botengine","Found %d vertical segments, and %d horizontal segments", VSegmentCount, HSegmentCount);
	m_SegmentCount = HSegmentCount+VSegmentCount;
	m_HSegmentCount = HSegmentCount;
	m_pSegments = (CSegment*) mem_alloc(m_SegmentCount*sizeof(CSegment));
	CSegment* pSegment = m_pSegments;
	// Horizontal segments
	for(int j = 1; j < m_Height-1; j++)
	{
		bool up = false;
		bool down = false;
		int up_i = 1, down_i = 1;
		for(int i = 0; i < m_Width; i++)
		{
			if((m_pGrid[i+j*m_Width] & GTILE_MASK) > GTILE_AIR && (m_pGrid[i+(j+1)*m_Width] & GTILE_MASK) <= GTILE_AIR)
			{
				if(!up)
					up_i = i;
				up = true;
			}
			else if(up)
			{
				pSegment->m_IsVertical = false;
				if(!up_i)
					up_i = -200;
				pSegment->m_A = vec2(up_i*32,(j+1)*32);
				pSegment->m_B = vec2(i*32,(j+1)*32);
				pSegment->m_SnapID = GameServer()->Server()->SnapNewID(GameServer()->GetWorldID());
				pSegment++;
				up = false;
			}

			if((m_pGrid[i+j*m_Width] & GTILE_MASK) > GTILE_AIR && (m_pGrid[i+(j-1)*m_Width] & GTILE_MASK) <= GTILE_AIR)
			{
				if(!down)
					down_i = i;
				down = true;
			}
			else if(down)
			{
				pSegment->m_IsVertical = false;
				if(!down_i)
					down_i = -200;
				pSegment->m_A = vec2(down_i*32,j*32);
				pSegment->m_B = vec2(i*32,j*32);
				pSegment->m_SnapID = GameServer()->Server()->SnapNewID(GameServer()->GetWorldID());
				pSegment++;
				down = false;
			}
		}
		if(up)
		{
			pSegment->m_IsVertical = false;
			if(!up_i)
				up_i = -200;
			pSegment->m_A = vec2(up_i*32,(j+1)*32);
			pSegment->m_B = vec2((m_Width+200)*32,(j+1)*32);
			pSegment->m_SnapID = GameServer()->Server()->SnapNewID(GameServer()->GetWorldID());
			pSegment++;
			up = false;
		}
		if(down)
		{
			pSegment->m_IsVertical = false;
			if(!down_i)
				down_i = -200;
			pSegment->m_A = vec2(down_i*32,j*32);
			pSegment->m_B = vec2((m_Width+200)*32,j*32);
			pSegment->m_SnapID = GameServer()->Server()->SnapNewID(GameServer()->GetWorldID());
			pSegment++;
			down = false;
		}
	}
	// Vertical segments
	for(int i = 1;i < m_Width-1; i++)
	{
		bool right = false;
		bool left = false;
		int right_j = 1, left_j = 1;
		for(int j = 0; j < m_Height; j++)
		{
			if((m_pGrid[i+j*m_Width] & GTILE_MASK) > GTILE_AIR && (m_pGrid[i+1+j*m_Width] & GTILE_MASK) <= GTILE_AIR)
			{
				if(!left)
					left_j = j;
				left = true;
			}
			else if(left)
			{
				pSegment->m_IsVertical = true;
				if(!left_j)
					left_j = -200;
				pSegment->m_A = vec2((i+1)*32,left_j*32);
				pSegment->m_B = vec2((i+1)*32,j*32);
				pSegment->m_SnapID = GameServer()->Server()->SnapNewID(GameServer()->GetWorldID());
				pSegment++;
				left = false;
			}
			if((m_pGrid[i+j*m_Width] & GTILE_MASK) > GTILE_AIR && (m_pGrid[i-1+j*m_Width] & GTILE_MASK) <= GTILE_AIR)
			{
				if(!right)
					right_j = j;
				right = true;
			}
			else if(right)
			{
				pSegment->m_IsVertical = true;
				if(!right_j)
					right_j = -200;
				pSegment->m_A = vec2(i*32,right_j*32);
				pSegment->m_B = vec2(i*32,j*32);
				pSegment->m_SnapID = GameServer()->Server()->SnapNewID(GameServer()->GetWorldID());
				pSegment++;
				right = false;
			}
		}
		if(left)
		{
			pSegment->m_IsVertical = true;
			if(!left_j)
				left_j = -200;
			pSegment->m_A = vec2((i+1)*32,left_j*32);
			pSegment->m_B = vec2((i+1)*32,(m_Height+200)*32);
			pSegment->m_SnapID = GameServer()->Server()->SnapNewID(GameServer()->GetWorldID());
			pSegment++;
			left = false;
		}
		if(right)
		{
			pSegment->m_IsVertical = true;
			if(!right_j)
				right_j = -200;
			pSegment->m_A = vec2(i*32,right_j*32);
			pSegment->m_B = vec2(i*32,(m_Height+200)*32);
			pSegment->m_SnapID = GameServer()->Server()->SnapNewID(GameServer()->GetWorldID());
			pSegment++;
			right = false;
		}
	}
	dbg_msg("botengine", "Allocate %d segments, use %d", m_SegmentCount, (int)(pSegment - m_pSegments));
	if(m_SegmentCount != (int)(pSegment - m_pSegments))
		dbg_msg("botengine", "segment count mismatch: allocated %d, filled %d", m_SegmentCount, (int)(pSegment - m_pSegments));
	qsort(m_pSegments,HSegmentCount,sizeof(CSegment),SegmentComp);
	qsort(m_pSegments+HSegmentCount,VSegmentCount,sizeof(CSegment),SegmentComp);
}

int CBotEngine::SegmentComp(const void *a, const void *b)
{
	CSegment *s0 = (CSegment *)a;
	CSegment *s1 = (CSegment *)b;
	if(s0->m_IsVertical && !s1->m_IsVertical)
		return 1;
	if(!s0->m_IsVertical && s1->m_IsVertical)
		return -1;
	if(s0->m_IsVertical)
	{
		if(s0->m_A.x < s1->m_A.x)
			return -1;
		if(s0->m_A.x > s1->m_A.x)
			return 1;
		if(s0->m_A.y < s1->m_A.y)
			return -1;
		if(s0->m_A.y > s1->m_A.y)
			return 1;
		return 0;
	}
	if(s0->m_A.y < s1->m_A.y)
		return -1;
	if(s0->m_A.y > s1->m_A.y)
		return 1;
	if(s0->m_A.x < s1->m_A.x)
		return -1;
	if(s0->m_A.x > s1->m_A.x)
		return 1;
	return 0;
}

void CBotEngine::GenerateCorners()
{
	int CornerCount = 0;
	for(int i = 1;i < m_Width - 1; i++)
	{
		for(int j = 1; j < m_Height - 1; j++)
		{
			if((m_pGrid[i+j*m_Width] & GTILE_MASK) > GTILE_AIR)
				continue;
			int n = 0;
			for(int k = 0; k < 8; k++)
			{
				int Index = i+g_Neighboors[k][0]+(j+g_Neighboors[k][1])*m_Width;
				if((m_pGrid[Index] & GTILE_MASK) > GTILE_AIR)
					n += g_PowerTwo[k];
			}
			if(g_IsOuterCorner[n])
				CornerCount++;
		}
	}
	dbg_msg("botengine","Found %d outer corners", CornerCount);
	m_pCorners = (vec2*)mem_alloc(CornerCount*sizeof(vec2));
	m_CornerCount = CornerCount;
	int m = 0;
	for(int i = 1;i < m_Width - 1; i++)
	{
		for(int j = 1; j < m_Height - 1; j++)
		{
			if((m_pGrid[i+j*m_Width] & GTILE_MASK) > GTILE_AIR)
				continue;
			int n = 0;
			for(int k = 0; k < 8; k++)
			{
				int Index = i+g_Neighboors[k][0]+(j+g_Neighboors[k][1])*m_Width;
				if((m_pGrid[Index] & GTILE_MASK) > GTILE_AIR)
					n += g_PowerTwo[k];
			}
			if(g_IsOuterCorner[n])
				m_pCorners[m++] = ConvertIndex(i+j*m_Width);
		}
	}
}

void CBotEngine::GenerateTriangles()
{
	int CornerCount = 0;
	for(int i = 1;i < m_Width - 1; i++)
	{
		for(int j = 1; j < m_Height - 1; j++)
		{
			if((m_pGrid[i+j*m_Width] & GTILE_MASK) > GTILE_AIR)
				continue;
			int n = 0;
			for(int k = 0; k < 8; k++)
			{
				int Index = i+g_Neighboors[k][0]+(j+g_Neighboors[k][1])*m_Width;
				if((m_pGrid[Index] & GTILE_MASK) > GTILE_AIR)
					n += g_PowerTwo[k];
			}
			if(g_IsInnerCorner[n] || g_IsOuterCorner[n])
				CornerCount++;
		}
	}
	dbg_msg("botengine","Found %d corners", CornerCount);
	m_Triangulation.m_pTriangles = (CTriangulation::CTriangleData*)mem_alloc(2*CornerCount*sizeof(CTriangulation::CTriangleData));
	vec2 *Corners = (vec2*)mem_alloc((CornerCount+3)*sizeof(vec2));
	int m = 0;
	for(int i = 1;i < m_Width - 1; i++)
	{
		for(int j = 1; j < m_Height - 1; j++)
		{
			if((m_pGrid[i+j*m_Width] & GTILE_MASK) > GTILE_AIR)
				continue;
			int n = 0;
			for(int k = 0; k < 8; k++)
			{
				int Index = i+g_Neighboors[k][0]+(j+g_Neighboors[k][1])*m_Width;
				if((m_pGrid[Index] & GTILE_MASK) > GTILE_AIR)
					n += g_PowerTwo[k];
			}
			if(g_IsInnerCorner[n] || g_IsOuterCorner[n])
				Corners[m++] = vec2(i, j);
		}
	}
	vec2 BL = Corners[0], TR = Corners[0];
	for(int i = 1; i < CornerCount ; i++)
	{
		vec2 c = Corners[i];
		if(c.x < BL.x) BL.x = c.x;
		if(c.y < BL.y) BL.y = c.y;
		if(c.x > TR.x) TR.x = c.x;
		if(c.y > TR.y) TR.y = c.y;
	}
	Corners[CornerCount] = BL;
	Corners[CornerCount+1].x = 2*TR.x-BL.x;
	Corners[CornerCount+1].y = BL.y;
	Corners[CornerCount+2].x = BL.x;
	Corners[CornerCount+2].y = 2*TR.y-BL.y;

	m_Triangulation.m_Size = 0;
	// CTriangle triangle(Corners[CornerCount], Corners[CornerCount+1], Corners[CornerCount+2]);
	// m_Triangulation.m_pTriangles[m_Triangulation.m_Size++].m_Triangle = triangle;
	for (int i = 0; i < CornerCount - 2; i++)
	{
		for (int j = i + 1; j < CornerCount - 1; j++)
		{
			if(FastIntersectLine(Corners[i].x+Corners[i].y*m_Width,Corners[j].x+Corners[j].y*m_Width))
				continue;
			for (int k = j + 1; k < CornerCount; k++)
			{
				if(FastIntersectLine(Corners[i].x+Corners[i].y*m_Width,Corners[k].x+Corners[k].y*m_Width) || FastIntersectLine(Corners[j].x+Corners[j].y*m_Width,Corners[k].x+Corners[k].y*m_Width))
					continue;
				CTriangle triangle(Corners[i], Corners[j], Corners[k]);
				if(triangle.IsFlat())
					continue;
				vec2 cc = triangle.OuterCircleCenter();
				float radius = distance(Corners[i], cc);

				bool found = false;
				for(int w = 0 ; w < m_Triangulation.m_Size; w++)
				{
					if(triangle.Intersects(m_Triangulation.m_pTriangles[w].m_Triangle))
					{
						found = true;
						break;
					}
				}
				if (found)
					continue;
				for (int w = 0; w < CornerCount; w++)
				{
					if (w == i || w == j || w == k)
						continue;
					if (distance(cc, Corners[w]) < radius)
					{
						found = true;
						break;
					}
				}
				if (found)
					continue;

				m_Triangulation.m_pTriangles[m_Triangulation.m_Size].m_Triangle = triangle;

				m_Triangulation.m_pTriangles[m_Triangulation.m_Size].m_aSnapID[0] = GameServer()->Server()->SnapNewID(GameServer()->GetWorldID());
				m_Triangulation.m_pTriangles[m_Triangulation.m_Size].m_aSnapID[1] = GameServer()->Server()->SnapNewID(GameServer()->GetWorldID());
				m_Triangulation.m_pTriangles[m_Triangulation.m_Size].m_aSnapID[2] = GameServer()->Server()->SnapNewID(GameServer()->GetWorldID());
				m_Triangulation.m_Size++;
			}
		}
	}
	for (int i = 0; i < CornerCount - 2; i++)
	{
		for (int j = i + 1; j < CornerCount - 1; j++)
		{
			if(FastIntersectLine(Corners[i].x+Corners[i].y*m_Width,Corners[j].x+Corners[j].y*m_Width))
				continue;
			for (int k = j + 1; k < CornerCount; k++)
			{
				if(FastIntersectLine(Corners[i].x+Corners[i].y*m_Width,Corners[k].x+Corners[k].y*m_Width) || FastIntersectLine(Corners[j].x+Corners[j].y*m_Width,Corners[k].x+Corners[k].y*m_Width))
					continue;
				CTriangle triangle(Corners[i], Corners[j], Corners[k]);
				if(triangle.IsFlat())
					continue;

				bool found = false;
				for(int w = 0 ; w < m_Triangulation.m_Size; w++)
				{
					if(triangle.Intersects(m_Triangulation.m_pTriangles[w].m_Triangle) || (triangle.m_aPoints[0] == m_Triangulation.m_pTriangles[w].m_Triangle.m_aPoints[0] && triangle.m_aPoints[1] == m_Triangulation.m_pTriangles[w].m_Triangle.m_aPoints[1] && triangle.m_aPoints[2] == m_Triangulation.m_pTriangles[w].m_Triangle.m_aPoints[2]))
					{
						found = true;
						break;
					}
				}
				if (found)
					continue;
				for (int w = 0; w < CornerCount; w++)
				{
					if (w == i || w == j || w == k)
						continue;
					if (triangle.Inside(Corners[w]))
					{
						found = true;
						break;
					}
				}
				if (found)
					continue;

				m_Triangulation.m_pTriangles[m_Triangulation.m_Size].m_Triangle = triangle;

				m_Triangulation.m_pTriangles[m_Triangulation.m_Size].m_aSnapID[0] = GameServer()->Server()->SnapNewID(GameServer()->GetWorldID());
				m_Triangulation.m_pTriangles[m_Triangulation.m_Size].m_aSnapID[1] = GameServer()->Server()->SnapNewID(GameServer()->GetWorldID());
				m_Triangulation.m_pTriangles[m_Triangulation.m_Size].m_aSnapID[2] = GameServer()->Server()->SnapNewID(GameServer()->GetWorldID());
				m_Triangulation.m_Size++;
			}
		}
	}
	dbg_msg("botengine","Build %d triangles", m_Triangulation.m_Size);
	mem_free(Corners);
}

void CBotEngine::GenerateGraphFromTriangles()
{
	m_Graph.m_NumEdges = 0;
	for (int i = 0; i < m_Triangulation.m_Size - 1; i++)
	{
		for (int j = i + 1; j < m_Triangulation.m_Size; j++)
		{
			int count = 0;
			for(int k = 0 ; k < 3; k++)
				for(int l = 0; l < 3; l++)
					if(m_Triangulation.m_pTriangles[i].m_Triangle.m_aPoints[k] == m_Triangulation.m_pTriangles[j].m_Triangle.m_aPoints[l])
						count++;
			if(count == 2)
				m_Graph.m_NumEdges+=2;
		}
	}
	m_Graph.m_Width = m_Width;
	m_Graph.m_NumVertices = m_Triangulation.m_Size;
	m_Graph.m_pVertices = (CVertex*)mem_alloc(m_Graph.m_NumVertices*sizeof(CVertex));
	m_Graph.m_pEdges = (CEdge*)mem_alloc(m_Graph.m_NumEdges*sizeof(CEdge));
	for (int i = 0; i < m_Triangulation.m_Size; i++)
		m_Graph.m_pVertices[i].m_Pos = m_Triangulation.m_pTriangles[i].m_Triangle.Centroid()*32 + vec2(16,16);
	CEdge *pEdge = m_Graph.m_pEdges;
	for (int i = 0; i < m_Triangulation.m_Size - 1; i++)
	{
		for (int j = i + 1; j < m_Triangulation.m_Size; j++)
		{
			int count = 0;
			for(int k = 0 ; k < 3; k++)
				for(int l = 0; l < 3; l++)
					if(m_Triangulation.m_pTriangles[i].m_Triangle.m_aPoints[k] == m_Triangulation.m_pTriangles[j].m_Triangle.m_aPoints[l])
						count++;
			if(count == 2)
			{
				pEdge->m_Start = m_Graph.m_pVertices[i].m_Pos;
				pEdge->m_StartID = i;
				pEdge->m_End = m_Graph.m_pVertices[j].m_Pos;
				pEdge->m_EndID = j;
				pEdge->m_Size = 2;
				pEdge->m_SnapID = GameServer()->Server()->SnapNewID(GameServer()->GetWorldID());
				pEdge++;
				pEdge->m_Start = m_Graph.m_pVertices[j].m_Pos;
				pEdge->m_StartID = j;
				pEdge->m_End = m_Graph.m_pVertices[i].m_Pos;
				pEdge->m_EndID = i;
				pEdge->m_Size = 2;
				pEdge->m_SnapID = GameServer()->Server()->SnapNewID(GameServer()->GetWorldID());
				pEdge++;
			}
		}
	}
	dbg_msg("botengine","Create graph, %d vertices, %d edges", m_Graph.m_NumVertices, m_Graph.m_NumEdges);
	m_Graph.ComputeClosestPath();
}

int CBotEngine::GetTile(int x, int y)
{
	x = clamp(x,0,m_Width-1);
	y = clamp(y,0,m_Height-1);
	return m_pGrid[y*m_Width+x];
}

int CBotEngine::GetTile(vec2 Pos)
{
	return GetTile(round_to_int(Pos.x/32), round_to_int(Pos.y/32));
}

int CBotEngine::ConvertFromIndex(vec2 Pos)
{
	return clamp(round_to_int(Pos.x/32), 0, m_Width-1)+clamp(round_to_int(Pos.y/32), 0, m_Width-1)*m_Width;
}

int CBotEngine::FastIntersectLine(int Id1, int Id2)
{
	int i1 = Id1%m_Width;
	int i2 = Id2%m_Width;
	int j1 = Id1/m_Width;
	int j2 = Id2/m_Width;
	int di = abs(i2-i1);
	int dj = abs(j2-j1);
	int i = 0;
	int j = 0;
	int si = (i2>i1) ? 1: (i2<i1) ? -1 : 0;
	int sj = (j2>j1) ? 1: (j2<j1) ? -1 : 0;
	int Neigh[4] = {Id1 + si, Id2 - si, Id1 + sj*m_Width, Id2 - sj*m_Width};
	for(int k = 0; k < 4; k++)
		if((GetTile(Neigh[k]) & GTILE_MASK) > GTILE_AIR)
			return GetTile(Neigh[k]);
	while(i <= di && j <= dj)
	{
		if((GetTile(i*si+i1, j*sj+j1) & GTILE_MASK) > GTILE_AIR)
			return GetTile(i*si+i1, j*sj+j1);
		if(abs((j+1)*di-i*dj) < abs(j*di-(i+1)*dj))
			j++;
		else
			i++;
	}
	return 0;
}

void CBotEngine::GetPath(vec2 VStart, vec2 VEnd, CPath *pPath)
{
	if(!pPath || !pPath->m_pVertices || pPath->m_MaxSize < 3)
	{
		if(pPath)
			pPath->m_Size = 0;
		return;
	}
	const int InnerMax = maximum(1, pPath->m_MaxSize - 2);
	const int N = m_Graph.GetPath(GetClosestVertex(VStart), GetClosestVertex(VEnd), pPath->m_pVertices + 1, InnerMax);
	if(N <= 0 || N + 2 > pPath->m_MaxSize)
	{
		pPath->m_Size = 0;
		return;
	}
	pPath->m_pVertices[0] = VStart;
	pPath->m_pVertices[N + 1] = VEnd;
	pPath->m_Size = N + 2;
}

int CBotEngine::GetPartialPath(vec2 Pos, vec2 Target, vec2 *pVertices, int MaxSize)
{
	if(m_Graph.m_NumVertices <= 0 || !m_Graph.m_pClosestPath || !m_Graph.m_pVertices || MaxSize < 2)
		return 0;
	int id[2] = {0};
	vec2 aPos[] = {Pos,Target};
	for(int j = 0 ; j < 2 ; j++)
	{
		int d = 1000;
		vec2 pt = aPos[j] / 32;
		for(int k = 0; k < m_Triangulation.m_Size; k++)
		{
			if(m_Triangulation.m_pTriangles[k].m_Triangle.Inside(pt))
			{
				id[j] = k;
				break;
			}
			int dist = distance(m_Graph.m_pVertices[k].m_Pos,aPos[j]);
			if(dist < d)
			{
				d = dist;
				id[j] = k;
			}
		}
	}
	int n = m_Graph.m_pClosestPath[id[0]+id[1]*m_Graph.m_NumVertices];
	if( n < 0 )
		return 0;
	pVertices[0] = Pos;
	int Size = 1;
	while( Size < MaxSize-1 && id[0] != id[1])
	{
		pVertices[Size++] = m_Graph.m_pVertices[id[0]].m_Pos;
		id[0] = m_Graph.m_pClosestPath[id[0]+id[1]*m_Graph.m_NumVertices];
	}
	pVertices[Size++] = m_Graph.m_pVertices[id[0]].m_Pos;
	if(Size < MaxSize)
		pVertices[Size++] = Target;
	return Size;
}

vec2 CBotEngine::NextPoint(vec2 Pos, vec2 Target)
{
	if(m_Graph.m_NumVertices <= 0 || !m_Graph.m_pClosestPath || m_Triangulation.m_Size <= 0 || !m_Graph.m_pVertices)
		return Target;
	int id[2] = {0};
	vec2 aPos[] = {Pos,Target};
	for(int j = 0 ; j < 2 ; j++)
	{
		int d = 1000;
		vec2 pt = aPos[j] / 32;
		for(int k = 0; k < m_Triangulation.m_Size; k++)
		{
			if(m_Triangulation.m_pTriangles[k].m_Triangle.Inside(pt))
			{
				id[j] = k;
				break;
			}
			int dist = distance(m_Graph.m_pVertices[k].m_Pos,aPos[j]);
			if(dist < d)
			{
				d = dist;
				id[j] = k;
			}
		}
	}
	int n = m_Graph.m_pClosestPath[id[0]+id[1]*m_Graph.m_NumVertices];
	if( n < 0 )
		return Target;
	return m_Graph.m_pVertices[n].m_Pos;
}

int CBotEngine::DistanceToEdge(CEdge Edge, vec2 Pos)
{
	if(!Edge.m_Size)
		return -1;

	vec2 InterPos = closest_point_on_line(Edge.m_Start,Edge.m_End, Pos);
	return distance(InterPos,Pos);
}

// Need something smarter
int CBotEngine::FarestPointOnEdge(CPath *pPath, vec2 Pos, vec2 *pTarget)
{
	if(!pPath || !pPath->m_pVertices || pPath->m_Size <= 0)
		return -1;
	for(int k = pPath->m_Size-1 ; k >=0 ; k--)
	{
		int D = distance(Pos, pPath->m_pVertices[k]);
		if( D < 1000)
		{
			vec2 VertexPos = pPath->m_pVertices[k];
			vec2 W = direction(angle(normalize(VertexPos-Pos))+pi/2)*14.f;
			if(!(GameServer()->Collision()->FastIntersectLine(Pos-W,VertexPos-W,0,0)) && !(GameServer()->Collision()->FastIntersectLine(Pos+W,VertexPos+W,0,0)))
			{
				if(pTarget)
					*pTarget = VertexPos;
				return D;
			}
		}
	}
	return -1;
}

int CBotEngine::GetClosestEdge(vec2 Pos, int ClosestRange, CEdge *pEdge)
{
	CEdge* ClosestEdge = 0;

	for(int k = 0; k < m_Graph.m_NumEdges; k++)
	{
		CEdge *pE = m_Graph.m_pEdges + k;
		int D = DistanceToEdge(*pE, Pos);
		if(D < 0)
			continue;
		if(D < ClosestRange || ClosestRange < 0)
		{
			ClosestEdge = pE;
			ClosestRange = D;
		}
	}
	if(ClosestEdge)
	{
		if(pEdge)
		{
			*pEdge = *ClosestEdge;
		}
		return ClosestRange;
	}
	return -1;
}

int CBotEngine::GetClosestVertex(vec2 Pos)
{
	if(m_Triangulation.m_Size <= 0 || !m_Graph.m_pVertices)
		return 0;
	int i = 0;
	int d = 1000;
	vec2 pt = Pos / 32;
	for(int k = 0; k < m_Triangulation.m_Size; k++)
	{
		if(m_Triangulation.m_pTriangles[k].m_Triangle.Inside(pt))
			return k;
		int dist = distance(m_Graph.m_pVertices[k].m_Pos,Pos);
		if(dist < d)
		{
			d = dist;
			i = k;
		}
	}
	return i;
}

void CBotEngine::OnCharacterDeath(int Victim, int Killer, int Weapon)
{
	(void)Victim;
	(void)Killer;
	(void)Weapon;
}

void CBotEngine::RegisterBot(int CID, CBot *pBot)
{
	if(CID >= 0 && CID < MAX_CLIENTS)
		m_apBotPlaceholder[CID] = pBot;
}

void CBotEngine::UnRegisterBot(int CID)
{
	if(CID >= 0 && CID < MAX_CLIENTS)
		m_apBotPlaceholder[CID] = 0;
}


int CBotEngine::NetworkClipped(int SnappingClient, vec2 CheckPos)
{
	(void)SnappingClient;
	(void)CheckPos;
	return 0;
}