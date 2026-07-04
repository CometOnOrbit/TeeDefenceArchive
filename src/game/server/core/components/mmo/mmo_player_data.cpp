#include "mmo_manager.h"
#include <game/server/data_center.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/core/tworld_controller.h>
#include <base/system.h>
#include <engine/shared/config.h>
#include <engine/shared/jsonparser.h>
#include <game/server/core/components/vote/vote_menu_manager.h>
#include <game/server/account.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/core/components/worlds/world_manager.h>
#include <game/server/sql_query.h>
#include <game/server/sql_pool.h>
#include <game/server/sql_wrapper.h>
#include <mysql.h>
#include <game/server/entities/character.h>
#include <game/server/core/components/mmo/mmo_item.h>
#include <game/server/global_state.h>

void CMMOManager::OnPlayerLogin(CPlayer *pPlayer)
{
	if(!pPlayer || pPlayer->GetAccountId() <= 0) return;

	// Load friend list
	LoadFriends(pPlayer);

	pPlayer->SetStat(AttributeIdentifier::Level, 1);
	pPlayer->SetStat(AttributeIdentifier::Experience, 0);
	pPlayer->SetStat(AttributeIdentifier::Gold, 0);
	pPlayer->SetStat(AttributeIdentifier::SkillPoints, 0);
	// TRPG六维初始值 (3 表示普通人类平均值)
	pPlayer->SetStat(AttributeIdentifier::STR, 3);
	pPlayer->SetStat(AttributeIdentifier::DEX, 3);
	pPlayer->SetStat(AttributeIdentifier::CON, 3);
	pPlayer->SetStat(AttributeIdentifier::INT, 3);
	pPlayer->SetStat(AttributeIdentifier::WIS, 3);
	pPlayer->SetStat(AttributeIdentifier::CHA, 3);
	pPlayer->m_MMODirty = false;
	pPlayer->SetStat(AttributeIdentifier::Reputation, 0);
	pPlayer->m_MMOInventory.clear();
	pPlayer->InitWeaponLoadouts();
	pPlayer->SyncStatsFromFields();

	if(!LoadPlayerData(pPlayer))
		dbg_msg("mmo", "Player #%d (UID=%lld): new MMO player", pPlayer->GetCID(), (long long)pPlayer->GetAccountId());

	if(!LoadInventory(pPlayer))
		dbg_msg("mmo", "Player #%d: no MMO inventory", pPlayer->GetCID());

	// Notify online friends
	CGlobalState::NotifyFriendsOnline(GS(), pPlayer);

	// Load daily checkin data
	LoadCheckinData(pPlayer);

	// Load daily recycle/sell limits
	LoadSellData(pPlayer);

	// Load pet data
	LoadPetData(pPlayer);

	// Load house data
	LoadHouseData(pPlayer);

	// Load marriage data
	LoadMarriageData(pPlayer);

	dbg_msg("mmo", "Player #%d login: Lv.%d, %lld exp, %d gold, %d items",
		pPlayer->GetCID(), pPlayer->GetStat(AttributeIdentifier::Level), (long long)pPlayer->GetStat(AttributeIdentifier::Experience),
		pPlayer->GetStat(AttributeIdentifier::Gold), (int)pPlayer->m_MMOInventory.size());
}

void CMMOManager::OnClientReset(int ClientID)
{
	CPlayer *pPlayer = GS()->m_apPlayers[ClientID];
	if(!pPlayer || pPlayer->GetAccountId() <= 0) return;

	// Leave group
	GroupLeave(ClientID);

	// Cancel any active trade
	TradeCancel(ClientID);

	// Save friends if dirty
	if(pPlayer->m_aFriendsDirty)
	{
		SaveFriends(pPlayer);
		pPlayer->m_aFriendsDirty = false;
	}

	// Save daily checkin data
	SaveCheckinData(pPlayer);

	// Save daily recycle/sell limits
	SaveSellData(pPlayer);

	// Notify online friends of disconnect
	CGlobalState::NotifyFriendsOffline(GS(), pPlayer);

	// Save pet data on disconnect
	SavePetData(pPlayer);

	// Save house data on disconnect
	if(pPlayer->m_HasHouse)
		SaveHouseData(pPlayer);

	// Save on disconnect
	if(pPlayer->m_MMODirty)
	{
		SavePlayerData(pPlayer);
		SaveInventory(pPlayer);
		pPlayer->m_MMODirty = false;
		dbg_msg("mmo", "Player #%d: saved on disconnect", ClientID);
	}
}

