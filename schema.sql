-- NASA procurement bot database schema (structure only) plus the seed rows it needs to run.
-- Load with: mysql nasaprocurement < schema.sql

/*M!999999\- enable the sandbox mode */ 
CREATE TABLE `control_data` (
  `control_id` int(11) NOT NULL AUTO_INCREMENT,
  `control_name` varchar(32) DEFAULT NULL,
  `control_data` varchar(64) NOT NULL,
  PRIMARY KEY (`control_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;
CREATE TABLE `hv_contract_modifications` (
  `hv_mod_id` varchar(128) NOT NULL,
  `hv_contract_id` varchar(64) NOT NULL,
  `hv_mod_obligation` decimal(16,2) NOT NULL,
  `hv_mod_number` varchar(16) NOT NULL,
  `hv_mod_action_type` varchar(1) NOT NULL,
  `hv_mod_action_description` varchar(64) NOT NULL,
  `hv_aware_date` datetime NOT NULL DEFAULT current_timestamp(),
  `hv_mod_action_date` date DEFAULT NULL,
  `hv_mod_description` varchar(1024) DEFAULT NULL,
  PRIMARY KEY (`hv_mod_id`),
  KEY `hv_contract_id` (`hv_contract_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;
CREATE TABLE `hv_contracts` (
  `hv_contract_id` varchar(64) NOT NULL,
  `hv_contract_description` varchar(256) DEFAULT NULL,
  `hv_aware_date` datetime NOT NULL DEFAULT current_timestamp(),
  `hv_parent_id` varchar(64) DEFAULT NULL,
  `hv_watch_children` tinyint(1) NOT NULL DEFAULT 0,
  `hv_seeded` tinyint(1) NOT NULL DEFAULT 0,
  `hv_active` tinyint(1) NOT NULL DEFAULT 1,
  `hv_children_seeded` tinyint(1) NOT NULL DEFAULT 0,
  PRIMARY KEY (`hv_contract_id`),
  KEY `hv_parent_id` (`hv_parent_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;
CREATE TABLE `opportunities` (
  `date_added` timestamp NOT NULL DEFAULT current_timestamp(),
  `noticeId` varchar(32) NOT NULL,
  `award_date` varchar(32) DEFAULT NULL,
  `award_amount` decimal(15,2) DEFAULT NULL,
  `award_awardee` varchar(128) DEFAULT NULL,
  `type` varchar(32) DEFAULT NULL,
  `department` varchar(128) DEFAULT NULL,
  `officeAddressZipcode` varchar(32) NOT NULL,
  `city` varchar(64) DEFAULT NULL,
  `description` varchar(128) DEFAULT NULL,
  `description_text` text DEFAULT NULL,
  `postedDate` date DEFAULT NULL,
  `typeOfSetAside` varchar(32) DEFAULT NULL,
  `responseDeadline` varchar(32) DEFAULT NULL,
  `solicitationNumber` varchar(128) DEFAULT NULL,
  `title` varchar(256) DEFAULT NULL,
  `uiLink` varchar(512) DEFAULT NULL,
  `twitter_status` varchar(32) DEFAULT NULL,
  `mastodon_status` varchar(32) DEFAULT NULL,
  `bluesky_status` varchar(128) DEFAULT NULL,
  `addl_pages` varchar(512) DEFAULT NULL,
  `placeOfPerformanceCity` varchar(64) DEFAULT NULL,
  `placeOfPerformanceState` varchar(32) DEFAULT NULL,
  `office` varchar(64) DEFAULT NULL,
  `primaryPOCfullName` varchar(128) DEFAULT NULL,
  `primaryPOCfullEmail` varchar(255) DEFAULT NULL,
  `primaryPOCphone` varchar(64) DEFAULT NULL,
  `secondaryPOCfullName` varchar(128) DEFAULT NULL,
  `secondaryPOCfullEmail` varchar(255) DEFAULT NULL,
  `secondaryPOCphone` varchar(64) DEFAULT NULL,
  `processed` tinyint(4) NOT NULL DEFAULT 0,
  `post_attempts` tinyint(4) NOT NULL DEFAULT 0,
  PRIMARY KEY (`noticeId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;
CREATE TABLE `saved_searches` (
  `search_id` int(11) NOT NULL AUTO_INCREMENT,
  `search_name` varchar(32) NOT NULL,
  `search_url` varchar(256) NOT NULL,
  PRIMARY KEY (`search_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;
CREATE TABLE `site_names` (
  `location_id` int(11) NOT NULL AUTO_INCREMENT,
  `officeAddressZipcode` int(16) DEFAULT NULL,
  `city` varchar(64) DEFAULT NULL,
  `site_name` varchar(64) DEFAULT NULL,
  PRIMARY KEY (`location_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;
CREATE TABLE `snooze` (
  `snooze_id` int(11) NOT NULL AUTO_INCREMENT,
  `snooze_time` timestamp NOT NULL DEFAULT current_timestamp(),
  PRIMARY KEY (`snooze_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;



-- Searches run by nasasamscraper.php (appended to the SAM.gov v2 search URL)
/*M!999999\- enable the sandbox mode */ 
INSERT INTO `saved_searches` VALUES (1,'NASA','organizationName=NATIONAL%20AERONAUTICS%20AND%20SPACE%20ADMINISTRATION');
INSERT INTO `saved_searches` VALUES (2,'NAICS code','ncode=927110');
INSERT INTO `saved_searches` VALUES (3,'CCode V126','ccode=V126');
INSERT INTO `site_names` VALUES (1,20546,'WASHINGTON','NASA Headquarters');
INSERT INTO `site_names` VALUES (2,20771,'GREENBELT','Goddard Space Flight Center');
INSERT INTO `site_names` VALUES (3,23681,'HAMPTON','Langley Research Center');
INSERT INTO `site_names` VALUES (4,32899,'KENNEDY SPACE CENTER','Kennedy Space Center');
INSERT INTO `site_names` VALUES (5,35812,'HUNTSVILLE','Marshall Space Flight Center');
INSERT INTO `site_names` VALUES (6,39529,'STENNIS SPACE CENTER','Stennis Space Center');
INSERT INTO `site_names` VALUES (7,44135,'CLEVELAND','Glenn Research Center');
INSERT INTO `site_names` VALUES (8,77058,'HOUSTON','Johnson Space Center');
INSERT INTO `site_names` VALUES (9,93523,'EDWARDS','Neil Armstrong Flight Research Center');
INSERT INTO `site_names` VALUES (10,94035,'MOFFETT FIELD','Ames Research Center');
INSERT INTO `site_names` VALUES (11,91109,'PASADENA','Jet Propulsion Laboratory');

-- Control flags (active = 1 lets procurement_check.php post; turnon.php / turnoff.php flip it)
INSERT INTO `control_data` (`control_name`, `control_data`) VALUES ('active','1'),('last_run',''),('last_process',''),('contract_monitor_lastrun','');
