-- TeeDefense Archive — MySQL schema (required for ArchiveServer)
-- Password: $sha256$<16-char-salt-hex>$<64-char-digest-hex>
-- Holding JSON: {"pickaxe":<id>,"axe":<id>,"sword":<id>,"turret":<id>}  (0 = none)
-- Items Extra JSON: {"Extra":{"Cards":[],"Parts":[]}}

SET SQL_MODE = "NO_AUTO_VALUE_ON_ZERO";
SET AUTOCOMMIT = 0;
START TRANSACTION;
SET time_zone = "+00:00";

CREATE TABLE IF NOT EXISTS `tw_Accounts`
(
  `UserID` int NOT NULL AUTO_INCREMENT,
  `Username` varchar(64) NOT NULL,
  `Password` varchar(128) NOT NULL,
  `Language` varchar(64) NOT NULL DEFAULT 'zh-cn',
  `Holding` JSON DEFAULT NULL,
  PRIMARY KEY (`UserID`),
  UNIQUE KEY `idx_username` (`Username`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

CREATE TABLE IF NOT EXISTS `tw_Items`
(
  `UserID` int NOT NULL,
  `ItemID` int NOT NULL,
  `Num` int NOT NULL,
  `Extra` longtext CHARACTER SET utf8mb4 COLLATE utf8mb4_bin DEFAULT NULL CHECK (json_valid(`Extra`)),
  PRIMARY KEY (`UserID`, `ItemID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

COMMIT;

-- ---------------------------------------------------------------------------
-- Upgrade from legacy schema (Sword / Pickaxe / Axe columns) — run if needed
-- ---------------------------------------------------------------------------
-- ALTER TABLE `tw_Accounts` ADD COLUMN `Holding` JSON DEFAULT NULL;
-- ALTER TABLE `tw_Accounts` MODIFY COLUMN `Password` varchar(128) NOT NULL;
-- UPDATE `tw_Accounts` SET `Holding` = JSON_OBJECT(
--   'pickaxe', IFNULL(`Pickaxe`, 0),
--   'axe', IFNULL(`Axe`, 0),
--   'sword', IFNULL(`Sword`, 0),
--   'turret', 0
-- ) WHERE `Holding` IS NULL;
-- ALTER TABLE `tw_Items` ADD PRIMARY KEY (`UserID`, `ItemID`);