bool CMMOManager::LoadPlayerData(CPlayer *pPlayer)
{
	if(!pPlayer || pPlayer->GetAccountId() <= 0) return false;
	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized()) return false;
	void *pRaw = pPool->Acquire();
	if(!pRaw) return false;
	MYSQL *pSql = (MYSQL *)pRaw;
	int64 UserId = pPlayer->GetAccountId();

	char aQuery[512];
	str_format(aQuery, sizeof(aQuery),
		"SELECT `Level`, `Experience`, `Gold`, `SkillPoints`, "
		"`STR`, `DEX`, `CON`, `INT`, `WIS`, `CHA`, "
		"`SkillSlot1`, `SkillSlot2`, `SkillSlot3`, "
		"`ItemSlot0`, `ItemSlot1`, `ItemSlot2`, `ItemSlot3`, "
		"`StoryData`, "
		"`FashionItemID`, "
		"`MeleeLoadout0`, `MeleeLoadout1`, `MeleeLoadout2`, `MeleeLoadout3`, "
		"`RangedLoadout0`, `RangedLoadout1`, `RangedLoadout2`, `RangedLoadout3`, "
		"`WeaponBar0`, `WeaponBar1`, `WeaponBar2`, `WeaponBar3` "
		"FROM `tw_mmo_players` WHERE `UserID`=%lld LIMIT 1", (long long)UserId);
	if(!SqlExecQuery(pSql, GS()->Config(), aQuery))
	{ pPool->Release(pRaw); return false; }

	MYSQL_RES *pRes = mysql_store_result(pSql);
	if(!pRes) { pPool->Release(pRaw); return false; }

	MYSQL_ROW Row = mysql_fetch_row(pRes);
	if(!Row)
	{
		mysql_free_result(pRes);
		// Create new player row
		str_format(aQuery, sizeof(aQuery),
			"INSERT INTO `tw_mmo_players` (`UserID`) VALUES (%lld)", (long long)UserId);
		SqlExecQuery(pSql, GS()->Config(), aQuery);
		pPool->Release(pRaw);
		return true;
	}

	pPlayer->SetStat(AttributeIdentifier::Level, str_toint(Row[0]));
	pPlayer->SetStat(AttributeIdentifier::Experience, str_toint(Row[1]));
	pPlayer->SetStat(AttributeIdentifier::Gold, str_toint(Row[2]));
	pPlayer->SetStat(AttributeIdentifier::SkillPoints, str_toint(Row[3]));
	pPlayer->SetStat(AttributeIdentifier::STR, Row[4] ? str_toint(Row[4]) : 3);
	pPlayer->SetStat(AttributeIdentifier::DEX, Row[5] ? str_toint(Row[5]) : 3);
	pPlayer->SetStat(AttributeIdentifier::CON, Row[6] ? str_toint(Row[6]) : 3);
	pPlayer->SetStat(AttributeIdentifier::INT, Row[7] ? str_toint(Row[7]) : 3);
	pPlayer->SetStat(AttributeIdentifier::WIS, Row[8] ? str_toint(Row[8]) : 3);
	pPlayer->SetStat(AttributeIdentifier::CHA, Row[9] ? str_toint(Row[9]) : 3);
	// Load skill slot bindings (columns 10/11/12)
	pPlayer->m_aSkillSlots[0] = Row[10] ? str_toint(Row[10]) : -1;
	pPlayer->m_aSkillSlots[1] = Row[11] ? str_toint(Row[11]) : -1;
	pPlayer->m_aSkillSlots[2] = Row[12] ? str_toint(Row[12]) : -1;
	// Load item quick slot bindings (columns 13/14/15/16)
	pPlayer->m_aItemQuickSlots[0] = Row[13] ? str_toint(Row[13]) : -1;
	pPlayer->m_aItemQuickSlots[1] = Row[14] ? str_toint(Row[14]) : -1;
	pPlayer->m_aItemQuickSlots[2] = Row[15] ? str_toint(Row[15]) : -1;
	pPlayer->m_aItemQuickSlots[3] = Row[16] ? str_toint(Row[16]) : -1;
	// Load story flags from StoryData column (index 17)
	if(Row[17] && Row[17][0])
	{
		const char *pData = Row[17];
		// Parse key=value,key=value format
		char aBuf[2048];
		str_copy(aBuf, pData, sizeof(aBuf));
		char *pTok = strtok(aBuf, ",");
		while(pTok)
		{
			const char *pEqC = str_find(pTok, "=");
			if(pEqC)
			{
				char *pEq = const_cast<char *>(pEqC);
				*pEq = '\0';
				pPlayer->SetStoryFlag(pTok, str_toint(pEq + 1));
			}
			pTok = strtok(nullptr, ",");
		}
	}
	// Load fashion item ID (column index 18)
	pPlayer->m_FashionItemID = Row[18] ? str_toint(Row[18]) : 0;
	// Load weapon loadouts + bar (columns 19-30)
	for(int i = 0; i < CPlayer::MMO_WEAPON_LOADOUT_SIZE; i++)
	{
		pPlayer->m_aMeleeLoadout[i] = Row[19 + i] ? str_toint(Row[19 + i]) : -1;
		pPlayer->m_aRangedLoadout[i] = Row[23 + i] ? str_toint(Row[23 + i]) : -1;
		pPlayer->m_aWeaponBar[i] = Row[27 + i] ? str_toint(Row[27 + i]) : -1;
	}
	// EquipWeaponSlot migration is handled in LoadInventory() after inventory items are loaded
	pPlayer->SyncStatsFromFields();
	mysql_free_result(pRes);
	pPool->Release(pRaw);
	return true;
}

