#include "rpg_fishing.h"

#include <engine/input_events.h>

#include <game/server/core/attribute_types.h>
#include <game/server/core/components/mmo/mmo_item.h>
#include <game/server/core/components/meta/mini_events_manager.h>
#include <game/server/core/mmo_context.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/entities/character.h>
#include <game/server/entities/skills/skill_vfx_common.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <generated/protocol.h>
#include <generated/server_data.h>

namespace
{
float TranslateToPercent(float From, float Value)
{
	if(From <= 0.f)
		return 0.f;
	return Value / From * 100.f;
}

bool PlayerHasFishRod(CPlayer *pPlayer, int &RodItemId)
{
	RodItemId = pPlayer->m_EquippedSlots.getSlot(ItemType::EquipFishrod);
	if(RodItemId > 0)
		return true;

	for(const CMMOItem &Item : pPlayer->m_MMOInventory)
	{
		if(const CMMOItemDescription *pDesc = CMMOItemDescription::Get(Item.m_ItemID);
			pDesc && pDesc->GetType() == ItemType::EquipFishrod)
		{
			RodItemId = Item.m_ItemID;
			return true;
		}
	}
	return false;
}

bool PlayerHasFishBait(const CPlayer *pPlayer)
{
	for(const CMMOItem &Item : pPlayer->m_MMOInventory)
	{
		if(Item.m_ItemID == 208 && Item.m_Count > 0)
			return true;
	}
	return false;
}

void BuildProgressBar(char *pBuf, int Size, int Done, int Total, int Width = 10)
{
	if(Size <= 0)
		return;
	pBuf[0] = '\0';
	if(Total <= 0 || Width <= 0)
		return;

	const int Filled = minimum(Width, maximum(0, (Done * Width) / Total));
	int Pos = 0;
	for(int i = 0; i < Filled && Pos + 4 < Size; ++i)
	{
		pBuf[Pos++] = '\xE2';
		pBuf[Pos++] = '\x96';
		pBuf[Pos++] = '\xB0';
	}
	for(int i = Filled; i < Width && Pos + 4 < Size; ++i)
	{
		pBuf[Pos++] = '\xE2';
		pBuf[Pos++] = '\x96';
		pBuf[Pos++] = '\xB1';
	}
	pBuf[Pos] = '\0';
}

bool ShouldSendHud(int Now, int &LastHudTick, int Interval)
{
	if(LastHudTick <= 0 || Now - LastHudTick >= Interval)
	{
		LastHudTick = Now;
		return true;
	}
	return false;
}

bool ShouldSendWarn(int Now, int &LastWarnTick, int Interval)
{
	if(LastWarnTick <= 0 || Now - LastWarnTick >= Interval)
	{
		LastWarnTick = Now;
		return true;
	}
	return false;
}
} // namespace

CEntityFishingRod::CEntityFishingRod(CGameWorld *pGameWorld, int ClientID, vec2 Position, vec2 Force, bool AutoMode)
	: CEntity(pGameWorld, CGameWorld::ENTTYPE_RPG_FISHING, 0, Position, 0)
{
	m_OwnerClientId = ClientID;
	m_EndRodPoint = Position;
	m_Rope.Init(NUM_ROPE_POINTS, Position, Force);
	m_Fishing.m_State = FishingNow::WAITING;
	m_Fishing.m_HookingTime = Server()->TickSpeed() * (5 + random_int() % 14);
	m_FloatInWater = false;
	m_AutoMode = AutoMode;
	m_LastAutoPullTick = 0;
	m_CachedRodItemId = -1;
	m_CachedSwitchNumber = -1;
	m_pCachedFishNode = nullptr;
	m_PhysicsTick = 0;

	AddSnappingGroupIds(SNAP_ROD, NUM_ROD_POINTS);
	AddSnappingGroupIds(SNAP_ROPE, NUM_ROPE_POINTS);
	GameWorld()->InsertEntity(this);

	if(CCharacter *pChar = GameServer()->GetPlayerChar(m_OwnerClientId))
		pChar->m_pFishingRod = this;
}

