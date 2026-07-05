#include "mmo_manager.h"
#include <game/server/data_center.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/core/tworld_controller.h>
#include <base/system.h>
#include <engine/shared/config.h>
#include <engine/shared/jsonparser.h>
#include <game/server/core/components/vote/vote_menu_manager.h>
#include <game/server/core/components/vote/vote_wrapper.h>
#include <game/server/account.h>
#include <game/server/sql_query.h>
#include <game/server/sql_pool.h>
#include <game/server/sql_wrapper.h>
#include <mysql.h>
#include <game/server/entities/character.h>
#include <game/server/core/components/mmo/mmo_item.h>
#include <vector>

int CMMOManager::GetMailCount(int64 AccountID)
{
	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized()) return 0;
	void *pRaw = pPool->Acquire();
	if(!pRaw) return 0;
	MYSQL *pSql = (MYSQL *)pRaw;

	char aQuery[256];
	str_format(aQuery, sizeof(aQuery),
		"SELECT COUNT(*) FROM `tw_accounts_mailbox` WHERE `UserID`=%lld", (long long)AccountID);
	if(!SqlExecQuery(pSql, GS()->Config(), aQuery))
	{ pPool->Release(pRaw); return 0; }

	MYSQL_RES *pRes = mysql_store_result(pSql);
	if(!pRes) { pPool->Release(pRaw); return 0; }

	MYSQL_ROW Row = mysql_fetch_row(pRes);
	int Count = Row ? str_toint(Row[0]) : 0;
	mysql_free_result(pRes);
	pPool->Release(pRaw);
	return Count;
}

int CMMOManager::GetUnreadMailCount(int64 AccountID)
{
	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized()) return 0;
	void *pRaw = pPool->Acquire();
	if(!pRaw) return 0;
	MYSQL *pSql = (MYSQL *)pRaw;

	char aQuery[256];
	str_format(aQuery, sizeof(aQuery),
		"SELECT COUNT(*) FROM `tw_accounts_mailbox` WHERE `UserID`=%lld AND `Readed`=0", (long long)AccountID);
	if(!SqlExecQuery(pSql, GS()->Config(), aQuery))
	{ pPool->Release(pRaw); return 0; }

	MYSQL_RES *pRes = mysql_store_result(pSql);
	if(!pRes) { pPool->Release(pRaw); return 0; }

	MYSQL_ROW Row = mysql_fetch_row(pRes);
	int Count = Row ? str_toint(Row[0]) : 0;
	mysql_free_result(pRes);
	pPool->Release(pRaw);
	return Count;
}

void CMMOManager::ShowMailboxVotes(int ClientID, CVoteMenuManager *pVote, CPlayer *pP, SPlayerVote *pSVote)
{
	(void)pSVote;
	if(!pVote || !pP)
		return;

	pVote->SetVoteLastPage(VOTE_PAGE_MMO_SOCIAL);
	pVote->SetVoteBuildClientID(ClientID);
	CVoteWrapper V(ClientID, GS(), pVote);

	int64 AccountID = pP->GetAccountId();
	int MailCount = GetMailCount(AccountID);
	int UnreadCount = GetUnreadMailCount(AccountID);

	char aTitle[64];
	str_format(aTitle, sizeof(aTitle), "邮箱 (%d 封, %d 未读)", MailCount, UnreadCount);
	V.GroupTitle(aTitle);

	if(MailCount <= 0)
	{
		V.Info("收件箱是空的");
		V.Footer();
		return;
	}

	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized())
	{
		V.Info("邮箱暂时不可用");
		V.Footer();
		return;
	}
	void *pRaw = pPool->Acquire();
	if(!pRaw)
	{
		V.Info("邮箱暂时不可用");
		V.Footer();
		return;
	}
	MYSQL *pSql = (MYSQL *)pRaw;

	char aQuery[512];
	str_format(aQuery, sizeof(aQuery),
		"SELECT `ID`, `Name`, `Description`, `Sender`, `Readed`, `CreatedAt`, `AttachedItems` FROM `tw_accounts_mailbox` WHERE `UserID`=%lld ORDER BY `CreatedAt` DESC",
		(long long)AccountID);
	if(!SqlExecQuery(pSql, GS()->Config(), aQuery))
	{
		pPool->Release(pRaw);
		V.Info("读取邮件失败");
		V.Footer();
		return;
	}

	MYSQL_RES *pRes = mysql_store_result(pSql);
	if(!pRes)
	{
		pPool->Release(pRaw);
		V.Info("读取邮件失败");
		V.Footer();
		return;
	}

	bool bUnreadSection = false;
	bool bReadSection = false;
	int ClaimableCount = 0;
	char aLine[128];
	char aCmd[64];

	MYSQL_ROW Row;
	while((Row = mysql_fetch_row(pRes)))
	{
		if(!Row[0])
			continue;
		const int Readed = Row[4] ? str_toint(Row[4]) : 0;
		const int MailID = str_toint(Row[0]);
		const char *pName = Row[1] ? Row[1] : "(无标题)";
		const char *pSender = Row[3] ? Row[3] : "系统";
		const char *pAttached = Row[6] ? Row[6] : "";
		if(pAttached[0] == '[')
			ClaimableCount++;

		if(Readed == 0)
		{
			if(!bUnreadSection)
			{
				V.GroupLine();
				V.GroupTitle("未读邮件");
				bUnreadSection = true;
			}
			str_format(aLine, sizeof(aLine), "%s — %s", pName, pSender);
			str_format(aCmd, sizeof(aCmd), "ccv_mail_read %d", MailID);
			V.Option(aCmd, aLine);
		}
		else
		{
			if(!bReadSection)
			{
				V.GroupLine();
				V.GroupTitle("已读邮件");
				bReadSection = true;
			}
			str_format(aLine, sizeof(aLine), "%s — %s", pName, pSender);
			str_format(aCmd, sizeof(aCmd), "ccv_mail_read %d", MailID);
			V.Option(aCmd, aLine);
		}
	}

	mysql_free_result(pRes);
	pPool->Release(pRaw);

	if(ClaimableCount > 0)
	{
		V.GroupLine();
		str_format(aLine, sizeof(aLine), "一键领取全部附件 (%d)", ClaimableCount);
		V.Option("ccv_mail_claimall", aLine);
	}

	if(bReadSection)
	{
		V.GroupLine();
		V.Option("ccv_mail_delread", "删除所有已读邮件");
	}

	V.Footer();
}