bool CMMOManager::SavePlayerData(CPlayer *pPlayer)
{
	if(!pPlayer || pPlayer->GetAccountId() <= 0) return false;
	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized()) return false;
	void *pRaw = pPool->Acquire();
	if(!pRaw) return false;
	MYSQL *pSql = (MYSQL *)pRaw;
	int64 UserId = pPlayer->GetAccountId();

	char aStoryBuf[1024] = {0};
	int StoryPos = 0;
	for(auto it = pPlayer->m_StoryFlags.begin(); it != pPlayer->m_StoryFlags.end(); ++it)
	{
		int Need = str_length(it->first.c_str()) + 16;
		if(StoryPos + Need >= (int)sizeof(aStoryBuf))
			break;
		str_format(aStoryBuf + StoryPos, sizeof(aStoryBuf) - StoryPos, "%s=%d,", it->first.c_str(), it->second);
		StoryPos = str_length(aStoryBuf);
	}

	char aQuery[8192];
	str_format(aQuery, sizeof(aQuery),
		"INSERT INTO `tw_mmo_players` (`UserID`, `Level`, `Experience`, `Gold`, `SkillPoints`, "
		"`STR`, `DEX`, `CON`, `INT`, `WIS`, `CHA`, "
		"`SkillSlot1`, `SkillSlot2`, `SkillSlot3`, "
		"`ItemSlot0`, `ItemSlot1`, `ItemSlot2`, `ItemSlot3`, `StoryData`, "
		"`FashionItemID`, "
		"`MeleeLoadout0`, `MeleeLoadout1`, `MeleeLoadout2`, `MeleeLoadout3`, "
		"`RangedLoadout0`, `RangedLoadout1`, `RangedLoadout2`, `RangedLoadout3`, "
		"`WeaponBar0`, `WeaponBar1`, `WeaponBar2`, `WeaponBar3`) "
		"VALUES (%lld, %d, %lld, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, '%s', %d, "
		"%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d) "
		"ON DUPLICATE KEY UPDATE "
		"`Level`=%d, `Experience`=%lld, `Gold`=%d, `SkillPoints`=%d, "
		"`STR`=%d, `DEX`=%d, `CON`=%d, `INT`=%d, `WIS`=%d, `CHA`=%d, "
		"`SkillSlot1`=%d, `SkillSlot2`=%d, `SkillSlot3`=%d, "
		"`ItemSlot0`=%d, `ItemSlot1`=%d, `ItemSlot2`=%d, `ItemSlot3`=%d, "
		"`StoryData`='%s', "
		"`FashionItemID`=%d, "
		"`MeleeLoadout0`=%d, `MeleeLoadout1`=%d, `MeleeLoadout2`=%d, `MeleeLoadout3`=%d, "
		"`RangedLoadout0`=%d, `RangedLoadout1`=%d, `RangedLoadout2`=%d, `RangedLoadout3`=%d, "
		"`WeaponBar0`=%d, `WeaponBar1`=%d, `WeaponBar2`=%d, `WeaponBar3`=%d",
		(long long)UserId,
		pPlayer->GetStat(AttributeIdentifier::Level), (long long)pPlayer->GetStat(AttributeIdentifier::Experience), pPlayer->GetStat(AttributeIdentifier::Gold),
		pPlayer->GetStat(AttributeIdentifier::SkillPoints),
		pPlayer->GetStat(AttributeIdentifier::STR), pPlayer->GetStat(AttributeIdentifier::DEX),
		pPlayer->GetStat(AttributeIdentifier::CON), pPlayer->GetStat(AttributeIdentifier::INT),
		pPlayer->GetStat(AttributeIdentifier::WIS), pPlayer->GetStat(AttributeIdentifier::CHA),
		pPlayer->m_aSkillSlots[0], pPlayer->m_aSkillSlots[1], pPlayer->m_aSkillSlots[2],
		pPlayer->m_aItemQuickSlots[0], pPlayer->m_aItemQuickSlots[1], pPlayer->m_aItemQuickSlots[2], pPlayer->m_aItemQuickSlots[3],
		aStoryBuf,
		pPlayer->m_FashionItemID,
		pPlayer->m_aMeleeLoadout[0], pPlayer->m_aMeleeLoadout[1], pPlayer->m_aMeleeLoadout[2], pPlayer->m_aMeleeLoadout[3],
		pPlayer->m_aRangedLoadout[0], pPlayer->m_aRangedLoadout[1], pPlayer->m_aRangedLoadout[2], pPlayer->m_aRangedLoadout[3],
		pPlayer->m_aWeaponBar[0], pPlayer->m_aWeaponBar[1], pPlayer->m_aWeaponBar[2], pPlayer->m_aWeaponBar[3],
		pPlayer->GetStat(AttributeIdentifier::Level), (long long)pPlayer->GetStat(AttributeIdentifier::Experience), pPlayer->GetStat(AttributeIdentifier::Gold),
		pPlayer->GetStat(AttributeIdentifier::SkillPoints),
		pPlayer->GetStat(AttributeIdentifier::STR), pPlayer->GetStat(AttributeIdentifier::DEX),
		pPlayer->GetStat(AttributeIdentifier::CON), pPlayer->GetStat(AttributeIdentifier::INT),
		pPlayer->GetStat(AttributeIdentifier::WIS), pPlayer->GetStat(AttributeIdentifier::CHA),
		pPlayer->m_aSkillSlots[0], pPlayer->m_aSkillSlots[1], pPlayer->m_aSkillSlots[2],
		pPlayer->m_aItemQuickSlots[0], pPlayer->m_aItemQuickSlots[1], pPlayer->m_aItemQuickSlots[2], pPlayer->m_aItemQuickSlots[3],
		aStoryBuf,
		pPlayer->m_FashionItemID,
		pPlayer->m_aMeleeLoadout[0], pPlayer->m_aMeleeLoadout[1], pPlayer->m_aMeleeLoadout[2], pPlayer->m_aMeleeLoadout[3],
		pPlayer->m_aRangedLoadout[0], pPlayer->m_aRangedLoadout[1], pPlayer->m_aRangedLoadout[2], pPlayer->m_aRangedLoadout[3],
		pPlayer->m_aWeaponBar[0], pPlayer->m_aWeaponBar[1], pPlayer->m_aWeaponBar[2], pPlayer->m_aWeaponBar[3]);
	bool Result = SqlExecQuery(pSql, GS()->Config(), aQuery);
	pPool->Release(pRaw);
	return Result;
}