CEntityFishingRod::~CEntityFishingRod()
{
	if(CCharacter *pChar = GameServer()->GetPlayerChar(m_OwnerClientId))
		pChar->m_pFishingRod = nullptr;
}

vec2 CEntityFishingRod::CalculateRodPoint(bool FacingRight, size_t Segment) const
{
	static const std::pair<vec2, vec2> s_aRodPositions[] = {
		{vec2(0, 0), vec2(100, -60)},
		{vec2(100, -60), vec2(140, -50)},
		{vec2(140, -50), vec2(160, -40)},
	};

	if(Segment >= NUM_ROD_POINTS)
		return m_Pos;

	const vec2 &To = s_aRodPositions[Segment].second;
	return FacingRight ? vec2(m_Pos.x + To.x, m_Pos.y + To.y) : vec2(m_Pos.x - To.x, m_Pos.y + To.y);
}

void CEntityFishingRod::UpdateRodEndPoint(const CCharacter *pChar)
{
	if(!pChar)
		return;

	const bool FacingRight = pChar->LatestInput().m_TargetX > 0.f;
	m_EndRodPoint = CalculateRodPoint(FacingRight, NUM_ROD_POINTS - 1);
}

void CEntityFishingRod::Tick()
{
	CCharacter *pChar = GameServer()->GetPlayerChar(m_OwnerClientId);
	if(!pChar || !pChar->IsAlive() || m_Rope.m_vPoints.size() < 2)
	{
		MarkForDestroy();
		return;
	}

	const float DistanceBetween = distance(m_Rope.m_vPoints.front(), m_Rope.m_vPoints.back());
	if(DistanceBetween > 800.f || m_Fishing.m_State == FishingNow::SUCCESS)
	{
		MarkForDestroy();
		return;
	}

	if(m_Fishing.m_State == FishingNow::AWAY)
	{
		GameServer()->SendChatLoc(m_OwnerClientId, "rpg_fish.away", "鱼溜走了！");
		MarkForDestroy();
		return;
	}

	CPlayer *pPlayer = pChar->GetPlayer();
	if(!pPlayer)
	{
		MarkForDestroy();
		return;
	}

	int RodItemId = m_CachedRodItemId;
	if(RodItemId <= 0 && !PlayerHasFishRod(pPlayer, RodItemId))
	{
		GameServer()->SendChatLoc(m_OwnerClientId, "rpg_fish.need_rod", "需要装备鱼竿才能钓鱼。");
		MarkForDestroy();
		return;
	}
	m_CachedRodItemId = RodItemId;

	const CMMOItemDescription *pRodInfo = CMMOItemDescription::Get(RodItemId > 0 ? RodItemId : 210);
	const char *pRodName = pRodInfo ? pRodInfo->GetName() : "鱼竿";

	m_Pos = pChar->GetPos();
	UpdateRodEndPoint(pChar);

	const bool HeavyPhysics = m_Fishing.m_State == FishingNow::HOOKING || m_Fishing.m_State == FishingNow::PULLING;
	if(HeavyPhysics || (++m_PhysicsTick % 2) == 0)
	{
		m_Rope.UpdatePhysics(GameServer()->Collision(), 3.0f, 16.f, 64.f);
		m_Rope.m_vPoints[0] = m_EndRodPoint;
	}
	else
	{
		m_Rope.m_vPoints[0] = m_EndRodPoint;
	}

	const vec2 &LastPoint = m_Rope.m_vPoints.back();
	const vec2 TestBox = vec2(LastPoint.x, LastPoint.y + 18.f);
	const bool InWater = (GameServer()->Collision()->GetCollisionFlagsAt(TestBox) & CCollision::COLFLAG_WATER) != 0;
	const int Now = Server()->Tick();
	const int WarnInterval = Server()->TickSpeed() * 2;

	if(!InWater)
	{
		if(m_Fishing.m_State == FishingNow::PULLING)
			m_Fishing.m_State = FishingNow::AWAY;
		m_FloatInWater = false;
		if(ShouldSendWarn(Now, m_LastWarnTick, WarnInterval))
			GameServer()->SendBroadcastLoc(m_OwnerClientId, "rpg_fish.no_water", "浮标需要落入水中才能钓鱼。");
		return;
	}

	if(!m_FloatInWater)
	{
		GameServer()->m_World.CreateSound(TestBox, SOUND_SFX_WATER);
		m_FloatInWater = true;
		GameServer()->SendChatLoc(m_OwnerClientId, "rpg_fish.cast", "浮标落水，等待鱼上钩…");
	}

	const int SwitchNumber = GameServer()->Collision()->GetSwitchNumber(TestBox);
	if(SwitchNumber != m_CachedSwitchNumber)
	{
		m_CachedSwitchNumber = SwitchNumber;
		m_pCachedFishNode = GameServer()->Collision()->GetFishNode(SwitchNumber);
	}
	if(!m_pCachedFishNode || m_pCachedFishNode->m_vItems.empty())
	{
		if(ShouldSendWarn(Now, m_LastWarnTick, WarnInterval))
			GameServer()->SendBroadcastLoc(m_OwnerClientId, "rpg_fish.no_fish", "这片水域没有鱼。");
		return;
	}

	GatheringNode *pNode = m_pCachedFishNode;

	const int PlayerLevel = maximum(1, pPlayer->GetStat(AttributeIdentifier::Level));
	if(PlayerLevel < pNode->Level)
	{
		if(ShouldSendWarn(Now, m_LastWarnTick, WarnInterval))
		{
			GameServer()->SendBroadcastLocF(m_OwnerClientId, "rpg_fish.level_low", "等级不足 — 需要 Lv%d 才能钓「%s」。",
				pNode->Level, pNode->Name.c_str());
		}
		return;
	}

	FishingTick(pPlayer, pNode, pRodName);
}