void CMMOManager::ShowMailReadVotes(int ClientID, CVoteMenuManager *pVote, CPlayer *pP, SPlayerVote *pSVote, int MailID)
{
	(void)pSVote;
	if(!pVote || !pP)
		return;

	pVote->SetVoteLastPage(VOTE_PAGE_MMO_MAILBOX);
	pVote->SetVoteBuildClientID(ClientID);
	CVoteWrapper V(ClientID, GS(), pVote);

	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized())
	{
		V.GroupTitle("邮件");
		V.Info("邮箱暂时不可用");
		V.Footer();
		return;
	}
	void *pRaw = pPool->Acquire();
	if(!pRaw)
	{
		V.GroupTitle("邮件");
		V.Info("邮箱暂时不可用");
		V.Footer();
		return;
	}
	MYSQL *pSql = (MYSQL *)pRaw;

	char aQuery[256];
	str_format(aQuery, sizeof(aQuery),
		"SELECT `Name`, `Description`, `Sender`, `CreatedAt`, `AttachedItems` FROM `tw_accounts_mailbox` WHERE `ID`=%d AND `UserID`=%lld LIMIT 1",
		MailID, (long long)pP->GetAccountId());
	if(!SqlExecQuery(pSql, GS()->Config(), aQuery))
	{
		pPool->Release(pRaw);
		V.GroupTitle("邮件");
		V.Info("读取邮件失败");
		V.Footer();
		return;
	}

	MYSQL_RES *pRes = mysql_store_result(pSql);
	if(!pRes)
	{
		pPool->Release(pRaw);
		V.GroupTitle("邮件");
		V.Info("读取邮件失败");
		V.Footer();
		return;
	}

	MYSQL_ROW Row = mysql_fetch_row(pRes);
	if(!Row)
	{
		mysql_free_result(pRes);
		pPool->Release(pRaw);
		V.GroupTitle("邮件");
		V.Info("邮件不存在");
		V.Footer();
		return;
	}

	const char *pName = Row[0] ? Row[0] : "(无标题)";
	const char *pDesc = Row[1] ? Row[1] : "";
	const char *pSender = Row[2] ? Row[2] : "系统";
	const char *pCreatedAt = Row[3] ? Row[3] : "";
	const char *pAttached = Row[4] ? Row[4] : "";

	V.GroupTitle(pName);

	char aLine[VOTE_DESC_LENGTH];
	str_format(aLine, sizeof(aLine), "发件人: %s", pSender);
	V.Info(aLine);
	if(pCreatedAt[0])
	{
		str_format(aLine, sizeof(aLine), "时间: %s", pCreatedAt);
		V.Info(aLine);
	}

	V.GroupLine();
	if(pDesc[0])
		V.Info(pDesc);

	if(pAttached[0] && pAttached[0] == '[')
	{
		V.GroupLine();
		V.GroupTitle("附件");

		CJsonParser Parser;
		json_value *pRoot = Parser.ParseString(pAttached, "mail_attachments");
		if(pRoot && pRoot->type == json_array)
		{
			for(unsigned int i = 0; i < pRoot->u.array.length; i++)
			{
				const json_value &Entry = (*pRoot)[(int)i];
				if(Entry.type != json_object)
					continue;
				int ItemID = (int)(json_int_t)Entry["id"];
				int Count = (int)(json_int_t)Entry["count"];

				const CMMOItemDescription *pDef = CMMOItemDescription::Get(ItemID);
				const char *pItemName = pDef ? GS()->Loc(pP->GetCID(), pDef->m_aNameKey, pDef->m_aName) : "未知物品";
				str_format(aLine, sizeof(aLine), "%s x%d", pItemName, Count);
				V.Info(aLine);
			}
			json_value_free(pRoot);
		}
		else if(pRoot)
		{
			json_value_free(pRoot);
		}

		char aClaimCmd[64];
		str_format(aClaimCmd, sizeof(aClaimCmd), "ccv_mail_claim %d", MailID);
		V.Option(aClaimCmd, "领取附件");
	}

	{
		char aQuery2[256];
		str_format(aQuery2, sizeof(aQuery2),
			"UPDATE `tw_accounts_mailbox` SET `Readed`=1 WHERE `ID`=%d AND `UserID`=%lld AND `Readed`=0",
			MailID, (long long)pP->GetAccountId());
		SqlExecQuery(pSql, GS()->Config(), aQuery2);
	}

	mysql_free_result(pRes);
	pPool->Release(pRaw);

	V.GroupLine();
	char aDelCmd[64];
	str_format(aDelCmd, sizeof(aDelCmd), "ccv_mail_delete %d", MailID);
	V.Option(aDelCmd, "删除邮件");
	V.Footer();
}