bool CMMOManager::LoadInventory(CPlayer *pPlayer)
{
	if(!pPlayer || pPlayer->GetAccountId() <= 0) return false;
	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized()) return false;
	void *pRaw = pPool->Acquire();
	if(!pRaw) return false;
	MYSQL *pSql = (MYSQL *)pRaw;
	int64 UserId = pPlayer->GetAccountId();

	char aQuery[256];
	str_format(aQuery, sizeof(aQuery),
		"SELECT `ItemID`, `Count`, `Enchant`, `Durability`, IFNULL(`ExpiresAt`,0) "
		"FROM `tw_mmo_items` WHERE `UserID`=%lld", (long long)UserId);
	if(!SqlExecQuery(pSql, GS()->Config(), aQuery))
	{ pPool->Release(pRaw); return false; }

	pPlayer->m_MMOInventory.clear();

	MYSQL_RES *pRes = mysql_store_result(pSql);
	if(!pRes) { pPool->Release(pRaw); return true; }

	MYSQL_ROW Row;
	while((Row = mysql_fetch_row(pRes)))
	{
		if(!Row[0]) continue;
		int ItemID = str_toint(Row[0]);
		int Count = Row[1] ? str_toint(Row[1]) : 1;
		int Enchant = Row[2] ? str_toint(Row[2]) : 0;
		int ExpiresAt = Row[4] ? str_toint(Row[4]) : 0;

		if(ItemID <= 0 || Count <= 0) continue;
		// Skip expired items
		if(ExpiresAt > 0 && (int)time(nullptr) >= ExpiresAt) continue;

		pPlayer->m_MMOInventory.Add(ItemID, Count, Enchant);
	}
	mysql_free_result(pRes);

	// Migrate legacy EquipWeaponSlot (slot index) to EquippedSlots (ItemID-based)
	{
		char aQ[256];
		str_format(aQ, sizeof(aQ),
			"SELECT `EquipWeaponSlot` FROM `tw_mmo_players` WHERE `UserID`=%lld LIMIT 1",
			(long long)UserId);
		if(SqlExecQuery(pSql, GS()->Config(), aQ))
		{
			MYSQL_RES *pRes2 = mysql_store_result(pSql);
			if(pRes2)
			{
				MYSQL_ROW r = mysql_fetch_row(pRes2);
				if(r && r[0])
				{
					int OldWeaponSlot = str_toint(r[0]);
					if(OldWeaponSlot >= 0 && OldWeaponSlot < (int)pPlayer->m_MMOInventory.size())
					{
						const CItem &WeaponItem = pPlayer->m_MMOInventory[OldWeaponSlot];
						int ItemID = WeaponItem.GetID();
						const CMMOItemDescription *pWpnDesc = CMMOItemDescription::Get(ItemID);
						if(pWpnDesc)
						{
							pPlayer->m_EquippedSlots.equipSlot(pWpnDesc->GetType(), ItemID);
							// Clear old slot index in DB
							str_format(aQ, sizeof(aQ),
								"UPDATE `tw_mmo_players` SET `EquipWeaponSlot`=-1 WHERE `UserID`=%lld",
								(long long)UserId);
							SqlExecQuery(pSql, GS()->Config(), aQ);
						}
					}
				}
				mysql_free_result(pRes2);
			}
		}
	}

	pPlayer->MigrateWeaponLoadoutFromEquippedSlots();
	pPlayer->EnsureWeaponBarFromLoadouts();

	pPool->Release(pRaw);
	return true;
}