void CEntityFishingRod::FishingTick(CPlayer *pPlayer, GatheringNode *pNode, const char *pRodName)
{
	const int Now = Server()->Tick();
	const int HudInterval = Server()->TickSpeed();
	const int MaxHealth = maximum(1, pNode->Health);

	m_Fishing.m_HookingTime--;

	if(m_Fishing.m_HookingTime <= 0)
	{
		if(m_Fishing.m_State == FishingNow::HOOKING)
		{
			const int HookingNextTime = PlayerHasFishBait(pPlayer) ? 10 : 15;
			m_Fishing.m_State = FishingNow::WAITING;
			m_Fishing.m_HookingTime = Server()->TickSpeed() * (3 + HookingNextTime);
			GameServer()->SendChatLoc(m_OwnerClientId, "rpg_fish.missed", "没按住收线，鱼跑了。");
		}
		else
		{
			m_Fishing.m_State = FishingNow::AWAY;
		}
		return;
	}

	if(m_PrevFishingState != m_Fishing.m_State)
	{
		if(m_Fishing.m_State == FishingNow::HOOKING)
		{
			GameServer()->SendChatLoc(m_OwnerClientId, "rpg_fish.bite", "有鱼咬钩！快按 Fire 收线！");
			GameServer()->m_World.CreateSound(m_Rope.m_vPoints.back(), SOUND_SFX_WATER);
		}
		else if(m_Fishing.m_State == FishingNow::PULLING)
		{
			GameServer()->SendChatLocF(m_OwnerClientId, "rpg_fish.hooked", "成功挂钩「%s」— 连按 Fire 把鱼拉上来！", pNode->Name.c_str());
		}
		m_PrevFishingState = m_Fishing.m_State;
		m_LastHudTick = 0;
	}

	if(m_Fishing.m_State == FishingNow::WAITING)
	{
		if(m_Fishing.m_HookingTime <= Server()->TickSpeed() * 3 + 1)
		{
			m_Fishing.m_State = FishingNow::HOOKING;
			m_LastAutoPullTick = Now;
		}

		if(ShouldSendHud(Now, m_LastHudTick, HudInterval))
		{
			const int SecLeft = maximum(0, m_Fishing.m_HookingTime / Server()->TickSpeed());
			GameServer()->SendBroadcastLocF(m_OwnerClientId, "rpg_fish.waiting", "等待「%s」上钩 | 鱼竿: %s | %s | ~%ds",
				pNode->Name.c_str(), pRodName, m_AutoMode ? "自动" : "手动", SecLeft);
		}
		return;
	}

	if(m_Fishing.m_State == FishingNow::HOOKING)
	{
		if(m_Fishing.m_HookingTime % Server()->TickSpeed() == 0)
		{
			GameWorld()->CreateDeath(m_Rope.m_vPoints.back(), m_OwnerClientId);
			m_Rope.SetForce(vec2(0.f, 5.f));
		}

		Server()->Input()->BlockInputGroup(m_OwnerClientId, BLOCK_INPUT_FIRE);
		const bool ManualPull = !m_AutoMode && Server()->Input()->IsKeyClicked(m_OwnerClientId, KEY_EVENT_FIRE);
		const bool AutoPull = m_AutoMode && (Now - m_LastAutoPullTick) >= (Server()->TickSpeed() - 1);
		if(ManualPull || AutoPull)
		{
			m_Fishing.m_State = FishingNow::PULLING;
			m_Fishing.m_Health = MaxHealth;
			m_Fishing.m_HookingTime = Server()->TickSpeed() * 3;
			m_Fishing.m_FromPoint = std::nullopt;
			m_LastAutoPullTick = Now;
		}

		if(ShouldSendHud(Now, m_LastHudTick, HudInterval / 2))
		{
			const int Sec = maximum(1, m_Fishing.m_HookingTime / Server()->TickSpeed());
			GameServer()->SendBroadcastLocF(m_OwnerClientId, "rpg_fish.hooking", ">>> 收线! <<< Fire 挂钩 [%ds]", Sec);
		}
		return;
	}

	if(m_Fishing.m_State == FishingNow::PULLING)
	{
		if(!m_Fishing.m_FromPoint)
		{
			m_Fishing.m_FromPoint = m_Rope.m_vPoints.back();
			m_Fishing.m_InterpolatedX = m_Fishing.m_FromPoint->x;
		}

		Server()->Input()->BlockInputGroup(m_OwnerClientId, BLOCK_INPUT_FIRE);
		const bool ManualPull = !m_AutoMode && Server()->Input()->IsKeyClicked(m_OwnerClientId, KEY_EVENT_FIRE);
		const bool AutoPull = m_AutoMode && (Now - m_LastAutoPullTick) >= (Server()->TickSpeed() - 1);
		if(ManualPull || AutoPull)
		{
			const int Damage = maximum(1, pPlayer->GetStat(AttributeIdentifier::Patience));
			m_Fishing.m_Health = maximum(m_Fishing.m_Health - Damage, 0);
			m_LastAutoPullTick = Now;

			const int TotalDamage = MaxHealth - m_Fishing.m_Health;
			const float PercentDmg = TranslateToPercent((float)MaxHealth, (float)TotalDamage);
			m_Fishing.m_InterpolatedX = m_Fishing.m_FromPoint->x + (m_EndRodPoint.x - m_Fishing.m_FromPoint->x) * (PercentDmg / 100.f);

			constexpr int IGNORE_POINTS = 3;
			float PercentHP = TranslateToPercent((float)MaxHealth, (float)m_Fishing.m_Health);
			float PercentPoints = TranslateToPercent((float)NUM_ROPE_POINTS, (float)m_Rope.m_vPoints.size());
			while(PercentPoints > PercentHP && (int)m_Rope.m_vPoints.size() > IGNORE_POINTS)
			{
				m_Rope.m_vPoints.erase(m_Rope.m_vPoints.begin() + 1);
				PercentPoints = TranslateToPercent((float)NUM_ROPE_POINTS, (float)m_Rope.m_vPoints.size());
			}

			GameWorld()->CreateHammerHit(m_Rope.m_vPoints.back());
		}

		if(!m_Rope.m_vPoints.empty())
		{
			vec2 &LastPointRef = m_Rope.m_vPoints.back();
			if(LastPointRef.x != m_Fishing.m_InterpolatedX)
				LastPointRef.x += (m_Fishing.m_InterpolatedX - LastPointRef.x) * 0.1f;
		}

		if(m_Fishing.m_Health <= 0)
		{
			const int ItemId = pNode->m_vItems.PickRandomItem();
			int Amount = 1 + random_int() % 2;
			if(TWorldController *pCore = GameServer()->Core())
			{
				if(pCore->MiniEventsManager())
				{
					const int Bonus = pCore->MiniEventsManager()->GetLootBonusPercent();
					if(Bonus > 0)
						Amount = maximum(1, Amount + Amount * Bonus / 100);
				}
			}

			if(ItemId >= 0)
			{
				pPlayer->m_MMOInventory.Add(ItemId, Amount, 0);
				pPlayer->m_MMODirty = true;

				const CMMOItemDescription *pDesc = CMMOItemDescription::Get(ItemId);
				const char *pName = pDesc ? pDesc->GetName() : "?";
				GameServer()->SendChatLocF(m_OwnerClientId, "rpg_fish.pickup", "获得 %d × %s", Amount, pName);
			}

			if(CCharacter *pChr = GameServer()->GetPlayerChar(m_OwnerClientId))
				pChr->SetEmote(EMOTE_HAPPY, Now + Server()->TickSpeed());
			GameWorld()->CreateHammerHit(m_EndRodPoint);
			GameServer()->m_World.CreateSound(m_EndRodPoint, SOUND_SFX_WATER);

			m_Rope.m_vPoints.back() = m_EndRodPoint;
			m_Fishing.m_State = FishingNow::SUCCESS;
			return;
		}

		if(ShouldSendHud(Now, m_LastHudTick, HudInterval / 2))
		{
			char aBar[32];
			const int Done = MaxHealth - m_Fishing.m_Health;
			BuildProgressBar(aBar, sizeof(aBar), Done, MaxHealth);
			const int Sec = maximum(0, m_Fishing.m_HookingTime / Server()->TickSpeed());
			GameServer()->SendBroadcastLocF(m_OwnerClientId, "rpg_fish.pulling", "「%s」 %s  %d/%d  [%ds] | %s",
				pNode->Name.c_str(), aBar, m_Fishing.m_Health, MaxHealth, Sec, pRodName);
		}
	}
}