bool CMMOManager::SendMail(const char *pSender, int64 TargetAID, const char *pTitle, const char *pDesc, const std::vector<std::pair<int,int>> &vItems)
{
	if(!pSender || TargetAID <= 0 || !pTitle) return false;
	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized()) return false;
	void *pRaw = pPool->Acquire();
	if(!pRaw) return false;
	MYSQL *pSql = (MYSQL *)pRaw;

	// Build AttachedItems JSON
	char aItemsJSON[2048];
	if(vItems.empty())
	{
		aItemsJSON[0] = 0;
	}
	else
	{
		str_copy(aItemsJSON, "[", sizeof(aItemsJSON));
		for(size_t i = 0; i < vItems.size(); i++)
		{
			char aEntry[128];
			str_format(aEntry, sizeof(aEntry), "{\"id\":%d,\"count\":%d,\"enchant\":0,\"dur\":100,\"exp\":0}",
				vItems[i].first, vItems[i].second);
			if((int)str_length(aItemsJSON) + (int)str_length(aEntry) + 3 > (int)sizeof(aItemsJSON))
				break;
			if(i > 0) str_append(aItemsJSON, ",", sizeof(aItemsJSON));
			str_append(aItemsJSON, aEntry, sizeof(aItemsJSON));
		}
		str_append(aItemsJSON, "]", sizeof(aItemsJSON));
	}

	CSqlString<128> EscapedSender(pSender);
	CSqlString<128> EscapedTitle(pTitle);
	CSqlString<2048> EscapedDesc(pDesc ? pDesc : "");
	CSqlString<2048> EscapedItems(aItemsJSON[0] ? aItemsJSON : "");

	char aQuery[4096];
	str_format(aQuery, sizeof(aQuery),
		"INSERT INTO `tw_accounts_mailbox` (`Name`, `Description`, `UserID`, `Sender`, `AttachedItems`, `Readed`) "
		"VALUES ('%s', '%s', %lld, '%s', '%s', 0)",
		EscapedTitle.cstr(), EscapedDesc.cstr(), (long long)TargetAID,
		EscapedSender.cstr(), EscapedItems.cstr());
	bool Result = SqlExecQuery(pSql, GS()->Config(), aQuery);
	pPool->Release(pRaw);
	return Result;
}