bool CMMOManager::LoadFriends(CPlayer *pPlayer)
{
	if(!pPlayer || pPlayer->GetAccountId() <= 0) return false;
	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized()) return false;
	void *pRaw = pPool->Acquire();
	if(!pRaw) return false;
	MYSQL *pSql = (MYSQL *)pRaw;
	int64 UserId = pPlayer->GetAccountId();

	char aQuery[256];
	str_format(aQuery, sizeof(aQuery),
		"SELECT `FriendUserID` FROM `tw_friends` WHERE `UserID`=%lld", (long long)UserId);
	if(!SqlExecQuery(pSql, GS()->Config(), aQuery))
	{ pPool->Release(pRaw); return false; }

	pPlayer->m_aFriends.clear();

	MYSQL_RES *pRes = mysql_store_result(pSql);
	if(!pRes) { pPool->Release(pRaw); return true; }

	MYSQL_ROW Row;
	while((Row = mysql_fetch_row(pRes)))
	{
		if(!Row[0]) continue;
		int64 FriendId = (int64)atoll(Row[0]);
		if(FriendId <= 0) continue;
		if((int)pPlayer->m_aFriends.size() >= 100) break;
		pPlayer->m_aFriends.push_back(FriendId);
	}
	mysql_free_result(pRes);
	pPool->Release(pRaw);
	pPlayer->m_aFriendsDirty = false;
	return true;
}

bool CMMOManager::SaveFriends(CPlayer *pPlayer)
{
	if(!pPlayer || pPlayer->GetAccountId() <= 0) return false;
	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized()) return false;
	void *pRaw = pPool->Acquire();
	if(!pRaw) return false;
	MYSQL *pSql = (MYSQL *)pRaw;
	int64 UserId = pPlayer->GetAccountId();

	// Clear old friends
	char aQuery[256];
	str_format(aQuery, sizeof(aQuery), "DELETE FROM `tw_friends` WHERE `UserID`=%lld", (long long)UserId);
	SqlExecQuery(pSql, GS()->Config(), aQuery);

	// Write new friends list
	for(const auto &FriendId : pPlayer->m_aFriends)
	{
		if(FriendId <= 0) continue;
		str_format(aQuery, sizeof(aQuery),
			"INSERT INTO `tw_friends` (`UserID`, `FriendUserID`) VALUES (%lld, %lld)",
			(long long)UserId, (long long)FriendId);
		SqlExecQuery(pSql, GS()->Config(), aQuery);
	}
	pPool->Release(pRaw);
	return true;
}

bool CMMOManager::SaveInventory(CPlayer *pPlayer)
{
	if(!pPlayer || pPlayer->GetAccountId() <= 0) return false;
	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized()) return false;
	void *pRaw = pPool->Acquire();
	if(!pRaw) return false;
	MYSQL *pSql = (MYSQL *)pRaw;
	int64 UserId = pPlayer->GetAccountId();

	// Clear old inventory
	char aQuery[256];
	str_format(aQuery, sizeof(aQuery), "DELETE FROM `tw_mmo_items` WHERE `UserID`=%lld", (long long)UserId);
	SqlExecQuery(pSql, GS()->Config(), aQuery);

	// Write new inventory
	for(const auto& Item : pPlayer->m_MMOInventory)
	{
		if(!Item.IsValid()) continue;
		str_format(aQuery, sizeof(aQuery),
			"INSERT INTO `tw_mmo_items` (`UserID`, `ItemID`, `Count`, `Enchant`, `Durability`, `ExpiresAt`) "
			"VALUES (%lld, %d, %d, %d, %d, %lld)",
			(long long)UserId, Item.GetID(), Item.GetValue(), Item.GetEnchant(), Item.GetDurability(), (long long)Item.GetExpiresAt());
		SqlExecQuery(pSql, GS()->Config(), aQuery);
	}
	pPool->Release(pRaw);
	return true;
}

// ─── Daily Checkin ───────────────────────────────────────────────────────

bool CMMOManager::LoadCheckinData(CPlayer *pPlayer)
{
	if(!pPlayer || pPlayer->GetAccountId() <= 0) return false;
	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized()) return false;
	void *pRaw = pPool->Acquire();
	if(!pRaw) return false;
	MYSQL *pSql = (MYSQL *)pRaw;
	int64 UserId = pPlayer->GetAccountId();

	char aQuery[256];
	str_format(aQuery, sizeof(aQuery),
		"SELECT `LastCheckinDate`, `CheckinStreak` FROM `tw_daily_checkin` WHERE `UserID`=%lld",
		(long long)UserId);
	if(!SqlExecQuery(pSql, GS()->Config(), aQuery))
	{ pPool->Release(pRaw); return false; }

	pPlayer->m_LastCheckinDate = 0;
	pPlayer->m_CheckinStreak = 0;

	MYSQL_RES *pRes = mysql_store_result(pSql);
	if(!pRes) { pPool->Release(pRaw); return true; }

	MYSQL_ROW Row = mysql_fetch_row(pRes);
	if(Row && Row[0] && Row[1])
	{
		pPlayer->m_LastCheckinDate = str_toint(Row[0]);
		pPlayer->m_CheckinStreak = str_toint(Row[1]);
	}
	mysql_free_result(pRes);
	pPool->Release(pRaw);
	return true;
}