void CEntityFishingRod::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CCharacter *pChar = GameServer()->GetPlayerChar(m_OwnerClientId);
	if(!pChar)
		return;

	const array<int> *pvRodIds = FindSnappingGroupIds(SNAP_ROD);
	if(!pvRodIds)
		return;

	const int CurTick = Server()->Tick();
	const bool FacingRight = pChar->LatestInput().m_TargetX > 0.f;

	const int NumRodSegs = minimum((int)pvRodIds->size(), (int)NUM_ROD_POINTS);
	for(int i = 0; i < NumRodSegs; ++i)
	{
		const vec2 From = (i == 0) ? m_Pos : CalculateRodPoint(FacingRight, (size_t)(i - 1));
		const vec2 To = CalculateRodPoint(FacingRight, (size_t)i);
		SnapLaserSegment(Server(), (*pvRodIds)[i], From, To, CurTick - 2);
	}

	const array<int> *pvRopeIds = FindSnappingGroupIds(SNAP_ROPE);
	if(!pvRopeIds)
		return;

	const size_t RopePointCount = m_Rope.m_vPoints.size();
	if(RopePointCount >= 2)
	{
		for(size_t i = 0; i + 1 < RopePointCount; ++i)
		{
			if((int)i >= pvRopeIds->size())
				break;
			SnapLaserSegment(Server(), (*pvRopeIds)[i], m_Rope.m_vPoints[i], m_Rope.m_vPoints[i + 1], CurTick - 6);
		}

		CNetObj_Pickup *pP = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, GetID(), sizeof(CNetObj_Pickup)));
		if(pP)
		{
			pP->m_X = round_to_int(m_Rope.m_vPoints.back().x);
			pP->m_Y = round_to_int(m_Rope.m_vPoints.back().y);
			pP->m_Type = PICKUP_HEALTH;
		}
	}
}