int CMMOManager::ClaimAllMailAttachments(CPlayer *pPlayer)
{
	if(!pPlayer || pPlayer->GetAccountId() <= 0)
		return 0;

	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized())
		return 0;
	void *pRaw = pPool->Acquire();
	if(!pRaw)
		return 0;
	MYSQL *pSql = (MYSQL *)pRaw;

	char aQuery[256];
	str_format(aQuery, sizeof(aQuery),
		"SELECT `ID` FROM `tw_accounts_mailbox` WHERE `UserID`=%lld AND `AttachedItems` LIKE '[%%'",
		(long long)pPlayer->GetAccountId());
	if(!SqlExecQuery(pSql, GS()->Config(), aQuery))
	{
		pPool->Release(pRaw);
		return 0;
	}

	MYSQL_RES *pRes = mysql_store_result(pSql);
	if(!pRes)
	{
		pPool->Release(pRaw);
		return 0;
	}

	std::vector<int> vMailIDs;
	MYSQL_ROW Row;
	while((Row = mysql_fetch_row(pRes)))
	{
		if(Row[0])
			vMailIDs.push_back(str_toint(Row[0]));
	}
	mysql_free_result(pRes);
	pPool->Release(pRaw);

	int Claimed = 0;
	for(int MailID : vMailIDs)
	{
		if(ClaimMailAttachments(pPlayer, MailID, true))
			Claimed++;
	}
	return Claimed;
}

bool CMMOManager::ClaimMailAttachments(CPlayer *pPlayer, int MailID, bool Silent)
{
	if(!pPlayer || MailID <= 0) return false;
	int64 AccountID = pPlayer->GetAccountId();

	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized()) return false;
	void *pRaw = pPool->Acquire();
	if(!pRaw) return false;
	MYSQL *pSql = (MYSQL *)pRaw;

	char aQuery[256];
	str_format(aQuery, sizeof(aQuery),
		"SELECT `AttachedItems` FROM `tw_accounts_mailbox` WHERE `ID`=%d AND `UserID`=%lld LIMIT 1",
		MailID, (long long)AccountID);
	if(!SqlExecQuery(pSql, GS()->Config(), aQuery))
	{ pPool->Release(pRaw); return false; }

	MYSQL_RES *pRes = mysql_store_result(pSql);
	if(!pRes) { pPool->Release(pRaw); return false; }

	MYSQL_ROW Row = mysql_fetch_row(pRes);
	if(!Row || !Row[0] || !Row[0][0])
	{
		mysql_free_result(pRes);
		pPool->Release(pRaw);
		return false;
	}

	const char *pAttached = Row[0];
	bool bSuccess = false;

	if(pAttached[0] == '[') // JSON array
	{
		CJsonParser Parser;
		json_value *pRoot = Parser.ParseString(pAttached, "claim_attachments");
		if(pRoot && pRoot->type == json_array)
		{
			for(unsigned int i = 0; i < pRoot->u.array.length; i++)
			{
				const json_value &Entry = (*pRoot)[(int)i];
				if(Entry.type != json_object) continue;
				int ItemID = (int)(json_int_t)Entry["id"];
				int Count = (int)(json_int_t)Entry["count"];
				if(ItemID <= 0 || Count <= 0) continue;
				if(GiveItem(pPlayer, ItemID, Count))
					bSuccess = true;
			}
			json_value_free(pRoot);
		}
		else if(pRoot)
		{
			json_value_free(pRoot);
		}
	}

	mysql_free_result(pRes);
	pPool->Release(pRaw);

	if(bSuccess)
	{
		DeleteMail(MailID);
		if(!Silent)
			GS()->SendChatTo(pPlayer->GetCID(), "✅ 已领取附件物品。");
	}
	return bSuccess;
}

void CMMOManager::DeleteMail(int MailID)
{
	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized()) return;
	void *pRaw = pPool->Acquire();
	if(!pRaw) return;
	MYSQL *pSql = (MYSQL *)pRaw;

	char aQuery[128];
	str_format(aQuery, sizeof(aQuery),
		"DELETE FROM `tw_accounts_mailbox` WHERE `ID`=%d", MailID);
	SqlExecQuery(pSql, GS()->Config(), aQuery);
	pPool->Release(pRaw);
}

void CMMOManager::DeleteReadMails(int64 AccountID)
{
	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized()) return;
	void *pRaw = pPool->Acquire();
	if(!pRaw) return;
	MYSQL *pSql = (MYSQL *)pRaw;

	char aQuery[256];
	str_format(aQuery, sizeof(aQuery),
		"DELETE FROM `tw_accounts_mailbox` WHERE `UserID`=%lld AND `Readed`=1",
		(long long)AccountID);
	SqlExecQuery(pSql, GS()->Config(), aQuery);
	pPool->Release(pRaw);
}