bool CMMOManager::SaveCheckinData(CPlayer *pPlayer)
{
	if(!pPlayer || pPlayer->GetAccountId() <= 0) return false;
	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized()) return false;
	void *pRaw = pPool->Acquire();
	if(!pRaw) return false;
	MYSQL *pSql = (MYSQL *)pRaw;
	int64 UserId = pPlayer->GetAccountId();

	char aQuery[256];
	str_format(aQuery, sizeof(aQuery),
		"INSERT INTO `tw_daily_checkin` (`UserID`, `LastCheckinDate`, `CheckinStreak`) "
		"VALUES (%lld, %d, %d) "
		"ON DUPLICATE KEY UPDATE `LastCheckinDate`=%d, `CheckinStreak`=%d",
		(long long)UserId,
		pPlayer->m_LastCheckinDate, pPlayer->m_CheckinStreak,
		pPlayer->m_LastCheckinDate, pPlayer->m_CheckinStreak);
	bool Result = SqlExecQuery(pSql, GS()->Config(), aQuery);
	pPool->Release(pRaw);
	return Result;
}

// ─── Daily Recycle/Sell Limits ───────────────────────────────────────────

bool CMMOManager::LoadSellData(CPlayer *pPlayer)
{
	if(!pPlayer || pPlayer->GetAccountId() <= 0) return false;
	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized()) return false;
	void *pRaw = pPool->Acquire();
	if(!pRaw) return false;
	MYSQL *pSql = (MYSQL *)pRaw;
	int64 UserId = pPlayer->GetAccountId();

	char aQuery[256];
	str_format(aQuery, sizeof(aQuery),
		"SELECT `LastSellDate`, `DailySellGold`, `DailySellCount` FROM `tw_daily_sell` WHERE `UserID`=%lld",
		(long long)UserId);
	if(!SqlExecQuery(pSql, GS()->Config(), aQuery))
	{ pPool->Release(pRaw); return false; }

	pPlayer->m_LastSellDate = 0;
	pPlayer->m_DailySellGold = 0;
	pPlayer->m_DailySellCount = 0;

	MYSQL_RES *pRes = mysql_store_result(pSql);
	if(!pRes) { pPool->Release(pRaw); return true; }

	MYSQL_ROW Row = mysql_fetch_row(pRes);
	if(Row && Row[0] && Row[1] && Row[2])
	{
		pPlayer->m_LastSellDate = str_toint(Row[0]);
		pPlayer->m_DailySellGold = str_toint(Row[1]);
		pPlayer->m_DailySellCount = str_toint(Row[2]);
	}
	mysql_free_result(pRes);
	pPool->Release(pRaw);
	return true;
}

bool CMMOManager::SaveSellData(CPlayer *pPlayer)
{
	if(!pPlayer || pPlayer->GetAccountId() <= 0) return false;
	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized()) return false;
	void *pRaw = pPool->Acquire();
	if(!pRaw) return false;
	MYSQL *pSql = (MYSQL *)pRaw;
	int64 UserId = pPlayer->GetAccountId();

	char aQuery[320];
	str_format(aQuery, sizeof(aQuery),
		"INSERT INTO `tw_daily_sell` (`UserID`, `LastSellDate`, `DailySellGold`, `DailySellCount`) "
		"VALUES (%lld, %d, %d, %d) "
		"ON DUPLICATE KEY UPDATE `LastSellDate`=%d, `DailySellGold`=%d, `DailySellCount`=%d",
		(long long)UserId,
		pPlayer->m_LastSellDate, pPlayer->m_DailySellGold, pPlayer->m_DailySellCount,
		pPlayer->m_LastSellDate, pPlayer->m_DailySellGold, pPlayer->m_DailySellCount);
	bool Result = SqlExecQuery(pSql, GS()->Config(), aQuery);
	pPool->Release(pRaw);
	return Result;
}

int CMMOManager::GetLevel(CPlayer *pPlayer)
{
	if(!pPlayer) return 1;
	return pPlayer->GetStat(AttributeIdentifier::Level);
}

void CMMOManager::AddExperience(CPlayer *pPlayer, int Amount)
{
	if(!pPlayer || Amount <= 0) return;

	// Marriage exp bonus: +5% if married
	if(pPlayer->m_SpouseAccountID > 0)
		Amount = Amount * 105 / 100;

	int NewExp = pPlayer->GetStat(AttributeIdentifier::Experience) + Amount;
	pPlayer->SetStat(AttributeIdentifier::Experience, NewExp);
	pPlayer->m_MMODirty = true;

	int Needed = MMOExpForLevel(pPlayer->GetStat(AttributeIdentifier::Level));
	while(pPlayer->GetStat(AttributeIdentifier::Experience) >= Needed)
	{
		NewExp = pPlayer->GetStat(AttributeIdentifier::Experience) - Needed;
		int NewLevel = pPlayer->GetStat(AttributeIdentifier::Level) + 1;
		int NewSP = pPlayer->GetStat(AttributeIdentifier::SkillPoints) + 1;
		pPlayer->SetStat(AttributeIdentifier::Experience, NewExp);
		pPlayer->SetStat(AttributeIdentifier::Level, NewLevel);
		pPlayer->SetStat(AttributeIdentifier::SkillPoints, NewSP);
		Needed = MMOExpForLevel(NewLevel);

		char aBuf[128];
		str_format(aBuf, sizeof(aBuf),
			"✨ 你升级了！等级 %d — 获得 1 技能点", NewLevel);
		GS()->SendChatTo(pPlayer->GetCID(), aBuf);
		SavePlayerData(pPlayer);

		// Notify player of newly unlocked zones at this level
		if(Core() && Core()->WorldManager())
			Core()->WorldManager()->NotifyUnlockedZonesByLeveling(pPlayer);
	}
}

void CMMOManager::AddGold(CPlayer *pPlayer, int Amount)
{
	if(!pPlayer || Amount <= 0) return;
	pPlayer->SetStat(AttributeIdentifier::Gold, pPlayer->GetStat(AttributeIdentifier::Gold) + Amount);
	pPlayer->m_MMODirty = true;
}

bool CMMOManager::SpendGold(CPlayer *pPlayer, int Amount)
{
	if(!pPlayer || pPlayer->GetStat(AttributeIdentifier::Gold) < Amount) return false;
	pPlayer->SetStat(AttributeIdentifier::Gold, pPlayer->GetStat(AttributeIdentifier::Gold) - Amount);
	pPlayer->m_MMODirty = true;
	return true;
}

int CMMOManager::GetGold(CPlayer *pPlayer)
{
	if(!pPlayer) return 0;
	return pPlayer->GetStat(AttributeIdentifier::Gold);
}

int CMMOManager::GetItemCount(CPlayer *pPlayer, int ItemID)
{
	if(!pPlayer) return 0;
	return pPlayer->m_MMOInventory.CountByID(ItemID);
}

bool CMMOManager::GiveItem(CPlayer *pPlayer, int ItemID, int Count, int Enchant)
{
	if(!pPlayer || ItemID <= 0 || Count <= 0) return false;
	if(!CMMOItemDescription::Get(ItemID))
	{
		dbg_msg("mmo", "GiveItem: unknown item ID %d", ItemID);
		return false;
	}
	bool Result = pPlayer->m_MMOInventory.Add(ItemID, Count, Enchant);
	if(Result)
		pPlayer->m_MMODirty = true;
	return Result;
}

bool CMMOManager::GiveAllItems(CPlayer *pPlayer, int *pOutGiven, int *pOutSkipped)
{
	if(!pPlayer || pPlayer->GetAccountId() <= 0)
		return false;

	int Given = 0;
	int Skipped = 0;

	for(const auto &Entry : CMMOItemDescription::Data())
	{
		const CMMOItemDescription &Def = Entry.second;
		if(!Def.IsValid())
			continue;

		if(!Def.m_Stackable && HasItem(pPlayer, Def.m_ID, 1))
		{
			Skipped++;
			continue;
		}

		const int Count = Def.m_Stackable ? 10 : 1;
		if(GiveItem(pPlayer, Def.m_ID, Count, 0))
			Given++;
		else
			Skipped++;
	}

	if(Given > 0)
		SaveInventory(pPlayer);

	if(pOutGiven)
		*pOutGiven = Given;
	if(pOutSkipped)
		*pOutSkipped = Skipped;
	return Given > 0;
}

bool CMMOManager::TakeItem(CPlayer *pPlayer, int ItemID, int Count)
{
	if(!pPlayer || Count <= 0) return false;
	bool Result = pPlayer->m_MMOInventory.RemoveByID(ItemID, Count);
	if(Result)
		pPlayer->m_MMODirty = true;
	return Result;
}

bool CMMOManager::HasItem(CPlayer *pPlayer, int ItemID, int Count)
{
	if(!pPlayer) return false;
	return pPlayer->m_MMOInventory.CountByID(ItemID) >= Count;
}

// ─── Pet Data ────────────────────────────────────────────────────────

bool CMMOManager::LoadPetData(CPlayer *pPlayer)
{
	if(!pPlayer || pPlayer->GetAccountId() <= 0) return false;
	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized()) return false;
	void *pRaw = pPool->Acquire();
	if(!pRaw) return false;
	MYSQL *pSql = (MYSQL *)pRaw;
	int64 UserId = pPlayer->GetAccountId();

	char aQuery[256];
	str_format(aQuery, sizeof(aQuery),
		"SELECT `PetID`, `PetName`, `PetLevel` FROM `tw_pets` WHERE `UserID`=%lld LIMIT 1",
		(long long)UserId);
	if(!SqlExecQuery(pSql, GS()->Config(), aQuery))
	{ pPool->Release(pRaw); return false; }

	MYSQL_RES *pRes = mysql_store_result(pSql);
	if(!pRes) { pPool->Release(pRaw); return true; }

	pPlayer->m_PetID = 0;
	pPlayer->m_PetLevel = 1;
	pPlayer->m_aPetName[0] = 0;

	MYSQL_ROW Row = mysql_fetch_row(pRes);
	if(Row)
	{
		pPlayer->m_PetID = Row[0] ? str_toint(Row[0]) : 0;
		if(Row[1]) str_copy(pPlayer->m_aPetName, Row[1], sizeof(pPlayer->m_aPetName));
		pPlayer->m_PetLevel = Row[2] ? str_toint(Row[2]) : 1;
	}
	mysql_free_result(pRes);
	pPool->Release(pRaw);
	return true;
}

bool CMMOManager::SavePetData(CPlayer *pPlayer)
{
	if(!pPlayer || pPlayer->GetAccountId() <= 0) return false;
	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized()) return false;
	void *pRaw = pPool->Acquire();
	if(!pRaw) return false;
	MYSQL *pSql = (MYSQL *)pRaw;
	int64 UserId = pPlayer->GetAccountId();

	char aQuery[512];
	str_format(aQuery, sizeof(aQuery),
		"INSERT INTO `tw_pets` (`UserID`, `PetID`, `PetName`, `PetLevel`, `PetExperience`) "
		"VALUES (%lld, %d, '%s', %d, 0) "
		"ON DUPLICATE KEY UPDATE `PetID`=%d, `PetName`='%s', `PetLevel`=%d",
		(long long)UserId,
		pPlayer->m_PetID, pPlayer->m_aPetName, pPlayer->m_PetLevel,
		pPlayer->m_PetID, pPlayer->m_aPetName, pPlayer->m_PetLevel);
	bool Result = SqlExecQuery(pSql, GS()->Config(), aQuery);
	pPool->Release(pRaw);
	return Result;
}

// ══════════════════════════════════════════════════════════════════════
//  Housing
// ══════════════════════════════════════════════════════════════════════

bool CMMOManager::LoadHouseData(CPlayer *pPlayer)
{
	if(!pPlayer || pPlayer->GetAccountId() <= 0) return false;
	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized()) return false;
	void *pRaw = pPool->Acquire();
	if(!pRaw) return false;
	MYSQL *pSql = (MYSQL *)pRaw;
	int64 UserId = pPlayer->GetAccountId();

	char aQuery[256];
	str_format(aQuery, sizeof(aQuery),
		"SELECT `HasHouse`, `HouseLevel` FROM `tw_housing` WHERE `UserID`=%lld",
		(long long)UserId);
	if(!SqlExecQuery(pSql, GS()->Config(), aQuery))
	{ pPool->Release(pRaw); return false; }

	pPlayer->m_HasHouse = false;
	pPlayer->m_HouseLevel = 1;

	MYSQL_RES *pRes = mysql_store_result(pSql);
	if(!pRes) { pPool->Release(pRaw); return true; }

	MYSQL_ROW Row = mysql_fetch_row(pRes);
	if(Row && Row[0])
	{
		pPlayer->m_HasHouse = str_toint(Row[0]) != 0;
		if(Row[1]) pPlayer->m_HouseLevel = str_toint(Row[1]);
	}
	mysql_free_result(pRes);
	pPool->Release(pRaw);
	return true;
}

bool CMMOManager::SaveHouseData(CPlayer *pPlayer)
{
	if(!pPlayer || pPlayer->GetAccountId() <= 0) return false;
	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized()) return false;
	void *pRaw = pPool->Acquire();
	if(!pRaw) return false;
	MYSQL *pSql = (MYSQL *)pRaw;
	int64 UserId = pPlayer->GetAccountId();

	char aQuery[256];
	str_format(aQuery, sizeof(aQuery),
		"INSERT INTO `tw_housing` (`UserID`, `HasHouse`, `HouseLevel`) "
		"VALUES (%lld, %d, %d) "
		"ON DUPLICATE KEY UPDATE `HasHouse`=%d, `HouseLevel`=%d",
		(long long)UserId,
		pPlayer->m_HasHouse ? 1 : 0, pPlayer->m_HouseLevel,
		pPlayer->m_HasHouse ? 1 : 0, pPlayer->m_HouseLevel);
	bool Result = SqlExecQuery(pSql, GS()->Config(), aQuery);
	pPool->Release(pRaw);
	return Result;
}

bool CMMOManager::LoadMarriageData(CPlayer *pPlayer)
{
	if(!pPlayer || pPlayer->GetAccountId() <= 0) return false;
	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized()) return false;
	void *pRaw = pPool->Acquire();
	if(!pRaw) return false;
	MYSQL *pSql = (MYSQL *)pRaw;
	int64 UserId = pPlayer->GetAccountId();

	pPlayer->m_SpouseAccountID = 0;
	pPlayer->m_MarriageDate = 0;

	char aQuery[256];
	str_format(aQuery, sizeof(aQuery),
		"SELECT `SpouseA`, `SpouseB`, `MarriedAt` FROM `tw_marriages` WHERE `SpouseA`=%lld OR `SpouseB`=%lld LIMIT 1",
		(long long)UserId, (long long)UserId);
	if(!SqlExecQuery(pSql, GS()->Config(), aQuery))
	{
		pPool->Release(pRaw);
		return false;
	}

	MYSQL_RES *pRes = mysql_store_result(pSql);
	if(!pRes)
	{
		pPool->Release(pRaw);
		return true;
	}

	MYSQL_ROW Row = mysql_fetch_row(pRes);
	if(Row && Row[0] && Row[1] && Row[2])
	{
		int64 A = (int64)atoll(Row[0]);
		int64 B = (int64)atoll(Row[1]);
		pPlayer->m_SpouseAccountID = (UserId == A) ? B : A;
		pPlayer->m_MarriageDate = str_toint(Row[2]);
	}
	mysql_free_result(pRes);
	pPool->Release(pRaw);
	return true;
}

bool CMMOManager::SaveMarriageData(CPlayer *pPlayer)
{
	// Marriage data is written on marriage/divorce directly via DB queries.
	// This stub exists for the declaration in mmo_manager.h.
	(void)pPlayer;
	return true;
}
