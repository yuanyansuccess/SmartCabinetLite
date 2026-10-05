-- MySQL dump 10.13  Distrib 5.7.32, for Win64 (x86_64)
--
-- Host: localhost    Database: smart_cabinet
-- ------------------------------------------------------
-- Server version	5.7.32-log

/*!40101 SET @OLD_CHARACTER_SET_CLIENT=@@CHARACTER_SET_CLIENT */;
/*!40101 SET @OLD_CHARACTER_SET_RESULTS=@@CHARACTER_SET_RESULTS */;
/*!40101 SET @OLD_COLLATION_CONNECTION=@@COLLATION_CONNECTION */;
/*!40101 SET NAMES utf8 */;
/*!40103 SET @OLD_TIME_ZONE=@@TIME_ZONE */;
/*!40103 SET TIME_ZONE='+00:00' */;
/*!40014 SET @OLD_UNIQUE_CHECKS=@@UNIQUE_CHECKS, UNIQUE_CHECKS=0 */;
/*!40014 SET @OLD_FOREIGN_KEY_CHECKS=@@FOREIGN_KEY_CHECKS, FOREIGN_KEY_CHECKS=0 */;
/*!40101 SET @OLD_SQL_MODE=@@SQL_MODE, SQL_MODE='NO_AUTO_VALUE_ON_ZERO' */;
/*!40111 SET @OLD_SQL_NOTES=@@SQL_NOTES, SQL_NOTES=0 */;

--
-- Current Database: `smart_cabinet`
--

CREATE DATABASE /*!32312 IF NOT EXISTS*/ `smart_cabinet` /*!40100 DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci */;

USE `smart_cabinet`;

--
-- Table structure for table `_borrow_overflow`
--

DROP TABLE IF EXISTS `_borrow_overflow`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `_borrow_overflow` (
  `tool_id` int(11) NOT NULL DEFAULT '0' COMMENT '工具主键ID',
  `tool_code` varchar(32) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '工具编号，如JZ01-CDQ(班组ID-规格缩写)',
  `tool_name` varchar(128) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '工具名称',
  `total_qty` int(11) NOT NULL DEFAULT '0' COMMENT '总数量',
  `total_borrowed` decimal(32,0) DEFAULT NULL,
  `overflow_qty` decimal(33,0) DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `_borrow_overflow`
--

LOCK TABLES `_borrow_overflow` WRITE;
/*!40000 ALTER TABLE `_borrow_overflow` DISABLE KEYS */;
INSERT INTO `_borrow_overflow` VALUES (1,'JZ01-CDQ','充电式电动解锥',4,5,1),(16,'JZ02-HQ','焊枪',3,4,1),(19,'JZ02-CL','游标卡尺',3,4,1);
/*!40000 ALTER TABLE `_borrow_overflow` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `cabinet_snapshot`
--

DROP TABLE IF EXISTS `cabinet_snapshot`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `cabinet_snapshot` (
  `snapshot_id` int(11) NOT NULL AUTO_INCREMENT COMMENT '快照主键ID',
  `cabinet_id` int(11) NOT NULL COMMENT '柜体ID',
  `snapshot_data` json NOT NULL COMMENT '快照数据(JSON格式，含柜内所有工具状态)',
  `snap_time` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '快照时间',
  `operator_id` int(11) DEFAULT NULL COMMENT '操作人ID',
  PRIMARY KEY (`snapshot_id`),
  KEY `idx_cabinet` (`cabinet_id`),
  KEY `idx_time` (`snap_time`),
  CONSTRAINT `fk_snapshot_cabinet` FOREIGN KEY (`cabinet_id`) REFERENCES `tool_cabinet` (`cabinet_id`) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='工具柜状态快照表 - 定时盘点数据';
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `cabinet_snapshot`
--

LOCK TABLES `cabinet_snapshot` WRITE;
/*!40000 ALTER TABLE `cabinet_snapshot` DISABLE KEYS */;
/*!40000 ALTER TABLE `cabinet_snapshot` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `face_recog_log`
--

DROP TABLE IF EXISTS `face_recog_log`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `face_recog_log` (
  `log_id` int(11) NOT NULL AUTO_INCREMENT,
  `result` varchar(16) COLLATE utf8mb4_unicode_ci NOT NULL,
  `best_sim` double DEFAULT '0',
  `best_dist` double DEFAULT '1',
  `threshold` double DEFAULT '0',
  `mode` varchar(8) COLLATE utf8mb4_unicode_ci DEFAULT '',
  `candidate_cnt` int(11) DEFAULT '0',
  `elapsed_ms` int(11) DEFAULT '0',
  `matched_user` varchar(64) COLLATE utf8mb4_unicode_ci DEFAULT '',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`log_id`),
  KEY `idx_result` (`result`),
  KEY `idx_time` (`created_at`)
) ENGINE=InnoDB AUTO_INCREMENT=234 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='人脸识别识别统计日志表';
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `face_recog_log`
--

LOCK TABLES `face_recog_log` WRITE;
/*!40000 ALTER TABLE `face_recog_log` DISABLE KEYS */;
INSERT INTO `face_recog_log` VALUES (1,'success',0.9825042197059244,0.187060312701952,0.97,'multi',2,2,'CF8888','2026-09-22 14:15:52'),(2,'success',0.9793534294775411,0.20320713827254663,0.97,'multi',2,1,'CF8888','2026-09-22 14:22:52'),(3,'rejected',0.9357890982092166,0.3583598799831905,0.97,'multi',2,2,'','2026-09-22 14:30:00'),(4,'success',0.9813318362501847,0.1932261046019187,0.97,'multi',2,1,'CF8888','2026-09-22 14:30:05'),(5,'rejected',0.9639557033215185,0.26849319052252485,0.97,'multi',2,2,'','2026-09-23 11:07:52'),(6,'success',0.9736363530234555,0.2296242451334116,0.97,'multi',2,1,'CF8888','2026-09-23 11:07:57'),(7,'rejected',0.9672687428088158,0.25585643314634143,0.97,'multi',2,2,'','2026-09-23 14:55:48'),(8,'success',0.9764281618192799,0.21712594585042205,0.97,'multi',2,1,'CF8888','2026-09-23 14:55:57'),(9,'success',0.9703424405882343,0.2435469540428116,0.97,'multi',2,1,'CF8888','2026-09-23 14:58:35'),(10,'rejected',0.950696641548072,0.31401706466982915,0.97,'multi',2,2,'','2026-09-23 15:09:10'),(11,'rejected',0.9693233383472881,0.24769603005584234,0.97,'multi',2,2,'','2026-09-23 15:09:17'),(12,'success',0.977917189505306,0.2101561823725106,0.97,'multi',2,1,'CF8888','2026-09-23 15:09:23'),(13,'success',0.9713551448770288,0.23935269007459015,0.97,'multi',2,2,'CF8888','2026-09-23 15:10:23'),(14,'success',0.9747881188348527,0.22455235988582825,0.97,'multi',2,2,'CF8888','2026-09-23 15:32:07'),(15,'success',0.975612435091506,0.22085092215562102,0.97,'multi',2,0,'CF8888','2026-09-23 15:35:44'),(16,'success',0.9761499516003258,0.2184035182851861,0.97,'multi',2,1,'CF8888','2026-09-23 15:41:38'),(17,'success',0.9792175274220123,0.20387482717583438,0.97,'multi',2,1,'CF8888','2026-09-23 15:43:19'),(18,'success',0.975685290596683,0.2205207899646514,0.97,'multi',2,2,'CF8888','2026-09-23 15:44:10'),(19,'success',0.978225209702703,0.20868536267451368,0.97,'multi',2,2,'CF8888','2026-09-23 15:44:54'),(20,'success',0.973911165850826,0.22842431634646387,0.97,'multi',2,1,'CF8888','2026-09-23 15:45:12'),(21,'rejected',0.9689149984767879,0.24933913260141383,0.97,'multi',2,2,'','2026-09-23 15:45:43'),(22,'rejected',0.9605798961244557,0.2807849849103235,0.97,'multi',2,10,'','2026-09-23 15:46:35'),(23,'rejected',0.967980926441893,0.2530575964404418,0.97,'multi',2,2,'','2026-09-23 15:46:41'),(24,'success',0.9700619645708302,0.24469587421601363,0.97,'multi',2,1,'CF8888','2026-09-23 15:46:46'),(25,'success',0.9715407036005019,0.23857617818842744,0.97,'multi',2,2,'CF8888','2026-09-23 15:46:57'),(26,'success',0.9706854097738508,0.24213463290553702,0.97,'multi',2,1,'CF8888','2026-09-23 15:47:07'),(27,'success',0.9746767825163084,0.225047628219859,0.97,'multi',2,1,'CF8888','2026-09-23 15:47:18'),(28,'rejected',0.9623829143789167,0.27428848178909443,0.97,'multi',2,2,'','2026-09-23 15:48:06'),(29,'rejected',0.9672262846668342,0.25602232454677104,0.97,'multi',2,1,'','2026-09-23 15:48:11'),(30,'success',0.9730985716316883,0.2319544281461883,0.97,'multi',2,2,'CF8888','2026-09-23 15:48:17'),(31,'rejected',0.9334508663743303,0.3648263521887359,0.97,'multi',2,2,'','2026-09-23 15:52:20'),(32,'success',0.9708934624673001,0.2412738590593692,0.97,'multi',2,1,'CF8888','2026-09-23 15:52:28'),(33,'rejected',0.9451251644300952,0.33128487912944365,0.97,'multi',2,1,'','2026-09-23 16:00:56'),(34,'rejected',0.9505264263400265,0.31455865481646994,0.97,'multi',2,1,'','2026-09-23 16:01:00'),(35,'rejected',0.9606955996749957,0.280372610377706,0.97,'multi',2,1,'','2026-09-23 16:01:15'),(36,'rejected',0.9593741487669838,0.2850468425821147,0.97,'multi',2,2,'','2026-09-23 16:01:40'),(37,'success',0.9736256779086008,0.22967072992176846,0.97,'multi',2,2,'CF8888','2026-09-23 16:01:44'),(38,'success',0.9747968658615064,0.2245134033348284,0.97,'multi',2,2,'CF8888','2026-09-23 16:02:38'),(39,'success',0.9777043642575359,0.21116645444986942,0.97,'multi',2,3,'CF8888','2026-09-23 17:42:43'),(40,'success',0.9782675759229419,0.20848224901443602,0.97,'multi',2,2,'CF8888','2026-09-23 17:42:57'),(41,'success',0.9774420563222761,0.21240500783985453,0.97,'multi',2,3,'CF8888','2026-09-23 17:43:39'),(42,'success',0.9802623771637077,0.19868378311423748,0.97,'multi',2,2,'CF8888','2026-09-23 17:43:53'),(43,'success',0.9813523345755224,0.1931199908061187,0.97,'multi',2,2,'CF8888','2026-09-23 17:48:25'),(44,'success',0.9753942421725827,0.22183668690014896,0.97,'multi',2,1,'CF8888','2026-09-23 17:55:07'),(45,'rejected',0.834589486610269,0.5751704328105386,0.97,'multi',2,3,'','2026-09-23 18:02:22'),(46,'success',0.9773722154920708,0.2127335634446492,0.97,'multi',2,5,'CF8888','2026-09-23 18:19:57'),(47,'success',0.9811357789641588,0.19423810664151786,0.97,'multi',2,2,'8888','2026-09-23 19:05:56'),(48,'success',0.9820466258259474,0.18949076058770298,0.97,'multi',2,2,'8888','2026-09-23 19:06:30'),(49,'success',0.9806454788520598,0.19674613667332927,0.97,'multi',2,2,'8888','2026-09-23 19:08:22'),(50,'success',0.9784725015345134,0.20749698053459123,0.97,'multi',2,1,'8888','2026-09-23 19:08:34'),(51,'rejected',0.9432868812973259,0.3367881194539808,0.97,'multi',2,2,'','2026-09-23 19:10:23'),(52,'success',0.9751438179808626,0.22296269651731623,0.97,'multi',2,2,'8888','2026-09-23 19:13:42'),(53,'rejected',0.9595849042605535,0.28430650973710314,0.97,'multi',2,2,'','2026-09-23 19:16:36'),(54,'rejected',0.9281411295435895,0.37910122779123495,0.97,'multi',2,2,'','2026-09-23 19:22:54'),(55,'rejected',0.9558610026060747,0.29711613013744437,0.97,'multi',2,7,'','2026-09-23 19:39:13'),(56,'rejected',0.966135299905695,0.2602487275446502,0.97,'multi',2,2,'','2026-09-23 19:40:32'),(57,'success',0.9772713749835685,0.21320705905964296,0.97,'multi',2,2,'8888','2026-09-23 19:40:44'),(58,'rejected',0.9413478013800558,0.3424972952300341,0.97,'multi',2,4,'','2026-09-23 19:46:19'),(59,'rejected',0.944051719823903,0.3345094323815021,0.97,'multi',2,2,'','2026-09-23 19:53:26'),(60,'success',0.9809275677920354,0.19530710283020744,0.97,'multi',2,2,'8888','2026-09-23 19:53:33'),(61,'success',0.9738784244319985,0.22856760736377887,0.97,'multi',2,2,'8888','2026-09-23 20:04:30'),(62,'success',0.9838971933945829,0.17945922436819525,0.97,'multi',2,2,'8888','2026-09-23 20:32:03'),(63,'success',0.9831249999303278,0.1837117310879874,0.97,'multi',2,2,'8888','2026-09-23 20:32:16'),(64,'success',0.982764681775451,0.1856626953620403,0.97,'multi',2,2,'8888','2026-09-23 20:33:04'),(65,'success',0.9789649275196629,0.2051100801049861,0.97,'multi',2,2,'8888','2026-09-23 20:39:32'),(66,'rejected',0.969889618975428,0.24539918917784587,0.97,'multi',2,3,'','2026-09-23 20:49:39'),(67,'success',0.9756054707516343,0.22088245402641685,0.97,'multi',2,1,'8888','2026-09-23 20:49:44'),(68,'success',0.9825483686420838,0.18682414917732948,0.97,'multi',2,1,'8888','2026-09-23 20:55:27'),(69,'rejected',0.9539652551810673,0.30342954641541664,0.97,'multi',2,2,'','2026-09-23 20:56:18'),(70,'rejected',0.9583698429647809,0.28854863380448936,0.97,'multi',2,2,'','2026-09-23 21:10:28'),(71,'success',0.9711092042971115,0.24037801772578543,0.97,'multi',2,1,'8888','2026-09-23 21:10:33'),(72,'success',0.97413467277087,0.22744373910543397,0.97,'multi',2,2,'8888','2026-09-23 21:12:37'),(73,'success',0.975029077757646,0.2234767202298901,0.97,'multi',2,1,'8888','2026-09-23 21:12:47'),(74,'success',0.9795839642196106,0.20206947211486287,0.97,'multi',2,2,'8888','2026-09-23 21:49:47'),(75,'rejected',0.9456041804366998,0.3298357759955709,0.97,'multi',2,1,'','2026-09-23 21:49:59'),(76,'rejected',0.9680385054669028,0.2528299607763991,0.97,'multi',2,2,'','2026-09-23 21:51:14'),(77,'success',0.9788008318718362,0.2059085628533411,0.97,'multi',2,2,'8888','2026-09-23 21:51:23'),(78,'rejected',0.9214890775241439,0.39625982000666193,0.97,'multi',2,4,'','2026-09-23 21:53:29'),(79,'success',0.9866474785930619,0.16341677641501823,0.97,'multi',2,1,'8888','2026-09-23 21:54:08'),(80,'success',0.9814937214541696,0.19238647845329807,0.97,'multi',2,3,'8888','2026-09-23 22:00:29'),(81,'rejected',0.9411788648077138,0.34299018992468666,0.97,'multi',2,2,'','2026-09-23 22:00:54'),(82,'success',0.9755984922351438,0.22091404556911196,0.97,'multi',2,2,'8888','2026-09-23 22:13:02'),(83,'success',0.9802671171605312,0.1986599246927737,0.97,'multi',2,1,'8888','2026-09-23 22:13:16'),(84,'rejected',0.9155789274760273,0.4109040582033059,0.97,'single',1,0,'','2026-09-24 10:28:12'),(85,'rejected',0.911580843125907,0.42052147834347897,0.97,'single',1,0,'','2026-09-24 10:28:17'),(86,'rejected',0.8855651705330346,0.4784032388413893,0.97,'single',1,0,'','2026-09-24 10:40:55'),(87,'rejected',0.9531907783444028,0.3059713112551475,0.97,'single',1,0,'','2026-09-24 10:41:00'),(88,'rejected',0.9225366588634907,0.3936072690805133,0.97,'single',1,0,'','2026-09-24 10:41:20'),(89,'rejected',0.9461341351526679,0.32822512045037683,0.97,'single',1,0,'','2026-09-24 10:41:25'),(90,'rejected',0.9381212112283979,0.3517919520728185,0.97,'single',1,0,'','2026-09-24 10:41:45'),(91,'rejected',0.9462644823172083,0.32782775258599345,0.97,'single',1,0,'','2026-09-24 10:41:50'),(92,'rejected',0.9361137100358298,0.35745290588879125,0.97,'single',1,0,'','2026-09-24 10:45:38'),(93,'rejected',0.9378260383085781,0.35263000919213455,0.97,'single',1,0,'','2026-09-24 10:46:28'),(94,'rejected',0.9355005434350504,0.3591641868698768,0.97,'single',1,0,'','2026-09-24 10:47:12'),(95,'rejected',0.9468496618441231,0.3260378449072347,0.97,'single',1,0,'','2026-09-24 10:47:18'),(96,'rejected',0.911690262682395,0.4202611981080468,0.97,'single',1,0,'','2026-09-24 10:47:38'),(97,'rejected',0.9249782400646376,0.3873545144576545,0.97,'single',1,0,'','2026-09-24 10:47:43'),(98,'rejected',0.941944209448041,0.34075149464663856,0.97,'single',1,0,'','2026-09-24 10:48:03'),(99,'rejected',0.9470837326027662,0.32531912761850995,0.97,'single',1,0,'','2026-09-24 10:48:08'),(100,'rejected',0.9125389364485126,0.41823692699590254,0.97,'single',1,1,'','2026-09-24 10:52:23'),(101,'success',0.9708852618580015,0.24130784546714487,0.97,'multi',2,1,'8888','2026-09-24 10:57:52'),(102,'rejected',0.8875676034004537,0.47419910712599656,0.97,'multi',2,2,'','2026-09-24 11:02:50'),(103,'success',0.9855978304500135,0.16971841119917425,0.97,'multi',2,1,'8888','2026-09-24 11:02:56'),(104,'success',0.9823624201749177,0.18781682472601477,0.97,'multi',2,1,'8888','2026-09-24 11:17:13'),(105,'rejected',0.9010499725127646,0.44485959017927157,0.97,'multi',2,2,'','2026-09-24 11:21:32'),(106,'success',0.97243597383964,0.234793637734755,0.97,'multi',2,2,'8888','2026-09-24 11:21:39'),(107,'rejected',0.9530071590291681,0.30657084326736556,0.97,'multi',2,3,'','2026-09-24 11:22:24'),(108,'success',0.9802980693478397,0.19850405865956383,0.97,'multi',2,2,'8888','2026-09-24 11:25:35'),(109,'rejected',0.8802594645133393,0.4893680322347598,0.97,'multi',2,1,'','2026-09-24 11:31:08'),(110,'rejected',0.903736481732735,0.43877902927843876,0.97,'multi',2,3,'','2026-09-24 11:32:06'),(111,'rejected',0.9120189673357548,0.4194783252189436,0.97,'multi',2,3,'','2026-09-24 11:32:45'),(112,'success',0.9802387335318689,0.198802748814654,0.97,'multi',2,4,'8888','2026-09-24 11:34:21'),(113,'success',0.983641956554764,0.18087588808481547,0.97,'multi',2,1,'8888','2026-09-24 12:12:21'),(114,'rejected',0.9695198390273957,0.2469014417641354,0.97,'multi',2,4,'','2026-09-24 12:19:36'),(115,'rejected',0.9516574203638093,0.3109423729123798,0.97,'multi',2,2,'','2026-09-26 14:25:48'),(116,'success',0.9824123874270494,0.187550593563179,0.97,'multi',2,2,'8888','2026-09-26 14:25:54'),(117,'rejected',0.9601032045830759,0.2824775935076072,0.97,'multi',2,1,'','2026-10-02 21:51:50'),(118,'success',0.9704631287157469,0.2430509053027908,0.97,'multi',2,1,'8888','2026-10-02 21:51:55'),(119,'rejected',0.9592392196631045,0.28551980784840547,0.97,'multi',2,2,'','2026-10-02 21:53:22'),(120,'rejected',0.9677085860930752,0.2541315167661234,0.97,'multi',2,1,'','2026-10-02 21:53:27'),(121,'success',0.9726031241319643,0.23408065220361882,0.97,'multi',2,1,'8888','2026-10-02 21:53:33'),(122,'success',0.9742225154411703,0.22705719349463135,0.97,'multi',2,2,'8888','2026-10-02 21:54:12'),(123,'rejected',0.9628558768872494,0.27255870234776997,0.97,'multi',2,1,'','2026-10-02 21:54:53'),(124,'success',0.9728753256630438,0.2329148957750694,0.97,'multi',2,1,'8888','2026-10-02 21:54:59'),(125,'rejected',0.9549436439005516,0.3001877948866289,0.97,'multi',2,1,'','2026-10-02 22:07:50'),(126,'success',0.9721630071530278,0.23595335491139774,0.97,'multi',2,1,'8888','2026-10-02 22:09:03'),(127,'rejected',0.9691740822762441,0.24829787644583676,0.97,'multi',2,1,'','2026-10-02 22:11:04'),(128,'rejected',0.8658597959269981,0.517957921211756,0.97,'multi',2,1,'','2026-10-02 22:12:23'),(129,'rejected',0.8866050548173772,0.4762246217545317,0.97,'multi',2,1,'','2026-10-02 22:12:38'),(130,'rejected',0.9518560424567822,0.31030294082788634,0.97,'multi',2,2,'','2026-10-03 06:21:43'),(131,'rejected',0.9692689540314944,0.247915493539652,0.97,'multi',2,3,'','2026-10-03 06:21:49'),(132,'rejected',0.9617133473073084,0.2767188200780399,0.97,'multi',2,5,'','2026-10-03 06:21:56'),(133,'rejected',0.9658316072543971,0.26141305531898335,0.97,'multi',2,1,'','2026-10-03 11:46:58'),(134,'rejected',0.9669817174988267,0.2569758062587718,0.97,'multi',2,1,'','2026-10-03 11:47:02'),(135,'rejected',0.9625009083959291,0.2738579617395484,0.97,'multi',2,3,'','2026-10-03 11:55:50'),(136,'success',0.9789766439558569,0.20505294947472905,0.97,'multi',2,1,'8888','2026-10-03 11:55:54'),(137,'rejected',0.9692354886657546,0.2480504437982149,0.97,'multi',2,1,'','2026-10-03 12:03:39'),(138,'success',0.9775989208234597,0.21166520345366271,0.97,'multi',2,1,'8888','2026-10-03 12:03:46'),(139,'success',0.9803567278960174,0.19820833536449667,0.97,'multi',2,1,'8888','2026-10-03 21:38:51'),(140,'rejected',0.8852381793954154,0.47908625654381864,0.97,'multi',2,1,'','2026-10-03 22:02:29'),(141,'success',0.98177156893903,0.1909368013818734,0.97,'multi',2,1,'8888','2026-10-03 22:02:36'),(142,'rejected',0.9228628470486742,0.3927776799955075,0.97,'single',1,4,'','2026-10-03 22:19:34'),(143,'rejected',0.9026379002613184,0.44127565022031834,0.97,'single',1,1,'','2026-10-03 22:19:40'),(144,'rejected',0.9318832890995136,0.36909811947634363,0.97,'single',1,1,'','2026-10-03 22:19:44'),(145,'rejected',0.9440285883550513,0.33457857565883875,0.97,'single',1,2,'','2026-10-03 22:25:00'),(146,'rejected',0.9291306632697287,0.37648196963539055,0.97,'single',1,1,'','2026-10-03 22:28:54'),(147,'success',0.9820533846247415,0.18945508900664673,0.97,'multi',2,2,'8888','2026-10-03 22:31:48'),(148,'success',0.9738910762662871,0.22851224795932878,0.97,'multi',2,1,'8888','2026-10-03 22:40:34'),(149,'success',0.974284200468033,0.22678535901581698,0.97,'multi',2,3,'8888','2026-10-03 22:40:48'),(150,'rejected',0.959493548275398,0.28462765756194985,0.97,'multi',2,3,'','2026-10-03 22:47:41'),(151,'rejected',0.9673782736212155,0.25542797959027463,0.97,'multi',2,1,'','2026-10-03 22:47:46'),(152,'success',0.9842959422572917,0.17722334915415586,0.97,'multi',2,1,'8888','2026-10-03 22:47:51'),(153,'success',0.9733074413157379,0.23105219619931083,0.97,'multi',2,2,'8888','2026-10-03 22:48:16'),(154,'success',0.9795186174110884,0.2023926015886529,0.97,'multi',2,4,'8888','2026-10-03 22:48:42'),(155,'success',0.9802838645135034,0.19857560518098105,0.97,'multi',2,1,'8888','2026-10-03 22:55:16'),(156,'success',0.9714896587475298,0.23879003853791883,0.97,'multi',2,2,'8888','2026-10-03 23:04:46'),(157,'success',0.9748271384395755,0.22437852642543368,0.97,'multi',2,1,'8888','2026-10-03 23:05:03'),(158,'success',0.9777234275148874,0.21107615917063025,0.97,'multi',2,4,'8888','2026-10-03 23:30:48'),(159,'success',0.9825340507520453,0.18690077179056802,0.97,'multi',2,2,'8888','2026-10-03 23:44:57'),(160,'success',0.981622397843788,0.19171646854775967,0.97,'multi',2,2,'8888','2026-10-03 23:45:14'),(161,'rejected',0.9687318179630411,0.2500727175721453,0.97,'multi',2,3,'','2026-10-04 07:37:44'),(162,'success',0.9807421957387931,0.1962539388710784,0.97,'multi',2,2,'8888','2026-10-04 07:37:49'),(163,'success',0.9794377395946837,0.20279181642914787,0.97,'multi',2,3,'8888','2026-10-04 07:45:24'),(164,'rejected',0.9505483325178117,0.31448900611051095,0.97,'multi',2,2,'','2026-10-04 08:02:52'),(165,'rejected',0.969291765196519,0.24782346460123847,0.97,'multi',2,2,'','2026-10-04 08:04:58'),(166,'success',0.9740375131509874,0.22787051958957882,0.97,'multi',2,2,'8888','2026-10-04 08:05:05'),(167,'rejected',0.9638109269791612,0.26903186807826046,0.97,'multi',2,2,'','2026-10-04 08:05:42'),(168,'success',0.9777355425201637,0.21101875499507655,0.97,'multi',2,2,'8888','2026-10-04 08:05:48'),(169,'rejected',0.9575582071510668,0.29134787745557056,0.97,'multi',2,2,'','2026-10-04 08:11:42'),(170,'success',0.9756184269211707,0.22082378983628234,0.97,'multi',2,1,'8888','2026-10-04 08:11:48'),(171,'rejected',0.9640224066592449,0.2682446396137491,0.97,'multi',2,1,'','2026-10-04 08:15:28'),(172,'rejected',0.9695571156373226,0.24675041788283622,0.97,'multi',2,2,'','2026-10-04 08:20:41'),(173,'rejected',0.9329797369607129,0.36611545457488454,0.97,'multi',2,2,'','2026-10-04 08:20:57'),(174,'success',0.9844793751858408,0.17618527074735274,0.97,'multi',2,2,'8888','2026-10-04 08:21:03'),(175,'success',0.9761739860682578,0.21829344438962264,0.97,'multi',2,2,'8888','2026-10-04 08:29:32'),(176,'rejected',0.9602694300234758,0.2818885239825286,0.97,'multi',2,1,'','2026-10-04 08:29:46'),(177,'success',0.9756647920273366,0.22061372565034626,0.97,'multi',2,1,'8888','2026-10-04 08:29:53'),(178,'success',0.9818782470907961,0.19037727232631507,0.97,'multi',2,1,'8888','2026-10-04 08:30:08'),(179,'success',0.9761013363349706,0.218625998751424,0.97,'multi',2,1,'8888','2026-10-04 08:37:04'),(180,'success',0.980509066104875,0.19743826323752567,0.97,'multi',2,1,'8888','2026-10-04 08:37:17'),(181,'success',0.9777379907740995,0.21100715260815545,0.97,'multi',2,1,'8888','2026-10-04 08:37:41'),(182,'rejected',0.9442543812167606,0.3339030361743943,0.97,'multi',2,1,'','2026-10-04 08:52:38'),(183,'rejected',0.9692350874937934,0.24805206109285044,0.97,'multi',2,2,'','2026-10-04 08:57:47'),(184,'success',0.9774948430329297,0.21215634313906537,0.97,'multi',2,1,'8888','2026-10-04 08:58:12'),(185,'success',0.9785680872158796,0.20703580745426647,0.97,'multi',2,2,'8888','2026-10-04 08:58:27'),(186,'success',0.9806878523270129,0.196530647345329,0.97,'multi',2,2,'8888','2026-10-04 08:58:40'),(187,'rejected',0.9638553928156895,0.2688665363495815,0.97,'multi',2,2,'','2026-10-04 09:00:22'),(188,'rejected',0.9624979314060225,0.27386883208564483,0.97,'multi',2,2,'','2026-10-04 09:00:27'),(189,'rejected',0.958288914338769,0.28882896551845666,0.97,'multi',2,2,'','2026-10-04 09:00:31'),(190,'success',0.9812125350014925,0.19384253918326275,0.97,'multi',2,2,'8888','2026-10-04 09:00:36'),(191,'success',0.9867090296785995,0.1630396903910267,0.97,'multi',2,2,'8888','2026-10-04 09:00:50'),(192,'success',0.9765235800970627,0.2166860397115461,0.97,'multi',2,2,'8888','2026-10-04 09:09:04'),(193,'success',0.9707160234104638,0.24200816758752938,0.97,'multi',2,1,'8888','2026-10-04 09:09:11'),(194,'success',0.9703036629928613,0.24370612223388521,0.97,'multi',2,2,'8888','2026-10-04 09:09:33'),(195,'success',0.9731244884230968,0.2318426689671365,0.97,'multi',2,1,'8888','2026-10-04 09:10:34'),(196,'rejected',0.9682439373959859,0.25201612092885545,0.97,'multi',2,2,'','2026-10-04 09:10:49'),(197,'rejected',0.9641030989102997,0.26794365485937616,0.97,'multi',2,2,'','2026-10-04 09:10:54'),(198,'success',0.9716833931103313,0.2379773387937119,0.97,'multi',2,1,'8888','2026-10-04 09:10:59'),(199,'rejected',0.9675213140166522,0.25486736151711353,0.97,'multi',2,2,'','2026-10-04 09:14:18'),(200,'success',0.9740022870496312,0.22802505542316182,0.97,'multi',2,1,'8888','2026-10-04 09:14:24'),(201,'success',0.9715817217878673,0.2384041870946606,0.97,'multi',2,1,'8888','2026-10-04 09:14:36'),(202,'success',0.9713846823457857,0.2392292526185483,0.97,'multi',2,2,'8888','2026-10-04 09:14:51'),(203,'success',0.9829911653607232,0.18443879548119343,0.97,'multi',2,1,'8888','2026-10-04 09:15:06'),(204,'success',0.9707262143591961,0.24196605398610646,0.97,'multi',2,1,'8888','2026-10-04 09:15:14'),(205,'success',0.9795015248890324,0.20247703628296998,0.97,'multi',2,1,'8888','2026-10-04 09:15:27'),(206,'rejected',0.9662071773491642,0.25997239334527833,0.97,'multi',2,1,'','2026-10-04 09:15:42'),(207,'rejected',0.9691868349841014,0.24824651061353756,0.97,'multi',2,1,'','2026-10-04 09:27:07'),(208,'success',0.9760304083183797,0.21895018466135174,0.97,'multi',2,1,'8888','2026-10-04 09:27:12'),(209,'success',0.9729165477622508,0.23273784495757982,0.97,'multi',2,1,'8888','2026-10-04 09:27:24'),(210,'rejected',0.9596131117989531,0.2842072771800411,0.97,'multi',2,2,'','2026-10-04 09:43:26'),(211,'success',0.977815200296663,0.21064092528915979,0.97,'multi',2,2,'8888','2026-10-04 09:43:33'),(212,'success',0.971055062428238,0.24060314865671126,0.97,'multi',2,2,'8888','2026-10-04 12:23:31'),(213,'rejected',0.9240402368507148,0.38976855478420985,0.97,'multi',2,2,'','2026-10-04 12:23:45'),(214,'rejected',0.9161077573952829,0.40961504514535996,0.97,'multi',2,1,'','2026-10-04 12:23:50'),(215,'rejected',0.8426548012251047,0.5609727244258765,0.97,'multi',2,1,'','2026-10-04 12:24:16'),(216,'rejected',0.8648274768455683,0.519947157227408,0.97,'multi',2,1,'','2026-10-04 12:24:24'),(217,'success',0.9730770480756529,0.23204720176872326,0.97,'multi',2,1,'8888','2026-10-04 12:24:32'),(218,'success',0.9739981934880142,0.22804300696134533,0.97,'multi',2,0,'8888','2026-10-05 06:49:01'),(219,'success',0.9761215828234128,0.21853337125751562,0.97,'multi',2,1,'8888','2026-10-05 07:30:03'),(220,'success',0.9712379380579766,0.23984187266623894,0.97,'multi',2,1,'8888','2026-10-05 07:47:28'),(221,'success',0.9800568567894863,0.1997155137214621,0.97,'multi',2,2,'8888','2026-10-05 08:37:48'),(222,'rejected',0.9569527577953166,0.2934186163306035,0.97,'multi',2,2,'','2026-10-05 08:53:03'),(223,'success',0.9714699755629966,0.23887245315022468,0.97,'multi',2,1,'8888','2026-10-05 08:53:08'),(224,'success',0.9720717227258383,0.236339913151213,0.97,'multi',2,1,'8888','2026-10-05 09:55:28'),(225,'rejected',0.9349027736556516,0.3608246841454953,0.97,'multi',2,1,'','2026-10-05 10:16:31'),(226,'rejected',0.9683058115326691,0.25177048463761925,0.97,'multi',2,1,'','2026-10-05 10:16:36'),(227,'success',0.973422135114225,0.23055526402914692,0.97,'multi',2,2,'8888','2026-10-05 10:16:40'),(228,'rejected',0.9691859708500219,0.2482499915406974,0.97,'multi',2,2,'','2026-10-05 10:33:08'),(229,'rejected',0.9679720160248549,0.2530928050148597,0.97,'multi',2,1,'','2026-10-05 10:33:12'),(230,'success',0.9718648889069074,0.23721345279344155,0.97,'multi',2,1,'8888','2026-10-05 10:33:17'),(231,'rejected',0.8770212931999111,0.4959409376127131,0.97,'multi',2,3,'','2026-10-05 10:38:35'),(232,'rejected',0.9626794849259809,0.27320510637255185,0.97,'multi',2,1,'','2026-10-05 10:42:04'),(233,'success',0.9772905682283217,0.21311701842733444,0.97,'multi',2,2,'8888','2026-10-05 10:42:09');
/*!40000 ALTER TABLE `face_recog_log` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `machine_group`
--

DROP TABLE IF EXISTS `machine_group`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `machine_group` (
  `group_id` int(11) NOT NULL AUTO_INCREMENT COMMENT '机组ID',
  `group_name` varchar(64) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '机组名称',
  `dept_id` int(11) DEFAULT NULL COMMENT '所属部门ID',
  `leader_name` varchar(32) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '负责人姓名',
  `leader_phone` varchar(20) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '负责人电话',
  `description` varchar(256) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '机组描述',
  `status` varchar(16) COLLATE utf8mb4_unicode_ci DEFAULT 'active' COMMENT 'active|inactive',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`group_id`),
  UNIQUE KEY `uk_group_name` (`group_name`),
  KEY `idx_dept` (`dept_id`),
  KEY `idx_status` (`status`),
  CONSTRAINT `fk_mg_dept` FOREIGN KEY (`dept_id`) REFERENCES `sys_department` (`dept_id`) ON DELETE SET NULL ON UPDATE CASCADE
) ENGINE=InnoDB AUTO_INCREMENT=9 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='工程机组表';
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `machine_group`
--

LOCK TABLES `machine_group` WRITE;
/*!40000 ALTER TABLE `machine_group` DISABLE KEYS */;
INSERT INTO `machine_group` VALUES (1,'发动机维护机组',1,'张三','138****6789','负责发动机拆装、检查、更换部件等核心维护作业','active','2026-06-24 16:18:05','2026-06-24 16:18:05'),(2,'航电检修机组',2,'李四','139****8901','负责航空电子设备、仪表、通信导航系统检测维修','active','2026-06-24 16:18:05','2026-06-24 16:18:05'),(3,'液压系统机组',3,'王五','137****2345','负责液压管路、泵阀、作动筒检查与更换','active','2026-06-24 16:18:05','2026-06-24 16:18:05'),(4,'结构修理机组',4,'赵六','136****7890','负责机身蒙皮、框架、紧固件损伤修复','active','2026-06-24 16:18:05','2026-06-24 16:18:05'),(5,'起落架维护机组',1,'周八','133****9012','负责起落架减震、刹车系统、轮胎更换','active','2026-06-24 16:18:05','2026-06-24 16:18:05'),(6,'电气线路机组',2,'孙七','135****3456','负责线路故障排查、线束修复、接插件更换','active','2026-06-24 16:18:05','2026-06-24 16:18:05'),(7,'焊接作业机组',3,'李四','139****8901','负责金属结构焊接、修补、热处理','active','2026-06-24 16:18:05','2026-06-24 16:18:05'),(8,'精密测量机组',4,'赵六','136****7890','负责三坐标测量、形位公差检测、校准','active','2026-06-24 16:18:05','2026-06-24 16:18:05');
/*!40000 ALTER TABLE `machine_group` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `sys_alert`
--

DROP TABLE IF EXISTS `sys_alert`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `sys_alert` (
  `alert_id` int(11) NOT NULL AUTO_INCREMENT COMMENT '告警主键ID',
  `type_id` int(11) DEFAULT NULL,
  `alert_type` varchar(32) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '告警类型：overdue_return逾期未还/wrong_position位置异常/timeout超时/rfid_error标签异常/low_stock库存不足等',
  `alert_level` varchar(8) COLLATE utf8mb4_unicode_ci NOT NULL DEFAULT 'warn' COMMENT '告警级别：info提示 warn警告 err严重',
  `tool_id` int(11) DEFAULT NULL COMMENT '关联工具ID',
  `tool_code` varchar(32) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '工具编号冗余字段',
  `content` varchar(512) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '告警内容描述',
  `status` varchar(16) COLLATE utf8mb4_unicode_ci DEFAULT 'unhandled' COMMENT '状态：unhandled未处理 handled已处理',
  `user_id` int(11) DEFAULT NULL COMMENT '借用人ID [V1.00.8]',
  `created_at` datetime DEFAULT CURRENT_TIMESTAMP COMMENT '告警时间',
  `handled_at` datetime DEFAULT NULL COMMENT '处理时间',
  `handler_id` int(11) DEFAULT NULL COMMENT '处理人ID',
  `remark` varchar(256) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '处理备注',
  `record_id` int(11) DEFAULT '0',
  PRIMARY KEY (`alert_id`),
  KEY `idx_type` (`alert_type`),
  KEY `idx_level` (`alert_level`),
  KEY `idx_status` (`status`),
  KEY `idx_time` (`created_at`),
  KEY `idx_tool` (`tool_id`),
  KEY `fk_alert_handler` (`handler_id`),
  KEY `idx_user` (`user_id`),
  KEY `idx_type_id` (`type_id`),
  CONSTRAINT `fk_alert_handler` FOREIGN KEY (`handler_id`) REFERENCES `sys_user` (`user_id`) ON DELETE SET NULL ON UPDATE CASCADE,
  CONSTRAINT `fk_alert_tool` FOREIGN KEY (`tool_id`) REFERENCES `tool_info` (`tool_id`) ON DELETE SET NULL ON UPDATE CASCADE,
  CONSTRAINT `fk_alert_user` FOREIGN KEY (`user_id`) REFERENCES `sys_user` (`user_id`) ON DELETE SET NULL ON UPDATE CASCADE
) ENGINE=InnoDB AUTO_INCREMENT=74 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='系统告警表 - 异常事件管理';
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `sys_alert`
--

LOCK TABLES `sys_alert` WRITE;
/*!40000 ALTER TABLE `sys_alert` DISABLE KEYS */;
INSERT INTO `sys_alert` VALUES (40,2,'mismatch','warn',NULL,'JZ01-BX','保险丝钳(JZ01-BX)被放置到错误货位(应在A柜01层15位，实际在03层04位)','unhandled',4,'2026-06-24 14:00:00',NULL,NULL,NULL,0),(41,2,'mismatch','warn',NULL,'JZ01-PH2','十字解锥头2#(JZ01-PH2) RFID检测位置异常，需人工归位','unhandled',4,'2026-06-25 16:30:00',NULL,NULL,NULL,0),(42,2,'mismatch','warn',NULL,'JZ01-NLJ','内六角扳手柜位不匹配（检测到B柜02层，应在A柜）','handled',5,'2026-06-22 11:20:45','2026-06-22 15:00:00',1,'已归位到正确货位',0),(62,2,'mismatch','warn',NULL,NULL,'入库校验异常：A柜-11位置传感器未检测到工具放置，用户忽略告警','unhandled',7,'2026-06-30 09:51:20',NULL,NULL,'',0),(63,2,'mismatch','warn',NULL,NULL,'工具核对异常：A-01-05位置工具未正确放入柜位，用户忽略告警','ignored',7,'2026-06-30 10:07:16','2026-07-04 17:32:00',NULL,'',0),(64,2,'mismatch','warn',NULL,NULL,'入库校验异常：C柜-1位置传感器未检测到工具放置，用户忽略告警','ignored',7,'2026-06-30 10:50:49','2026-06-30 11:28:30',NULL,'',0),(65,2,'mismatch','warn',2,'JZ01-KB','工具核对异常：A-001-005位置工具RFID标签与清单不符，用户忽略告警或倒计时超时','unhandled',7,'2026-07-04 22:36:48',NULL,NULL,'',0),(66,2,'mismatch','warn',2,'JZ01-KB','工具核对异常：A-001-005位置工具RFID标签与清单不符，用户忽略告警或倒计时超时','unhandled',7,'2026-07-04 22:38:00',NULL,NULL,'',0),(67,2,'mismatch','warn',NULL,NULL,'工具核对异常：A-006-14位置工具未正确放入柜位，用户忽略告警或倒计时超时','unhandled',7,'2026-07-07 11:25:43',NULL,NULL,'',0),(68,17,'hw_comm','error',NULL,'','1号柜视频摄像头通讯中断(心跳丢失3次)，人脸识别功能暂不可用，请检查摄像头线路','unhandled',NULL,'2026-06-26 09:15:00',NULL,NULL,'',0),(69,17,'hw_comm','error',NULL,'','2号柜IO板卡通讯超时(串口无响应)，工具位在柜检测暂停','unhandled',NULL,'2026-06-26 10:40:00',NULL,NULL,'',0),(70,17,'hw_comm','error',NULL,'','1号柜视频摄像头通讯恢复，已自动重连成功','handled',NULL,'2026-06-25 16:20:00',NULL,NULL,'',0),(71,18,'hw_fault','error',NULL,'','3号柜电源电压异常(10.8V)，低于正常工作范围(11.5-12.5V)，请检查供电线路','unhandled',NULL,'2026-06-26 11:05:00',NULL,NULL,'',0),(72,18,'hw_fault','warn',NULL,'','2号柜内部温度达到39.2°C，超出安全范围(25-35°C)，散热风扇已自动启动','unhandled',NULL,'2026-06-26 13:30:00',NULL,NULL,'',0),(73,18,'hw_fault','error',NULL,'','A-02-03位工具检测传感器无响应，现场排查为传感器接插件松动，已复位','handled',NULL,'2026-06-24 15:50:00',NULL,NULL,'',0);
/*!40000 ALTER TABLE `sys_alert` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `sys_alert_type`
--

DROP TABLE IF EXISTS `sys_alert_type`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `sys_alert_type` (
  `type_id` int(11) NOT NULL AUTO_INCREMENT,
  `type_code` varchar(32) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '类型编码',
  `type_name` varchar(32) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '类型显示名',
  `alert_level` varchar(8) COLLATE utf8mb4_unicode_ci NOT NULL DEFAULT 'warn' COMMENT '默认级别:info|warn|error',
  `sort_order` int(11) NOT NULL DEFAULT '0' COMMENT '排序',
  `is_active` tinyint(4) NOT NULL DEFAULT '1' COMMENT '1=启用 0=禁用',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`type_id`),
  UNIQUE KEY `uk_type_code` (`type_code`),
  KEY `idx_active` (`is_active`)
) ENGINE=InnoDB AUTO_INCREMENT=19 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='告警类型字典表';
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `sys_alert_type`
--

LOCK TABLES `sys_alert_type` WRITE;
/*!40000 ALTER TABLE `sys_alert_type` DISABLE KEYS */;
INSERT INTO `sys_alert_type` VALUES (1,'overdue','逾期未还','warn',1,0,'2026-10-05 07:47:12'),(2,'mismatch','工具错放','warn',2,1,'2026-10-05 07:47:12'),(3,'missing','工具缺失','error',3,0,'2026-10-05 07:47:12'),(4,'offline','柜门异常','error',4,0,'2026-10-05 07:47:12'),(5,'unauthorized','未授权操作','error',5,0,'2026-10-05 07:47:12'),(6,'low_stock','库存不足','info',6,0,'2026-10-05 07:47:12'),(7,'system','系统异常','info',7,0,'2026-10-05 07:47:12'),(8,'power','电源异常','error',8,0,'2026-10-05 07:47:12'),(9,'network_error','网络故障','error',9,0,'2026-10-05 07:47:12'),(10,'door_open','柜门未关','warn',10,0,'2026-10-05 07:47:12'),(11,'stranger','陌生人告警','warn',11,0,'2026-10-05 07:47:12'),(12,'rack_mismatch','货架错放','warn',12,0,'2026-10-05 07:47:12'),(13,'temp_high','温度过高','warn',13,0,'2026-10-05 07:47:12'),(14,'power_low','电量不足','warn',14,0,'2026-10-05 07:47:12'),(15,'sensor_fail','传感器故障','error',15,0,'2026-10-05 07:47:12'),(16,'login_fail','登录失败','warn',16,0,'2026-10-05 07:47:12'),(17,'hw_comm','硬件通讯故障','error',17,1,'2026-10-05 07:47:12'),(18,'hw_fault','系统硬件故障','error',18,1,'2026-10-05 07:47:12');
/*!40000 ALTER TABLE `sys_alert_type` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `sys_department`
--

DROP TABLE IF EXISTS `sys_department`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `sys_department` (
  `dept_id` int(11) NOT NULL AUTO_INCREMENT COMMENT '部门主键ID',
  `dept_name` varchar(64) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '部门名称',
  `parent_id` int(11) DEFAULT '0' COMMENT '上级部门ID，0表示顶级',
  `sort_order` int(11) DEFAULT '0' COMMENT '排序权重，越小越靠前',
  `status` tinyint(4) DEFAULT '1' COMMENT '状态：1启用 0禁用',
  `created_at` datetime DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
  `updated_at` datetime DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
  PRIMARY KEY (`dept_id`),
  KEY `idx_parent` (`parent_id`),
  KEY `idx_sort` (`sort_order`)
) ENGINE=InnoDB AUTO_INCREMENT=6 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='部门组织架构表';
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `sys_department`
--

LOCK TABLES `sys_department` WRITE;
/*!40000 ALTER TABLE `sys_department` DISABLE KEYS */;
INSERT INTO `sys_department` VALUES (1,'技术部',0,1,1,'2026-06-03 10:46:32','2026-06-03 10:46:32'),(2,'维修一组',0,2,1,'2026-06-03 10:46:32','2026-06-03 10:46:32'),(3,'维修二组',0,3,1,'2026-06-03 10:46:32','2026-06-03 10:46:32'),(4,'维修三组',0,4,1,'2026-06-03 10:46:32','2026-06-03 10:46:32'),(5,'质检部',0,5,1,'2026-06-03 10:46:32','2026-06-03 10:46:32');
/*!40000 ALTER TABLE `sys_department` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `sys_operation_log`
--

DROP TABLE IF EXISTS `sys_operation_log`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `sys_operation_log` (
  `log_id` int(11) NOT NULL AUTO_INCREMENT COMMENT '日志主键ID',
  `user_id` int(11) DEFAULT NULL COMMENT '操作人ID',
  `username` varchar(64) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '操作人用户名冗余',
  `operation_type` varchar(32) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '操作类型：login/logout/borrow/return/checkin/checkout/create_user/update_user/delete_user等',
  `target_type` varchar(32) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '操作目标类型：user/tool/record等',
  `target_id` varchar(64) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '操作目标ID',
  `content` text COLLATE utf8mb4_unicode_ci COMMENT '操作详情(JSON格式，记录完整操作参数与结果)',
  `ip_address` varchar(45) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '操作来源IP',
  `result` varchar(16) COLLATE utf8mb4_unicode_ci DEFAULT 'success' COMMENT '操作结果：success成功 fail失败',
  `error_msg` text COLLATE utf8mb4_unicode_ci COMMENT '失败时的错误信息',
  `created_at` datetime DEFAULT CURRENT_TIMESTAMP COMMENT '操作时间',
  PRIMARY KEY (`log_id`),
  KEY `idx_user` (`user_id`),
  KEY `idx_type` (`operation_type`),
  KEY `idx_time` (`created_at`),
  KEY `idx_target` (`target_type`,`target_id`)
) ENGINE=InnoDB AUTO_INCREMENT=730 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='系统操作日志表 - 审计追踪不可篡改';
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `sys_operation_log`
--

LOCK TABLES `sys_operation_log` WRITE;
/*!40000 ALTER TABLE `sys_operation_log` DISABLE KEYS */;
INSERT INTO `sys_operation_log` VALUES (546,1,'张三','login','','','管理员张三登录系统','','success',NULL,'2026-06-13 08:30:00'),(547,2,'李四','login','','','用户李四通过人脸识别登录','','success',NULL,'2026-06-13 08:35:00'),(548,3,'王五','borrow','','','王五借用工具:充电式电动解锥','','success',NULL,'2026-06-12 09:00:00'),(549,4,'赵六','return','','','赵六归还工具:内六角扳手','','success',NULL,'2026-06-12 09:30:00'),(550,5,'孙七','borrow','','','孙七借用工具:游标卡尺','','success',NULL,'2026-06-12 11:00:00'),(551,6,'周八','login','','','用户周八登录系统','','success',NULL,'2026-06-12 13:00:00'),(552,1,'张三','add_user','','','管理员新增测试用户','','success',NULL,'2026-06-12 10:00:00'),(553,1,'张三','edit_user','','','管理员编辑用户:赵六信息','','success',NULL,'2026-06-12 10:15:00'),(554,2,'李四','borrow','','','李四借用工具:热风枪','','success',NULL,'2026-06-12 14:00:00'),(555,3,'王五','return','','','王五归还工具:9#开口扳手','','success',NULL,'2026-06-11 09:00:00'),(556,1,'张三','disable_user','','','管理员禁用用户:孙七','','success',NULL,'2026-06-11 10:00:00'),(557,2,'李四','return','','','李四归还工具:万用表','','success',NULL,'2026-06-11 10:30:00'),(558,3,'王五','borrow','','','王五借用工具:锤子','','success',NULL,'2026-06-11 11:00:00'),(559,6,'周八','borrow','','','周八借用工具:焊枪','','success',NULL,'2026-06-11 14:00:00'),(560,1,'张三','export','','','管理员导出本月借用记录','','success',NULL,'2026-06-09 16:00:00'),(561,1,'','checkout','tool','JZ01-CDQ','出库工具「充电式电动解锥」编号[JZ01-CDQ]×2，原因：报废更换','127.0.0.1','success',NULL,'2026-06-20 09:15:00'),(562,2,'','checkout','tool','JZ01-NLJ','出库工具「内六角扳手」编号[JZ01-NLJ]×1，原因：损坏退役','127.0.0.1','success',NULL,'2026-06-19 14:30:00'),(563,3,'','checkout','tool','JZ02-CL','出库工具「游标卡尺」编号[JZ02-CL]×1，原因：调拨其他机组','127.0.0.1','success',NULL,'2026-06-18 10:00:00'),(564,1,'','checkout','tool','JZ03-RF','出库工具「热风枪」编号[JZ03-RF]×3，原因：升级替换','127.0.0.1','success',NULL,'2026-06-17 15:45:00'),(565,2,'','checkout','tool','JZ01-KB','出库工具「9# 开口扳手」编号[JZ01-KB]×2，原因：超期淘汰','127.0.0.1','success',NULL,'2026-06-16 11:20:00'),(566,3,'','checkout','tool','JZ01-WY','出库工具「万用表」编号[JZ01-WY]×1，原因：其他原因','127.0.0.1','success',NULL,'2026-06-15 16:00:00'),(567,1,'','checkout','tool','JZ01-CZ','出库工具「锤子」编号[JZ01-CZ]×1，原因：损坏退役','127.0.0.1','success',NULL,'2026-06-14 09:30:00'),(568,2,'','checkout','tool','JZ02-HQ','出库工具「焊枪」编号[JZ02-HQ]×1，原因：报废更换','127.0.0.1','success',NULL,'2026-06-13 13:45:00'),(569,3,'','checkout','tool','JZ01-CDQ','出库工具「充电式电动解锥」编号[JZ01-CDQ]×1，原因：调拨其他机组','127.0.0.1','success',NULL,'2026-06-12 10:15:00'),(570,1,'','checkout','tool','JZ01-NLJ','出库工具「内六角扳手」编号[JZ01-NLJ]×2，原因：升级替换','127.0.0.1','success',NULL,'2026-06-11 14:30:00'),(582,1,'','checkout','tool','JZ01-test1','出库工具「test1」编号[JZ01-test1]×1，原因：其他原因','127.0.0.1','success',NULL,'2026-06-28 06:36:00'),(584,7,'','checkout','tool','JZ01-M10','出库工具「M10高强度螺栓」编号[JZ01-M10]×1，原因：损坏退役','127.0.0.1','success',NULL,'2026-06-29 09:58:05'),(589,1,'','checkin','tool','JZ01-PH2','入库工具「十字解锥头 2#」编号[JZ01-PH2]×1','127.0.0.1','success',NULL,'2026-06-03 10:46:32'),(590,1,'','checkin','tool','JZ01-NLJ','入库工具「内六角扳手」编号[JZ01-NLJ]×1','127.0.0.1','success',NULL,'2026-06-03 10:46:32'),(591,1,'','checkin','tool','JZ01-DB','入库工具「电工刀」编号[JZ01-DB]×1','127.0.0.1','success',NULL,'2026-06-03 10:46:32'),(592,1,'','checkin','tool','JZ01-YQ','入库工具「压线钳」编号[JZ01-YQ]×1','127.0.0.1','success',NULL,'2026-06-03 10:46:32'),(593,1,'','checkin','tool','JZ01-CZ','入库工具「锤子」编号[JZ01-CZ]×1','127.0.0.1','success',NULL,'2026-06-03 10:46:32'),(594,1,'','checkin','tool','JZ01-WY','入库工具「万用表」编号[JZ01-WY]×1','127.0.0.1','success',NULL,'2026-06-03 10:46:32'),(595,1,'','checkin','tool','JZ01-CRV','入库工具「开口扳手（20×22）」编号[JZ01-CRV]×1','127.0.0.1','success',NULL,'2026-06-03 10:46:32'),(596,1,'','checkin','tool','JZ02-ZD','入库工具「十字螺丝刀」编号[JZ02-ZD]×1','127.0.0.1','success',NULL,'2026-06-03 10:46:32'),(597,1,'','checkin','tool','JZ02-YZ','入库工具「一字解锥头」编号[JZ02-YZ]×1','127.0.0.1','success',NULL,'2026-06-03 10:46:32'),(598,1,'','checkin','tool','JZ02-JL','入库工具「棘轮扳手」编号[JZ02-JL]×1','127.0.0.1','success',NULL,'2026-06-03 10:46:32'),(599,1,'','checkin','tool','JZ02-HQ','入库工具「焊枪」编号[JZ02-HQ]×1','127.0.0.1','success',NULL,'2026-06-03 10:46:32'),(600,1,'','checkin','tool','JZ02-BZ','入库工具「剥线钳」编号[JZ02-BZ]×1','127.0.0.1','success',NULL,'2026-06-03 10:46:32'),(601,1,'','checkin','tool','JZ02-SG','入库工具「手锯」编号[JZ02-SG]×1','127.0.0.1','success',NULL,'2026-06-03 10:46:32'),(602,1,'','checkin','tool','JZ02-CL','入库工具「游标卡尺」编号[JZ02-CL]×1','127.0.0.1','success',NULL,'2026-06-03 10:46:32'),(603,1,'','checkin','tool','JZ02-DS','入库工具「电刷」编号[JZ02-DS]×1','127.0.0.1','success',NULL,'2026-06-03 10:46:32'),(604,1,'','checkin','tool','JZ03-QG','入库工具「强光手电」编号[JZ03-QG]×1','127.0.0.1','success',NULL,'2026-06-03 10:46:32'),(605,1,'','checkin','tool','JZ03-RF','入库工具「热风枪」编号[JZ03-RF]×1','127.0.0.1','success',NULL,'2026-06-03 10:46:32'),(606,1,'','checkin','tool','JZ03-JQ','入库工具「剪刀」编号[JZ03-JQ]×1','127.0.0.1','success',NULL,'2026-06-03 10:46:32'),(607,1,'','checkin','tool','JZ03-DJ','入库工具「电烙铁」编号[JZ03-DJ]×1','127.0.0.1','success',NULL,'2026-06-03 10:46:32'),(608,1,'','checkin','tool','JZ03-YG','入库工具「验电笔」编号[JZ03-YG]×1','127.0.0.1','success',NULL,'2026-06-03 10:46:32'),(609,1,'','checkin','tool','JZ03-XY','入库工具「吸锡器」编号[JZ03-XY]×1','127.0.0.1','success',NULL,'2026-06-03 10:46:32'),(610,1,'','checkin','tool','JZ01-M6','入库工具「M6不锈钢螺栓」编号[JZ01-M6]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(611,1,'','checkin','tool','JZ01-M8','入库工具「M8不锈钢螺栓」编号[JZ01-M8]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(612,1,'','checkin','tool','JZ01-M10','入库工具「M10高强度螺栓」编号[JZ01-M10]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(613,1,'','checkin','tool','JZ01-M12','入库工具「M12高强度螺栓」编号[JZ01-M12]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(614,1,'','checkin','tool','JZ01-N6','入库工具「M6自锁螺母」编号[JZ01-N6]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(615,1,'','checkin','tool','JZ01-N8','入库工具「M8自锁螺母」编号[JZ01-N8]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(616,1,'','checkin','tool','JZ01-N10','入库工具「M10自锁螺母」编号[JZ01-N10]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(617,1,'','checkin','tool','JZ01-W6','入库工具「M6平垫圈」编号[JZ01-W6]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(618,1,'','checkin','tool','JZ01-W8','入库工具「M8弹簧垫圈」编号[JZ01-W8]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(619,1,'','checkin','tool','JZ01-W10','入库工具「M10弹簧垫圈」编号[JZ01-W10]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(620,1,'','checkin','tool','JZ01-CAL150','入库工具「游标卡尺150mm」编号[JZ01-CAL150]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(621,1,'','checkin','tool','JZ01-CAL300','入库工具「游标卡尺300mm」编号[JZ01-CAL300]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(622,1,'','checkin','tool','JZ01-MIC25','入库工具「千分尺25mm」编号[JZ01-MIC25]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(623,1,'','checkin','tool','JZ01-MIC50','入库工具「千分尺50mm」编号[JZ01-MIC50]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(624,1,'','checkin','tool','JZ01-DT200','入库工具「深度尺200mm」编号[JZ01-DT200]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(625,1,'','checkin','tool','JZ01-PRO300','入库工具「万能角度尺」编号[JZ01-PRO300]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(626,1,'','checkin','tool','JZ01-TG300','入库工具「螺纹规公制」编号[JZ01-TG300]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(627,1,'','checkin','tool','JZ01-R500','入库工具「塞尺500mm」编号[JZ01-R500]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(628,1,'','checkin','tool','JZ01-LEV300','入库工具「水平仪300mm」编号[JZ01-LEV300]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(629,1,'','checkin','tool','JZ01-DCL100','入库工具「百分表100mm」编号[JZ01-DCL100]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(630,1,'','checkin','tool','JZ01-DR12','入库工具「充电式电钻12V」编号[JZ01-DR12]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(631,1,'','checkin','tool','JZ01-DR18','入库工具「充电式电钻18V」编号[JZ01-DR18]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(632,1,'','checkin','tool','JZ01-GR4','入库工具「角磨机100mm」编号[JZ01-GR4]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(633,1,'','checkin','tool','JZ01-GR5','入库工具「角磨机125mm」编号[JZ01-GR5]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(634,1,'','checkin','tool','JZ01-CT3','入库工具「电锤3功能」编号[JZ01-CT3]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(635,1,'','checkin','tool','JZ01-TR500','入库工具「热风枪500W」编号[JZ01-TR500]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(636,1,'','checkin','tool','JZ01-WL120','入库工具「电焊机120A」编号[JZ01-WL120]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(637,1,'','checkin','tool','JZ01-CT60','入库工具「切割机600W」编号[JZ01-CT60]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(638,1,'','checkin','tool','JZ01-DC100','入库工具「除尘枪」编号[JZ01-DC100]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(639,1,'','checkin','tool','JZ01-PMP12','入库工具「电动黄油枪」编号[JZ01-PMP12]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(640,1,'','checkin','tool','JZ01-JK10','入库工具「液压千斤顶10T」编号[JZ01-JK10]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(641,1,'','checkin','tool','JZ01-JK20','入库工具「液压千斤顶20T」编号[JZ01-JK20]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(642,1,'','checkin','tool','JZ01-PT10','入库工具「液压压力表」编号[JZ01-PT10]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(643,1,'','checkin','tool','JZ01-PH15','入库工具「液压软管15M」编号[JZ01-PH15]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(644,1,'','checkin','tool','JZ01-PH20','入库工具「液压软管20M」编号[JZ01-PH20]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(645,1,'','checkin','tool','JZ01-JC1','入库工具「液压油桶」编号[JZ01-JC1]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(646,1,'','checkin','tool','JZ01-OIL46','入库工具「抗磨液压油」编号[JZ01-OIL46]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(647,1,'','checkin','tool','JZ01-OIL68','入库工具「抗磨液压油」编号[JZ01-OIL68]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(648,1,'','checkin','tool','JZ01-FH10','入库工具「液压过滤器」编号[JZ01-FH10]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(649,1,'','checkin','tool','JZ01-WG200','入库工具「焊枪200A」编号[JZ01-WG200]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(650,1,'','checkin','tool','JZ01-WG315','入库工具「焊枪315A」编号[JZ01-WG315]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(651,1,'','checkin','tool','JZ01-ER70','入库工具「焊条ER70」编号[JZ01-ER70]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(652,1,'','checkin','tool','JZ01-ER80','入库工具「焊条ER80」编号[JZ01-ER80]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(653,1,'','checkin','tool','JZ01-WM3','入库工具「焊接面罩自动变光」编号[JZ01-WM3]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(654,1,'','checkin','tool','JZ01-WL100','入库工具「焊锡丝」编号[JZ01-WL100]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(655,1,'','checkin','tool','JZ01-GVN','入库工具「绝缘手套」编号[JZ01-GVN]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(656,1,'','checkin','tool','JZ01-GVL','入库工具「皮手套」编号[JZ01-GVL]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(657,1,'','checkin','tool','JZ01-GLL','入库工具「棉纱手套」编号[JZ01-GLL]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(658,1,'','checkin','tool','JZ01-GOG','入库工具「防护眼镜」编号[JZ01-GOG]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(659,1,'','checkin','tool','JZ01-MASK','入库工具「防尘口罩」编号[JZ01-MASK]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(660,1,'','checkin','tool','JZ01-HEL','入库工具「安全帽」编号[JZ01-HEL]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(661,1,'','checkin','tool','JZ01-VEST','入库工具「反光背心」编号[JZ01-VEST]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(662,1,'','checkin','tool','JZ01-EBT','入库工具「安全带」编号[JZ01-EBT]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(663,1,'','checkin','tool','JZ01-EAR','入库工具「耳塞」编号[JZ01-EAR]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(664,1,'','checkin','tool','JZ01-FIR','入库工具「灭火器」编号[JZ01-FIR]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(665,1,'','checkin','tool','JZ01-MUL1','入库工具「数字万用表」编号[JZ01-MUL1]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(666,1,'','checkin','tool','JZ01-MUL2','入库工具「钳形万用表」编号[JZ01-MUL2]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(667,1,'','checkin','tool','JZ01-INS1','入库工具「绝缘电阻测试仪」编号[JZ01-INS1]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(668,1,'','checkin','tool','JZ01-INS2','入库工具「接地电阻测试仪」编号[JZ01-INS2]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(669,1,'','checkin','tool','JZ01-PEN','入库工具「验电笔」编号[JZ01-PEN]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(670,1,'','checkin','tool','JZ01-PEN2','入库工具「高压验电笔」编号[JZ01-PEN2]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(671,1,'','checkin','tool','JZ01-SCOPE','入库工具「示波器」编号[JZ01-SCOPE]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(672,1,'','checkin','tool','JZ01-PSU','入库工具「直流稳压电源」编号[JZ01-PSU]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(673,1,'','checkin','tool','JZ01-SIG','入库工具「信号发生器」编号[JZ01-SIG]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(674,1,'','checkin','tool','JZ01-RLY','入库工具「继电器测试仪」编号[JZ01-RLY]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(675,1,'','checkin','tool','JZ01-DRL3','入库工具「麻花钻3mm」编号[JZ01-DRL3]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(676,1,'','checkin','tool','JZ01-DRL6','入库工具「麻花钻6mm」编号[JZ01-DRL6]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(677,1,'','checkin','tool','JZ01-DRL10','入库工具「麻花钻10mm」编号[JZ01-DRL10]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(678,1,'','checkin','tool','JZ01-TAP6','入库工具「丝锥M6」编号[JZ01-TAP6]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(679,1,'','checkin','tool','JZ01-TAP8','入库工具「丝锥M8」编号[JZ01-TAP8]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(680,1,'','checkin','tool','JZ01-MIL6','入库工具「铣刀6mm」编号[JZ01-MIL6]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(681,1,'','checkin','tool','JZ01-MIL10','入库工具「铣刀10mm」编号[JZ01-MIL10]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(682,1,'','checkin','tool','JZ01-SAW1','入库工具「手锯条」编号[JZ01-SAW1]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(683,1,'','checkin','tool','JZ01-LIGHT','入库工具「LED工作灯」编号[JZ01-LIGHT]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(684,1,'','checkin','tool','JZ01-FLASH','入库工具「强光手电」编号[JZ01-FLASH]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(685,1,'','checkin','tool','JZ01-BAG','入库工具「工具包」编号[JZ01-BAG]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(686,1,'','checkin','tool','JZ01-BOX','入库工具「零件盒」编号[JZ01-BOX]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(687,1,'','checkin','tool','JZ01-MAG','入库工具「磁性拾取器」编号[JZ01-MAG]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(688,1,'','checkin','tool','JZ01-CLOTH','入库工具「清洁布」编号[JZ01-CLOTH]×1','127.0.0.1','success',NULL,'2026-06-27 12:38:26'),(727,7,'','checkin','tool','test','入库工具「test」编号[test]×2件 位置A-006-11,A-006-14','127.0.0.1','success',NULL,'2026-07-02 22:34:05'),(728,7,'','checkin','tool','test','入库工具「test」编号[test]×1件 位置B-003-009','127.0.0.1','success',NULL,'2026-07-03 16:58:55'),(729,7,'','checkin','tool','testnew','入库工具「testnew」编号[testnew]×1件 位置C-007-001','127.0.0.1','success',NULL,'2026-07-04 17:39:17');
/*!40000 ALTER TABLE `sys_operation_log` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `sys_user`
--

DROP TABLE IF EXISTS `sys_user`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `sys_user` (
  `user_id` int(11) NOT NULL AUTO_INCREMENT COMMENT '用户主键ID',
  `username` varchar(64) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '登录用户名/工号',
  `password_hash` varchar(256) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '密码哈希值(SHA256+salt)，绝不明文存储',
  `password_salt` varchar(64) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '密码盐值，每个用户独立生成',
  `real_name` varchar(32) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '真实姓名',
  `work_no` varchar(32) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '工号，如CF002',
  `dept_id` int(11) DEFAULT NULL COMMENT '所属部门ID，关联sys_department',
  `department` varchar(64) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '部门名称冗余字段，减少join查询',
  `role` varchar(16) COLLATE utf8mb4_unicode_ci NOT NULL DEFAULT 'user' COMMENT '角色：admin管理员 user普通用户',
  `face_feature` text COLLATE utf8mb4_unicode_ci COMMENT '人脸特征数据(base64编码)，NULL表示未录入',
  `phone` varchar(20) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '联系电话',
  `email` varchar(128) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '电子邮箱',
  `status` varchar(16) COLLATE utf8mb4_unicode_ci DEFAULT 'active' COMMENT '状态：active启用 inactive禁用 pending待激活',
  `last_login_at` datetime DEFAULT NULL COMMENT '最后登录时间',
  `created_at` datetime DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
  `updated_at` datetime DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
  PRIMARY KEY (`user_id`),
  UNIQUE KEY `uk_username` (`username`),
  UNIQUE KEY `uk_work_no` (`work_no`),
  KEY `idx_dept` (`dept_id`),
  KEY `idx_role` (`role`),
  KEY `idx_status` (`status`),
  CONSTRAINT `fk_user_dept` FOREIGN KEY (`dept_id`) REFERENCES `sys_department` (`dept_id`) ON DELETE SET NULL ON UPDATE CASCADE
) ENGINE=InnoDB AUTO_INCREMENT=13 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='系统用户表 - 含密码安全存储与权限控制';
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `sys_user`
--

LOCK TABLES `sys_user` WRITE;
/*!40000 ALTER TABLE `sys_user` DISABLE KEYS */;
INSERT INTO `sys_user` VALUES (1,'001','47716ec0262b5d653358d261d8267d59697fab5ce4727cbe1a4806f7c6583cf2','K7mP2xQ9vL5nR3','小袁','001',1,'技术部','admin',NULL,'13811116789','','active','2026-09-23 11:04:09','2026-06-03 10:46:32','2026-09-23 18:59:47'),(2,'002','1836d4e19c922a4d51b3c6f755682b2d41251bf41c27b2219686c5860fffe46e','M8nQ3xR0wL6oP4','李四11','002',2,'维修一组','admin',NULL,'13922221111','','active','2026-10-03 22:29:05','2026-06-03 10:46:32','2026-10-03 22:29:05'),(3,'003','8fd7f8ce4a443187a2e033373d915c3533d4e282b2608c37ed5b65a07a6428f4','N9oR4yS1xM7pQ5','王五','003',3,'维修二组','user',NULL,'186253332222','','disabled','2026-06-13 14:35:42','2026-06-03 10:46:32','2026-09-23 18:59:47'),(4,'004','2cf35948191c769796ea679c8a482788d8cbb423bc098b49901879e2828fc23f','O0pS5zT2yN8qR6','赵六','004',2,'维修一组','user',NULL,'136****7890','','active',NULL,'2026-06-03 10:46:32','2026-09-23 18:59:47'),(5,'005','c6446b413d80f28594ada3a5c355a7518ded33987c5b8f71ba2c8718f30e3bd7','P1qT6aU3zO9rS7','孙七','005',4,'维修三组','user',NULL,'135****3456','','disabled',NULL,'2026-06-03 10:46:32','2026-09-23 18:59:47'),(6,'006','9953ee69f5f760899102e2c1d7c71afc173a992bcabb7be378b9444009b9314e','Q2rU7bV4aP0sT8','周八','006',3,'维修二组','user',NULL,'133****9012','','active','2026-06-09 20:33:50','2026-06-03 10:46:32','2026-09-23 18:59:47'),(7,'8888','b8ddfa322bfb9646d04f4f2e033e91d16283da3803683a119db0acda4d074975','d6f24366ba556ed899c0d0fc74853241','袁燕','8888',1,'技术部','admin','-0.09037976,0.04641208,0.04254105,-0.03300744,-0.08622696,-0.04733797,-0.09472980,-0.13723117,0.13645354,-0.17964552,0.16302751,-0.11446615,-0.23506567,0.00543717,-0.06715045,0.21203399,-0.19225959,-0.16982199,-0.00558400,0.00397104,0.05832737,-0.03338508,-0.00801831,0.07745639,-0.12925667,-0.30821919,-0.10780206,-0.09002645,-0.00737514,-0.06414925,-0.06997380,0.05951652,-0.11961804,-0.00353699,0.04772502,0.12267106,-0.00326500,-0.11337849,0.21551593,-0.02682362,-0.32250762,-0.00028941,0.08729159,0.26126525,0.12346129,0.02433931,-0.00998356,-0.09936392,0.12332839,-0.18365042,-0.01409800,0.15804198,0.08158992,0.09585645,0.02619257,-0.14072607,0.08943798,0.10203198,-0.11624335,0.00893057,0.10900647,-0.15387173,0.10706431,-0.07016842,0.27910596,0.06472651,-0.13226388,-0.12251761,0.09698518,-0.15311310,-0.09240340,0.03879544,-0.14217126,-0.18669078,-0.30728602,-0.03236571,0.34328339,0.15186167,-0.11391686,0.13274176,-0.00011095,-0.02161510,0.13368025,0.26138419,0.00896918,0.07881124,-0.03666929,-0.00958357,0.25691319,-0.08630934,0.01366910,0.17943245,-0.05970304,0.07288864,-0.00642561,-0.00270710,-0.09656084,0.02478456,-0.07449134,0.03776925,0.00657565,-0.01889804,0.04703994,0.10290349,-0.17056914,0.05034187,-0.01619966,0.01490904,0.01561750,0.06272487,-0.06951103,-0.07372429,0.10556178,-0.26590976,0.21548831,0.19333147,0.05799798,0.09887751,0.13442203,0.07088491,0.01399811,-0.00803267,-0.17679346,-0.02452130,0.11565418,-0.02510000,0.09848087,0.00482514','18628256988',NULL,'active','2026-09-24 11:00:44','2026-06-10 21:19:18','2026-10-03 22:30:22'),(10,'007','31f41a68bd996d18cebb1347e89a88cccd371e1337d5cdffabe02076ad6530c3','5cd6d69cdaf437af4ebb61d9d54d85ac','张三','007',NULL,'技术部','user',NULL,'13800138000',NULL,'active','2026-06-27 10:02:20','2026-06-27 09:55:07','2026-09-23 18:59:47'),(11,'008','671482d967c2e82f1337ce5d4a884e36d45d383f1b62eefe7a811ad989cd6be9','c6cabd2d10c8bdd86582ed88eed27eb6','马慧芳','008',NULL,'技术部','user','-0.14446980,0.05906566,0.11112928,-0.07001518,-0.10277819,-0.06078334,-0.05388048,-0.12849388,0.12791459,-0.12202986,0.20188139,-0.07528247,-0.19629382,-0.04817959,-0.05963349,0.19735917,-0.18493123,-0.15894397,-0.09522603,-0.05764852,0.02831164,0.06055769,-0.00804442,-0.01161571,-0.07560876,-0.37729794,-0.09439914,-0.03946378,0.02963907,0.01645831,-0.03884758,0.07574720,-0.13489111,-0.07398781,0.08283230,0.17447700,-0.02065436,-0.10870367,0.11528552,-0.06111333,-0.27230588,0.05598699,0.11245194,0.22565885,0.19073744,0.00612034,0.03080477,-0.15061693,0.12141656,-0.10993333,-0.02756750,0.15050884,0.07903044,0.12332910,0.01501459,-0.07548591,0.03799427,0.15697256,-0.09486341,-0.06023104,0.11833799,-0.11696780,0.01200057,-0.11765725,0.16060276,0.05845851,-0.11950359,-0.21707852,0.11454757,-0.15127338,-0.14262423,0.02858600,-0.16608354,-0.11419476,-0.28478181,0.01809002,0.38089183,0.07608315,-0.14493664,0.05567430,-0.01010294,0.01490678,0.09507126,0.18647592,-0.01649676,0.02916191,-0.05577341,-0.04223906,0.25305292,-0.04727194,-0.01771358,0.13676786,0.00148042,0.06834962,0.03840620,0.05101749,-0.08729991,0.06280763,-0.14555253,-0.00818296,0.01899931,-0.03097299,0.00864492,0.08965551,-0.11410386,0.15940970,-0.00863282,0.07348497,0.04886493,0.01003704,-0.00200236,-0.05719826,0.12879002,-0.17365377,0.19414774,0.21695472,0.07075945,0.13933614,0.14745122,0.09665442,-0.01958846,0.02677132,-0.18424435,-0.06061604,0.07703085,-0.00646535,0.06327023,0.01411405','13888138888',NULL,'active',NULL,'2026-06-27 14:12:29','2026-09-23 18:59:47'),(12,'011','4238d278bd8be4028b115958e457deb3736c9a7dedc9e7f9a5e8e0f136a027eb','31ca0e510c36073b6fd1474461687943','小王','011',NULL,'技术部','user',NULL,'18628256929',NULL,'disabled',NULL,'2026-06-29 09:03:33','2026-09-23 18:59:47');
/*!40000 ALTER TABLE `sys_user` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `system_config`
--

DROP TABLE IF EXISTS `system_config`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `system_config` (
  `config_key` varchar(64) COLLATE utf8mb4_unicode_ci NOT NULL,
  `config_value` text COLLATE utf8mb4_unicode_ci,
  `updated_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`config_key`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `system_config`
--

LOCK TABLES `system_config` WRITE;
/*!40000 ALTER TABLE `system_config` DISABLE KEYS */;
/*!40000 ALTER TABLE `system_config` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `task_type`
--

DROP TABLE IF EXISTS `task_type`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `task_type` (
  `type_id` int(11) NOT NULL AUTO_INCREMENT COMMENT '任务类型ID',
  `type_code` varchar(32) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '类型编码',
  `type_name` varchar(64) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '类型名称',
  `description` varchar(256) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '类型描述',
  `sort_order` int(11) NOT NULL DEFAULT '0' COMMENT '排序(越小越前)',
  `is_active` tinyint(4) NOT NULL DEFAULT '1' COMMENT '1=启用 0=禁用',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `default_duration` int(11) DEFAULT '30',
  PRIMARY KEY (`type_id`),
  UNIQUE KEY `uk_type_code` (`type_code`)
) ENGINE=InnoDB AUTO_INCREMENT=11 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='飞机维修任务类型表';
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `task_type`
--

LOCK TABLES `task_type` WRITE;
/*!40000 ALTER TABLE `task_type` DISABLE KEYS */;
INSERT INTO `task_type` VALUES (1,'PRECHECK','航前检查','航班起飞前对工具柜工具进行全面检查与准备',1,1,'2026-06-30 17:09:24',60),(2,'POSTCHECK','航后维护','航班降落后对工具进行归位、清洁与维护',2,1,'2026-06-30 17:09:24',60),(3,'ENG_MAINT','发动机维护','发动机拆装、检查、更换部件等核心维护作业',3,1,'2026-06-30 17:09:24',120),(4,'AVIONICS','航电检修','航空电子设备、仪表、通信导航系统检测维修',4,1,'2026-06-30 17:09:24',90),(5,'HYDRAULIC','液压系统维护','液压管路、泵阀、作动筒检查与更换',5,1,'2026-06-30 17:09:24',60),(6,'STRUCTURE','结构修理','机身蒙皮、框架、紧固件损伤修复',6,1,'2026-06-30 17:09:24',180),(7,'LANDING','起落架维护','起落架减震、刹车系统、轮胎更换',7,1,'2026-06-30 17:09:24',90),(8,'ELECTRICAL','电气线路检修','线路故障排查、线束修复、接插件更换',8,1,'2026-06-30 17:09:24',60),(9,'WELDING','焊接作业','金属结构焊接、修补、热处理',9,1,'2026-06-30 17:09:24',120),(10,'MEASURE','精密测量','三坐标测量、形位公差检测、校准',10,1,'2026-06-30 17:09:24',60);
/*!40000 ALTER TABLE `task_type` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `task_type_tool`
--

DROP TABLE IF EXISTS `task_type_tool`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `task_type_tool` (
  `id` int(11) NOT NULL AUTO_INCREMENT,
  `type_id` int(11) NOT NULL COMMENT '任务类型ID',
  `tool_id` int(11) NOT NULL COMMENT '工具ID',
  `sort_order` int(11) NOT NULL DEFAULT '0' COMMENT '推荐排序',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `recommended_qty` int(11) NOT NULL DEFAULT '1' COMMENT '推荐借用数量',
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_type_tool` (`type_id`,`tool_id`),
  KEY `idx_type_id` (`type_id`),
  KEY `idx_tool_id` (`tool_id`)
) ENGINE=InnoDB AUTO_INCREMENT=7187 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='任务类型-工具推荐关联表';
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `task_type_tool`
--

LOCK TABLES `task_type_tool` WRITE;
/*!40000 ALTER TABLE `task_type_tool` DISABLE KEYS */;
INSERT INTO `task_type_tool` VALUES (6104,1,1,1,'2026-06-30 17:09:24',1),(6105,1,2,2,'2026-06-30 17:09:24',1),(6106,1,3,3,'2026-06-30 17:09:24',1),(6107,1,5,4,'2026-06-30 17:09:24',1),(6108,1,6,5,'2026-06-30 17:09:24',1),(6109,1,9,6,'2026-06-30 17:09:24',1),(6110,1,10,7,'2026-06-30 17:09:24',1),(6111,1,11,8,'2026-06-30 17:09:24',1),(6112,1,13,9,'2026-06-30 17:09:24',1),(6113,1,19,10,'2026-06-30 17:09:24',1),(6114,2,9,1,'2026-06-30 17:09:24',1),(6115,2,13,2,'2026-06-30 17:09:24',1),(6116,2,2,3,'2026-06-30 17:09:24',1),(6117,2,11,4,'2026-06-30 17:09:24',1),(6118,2,6,5,'2026-06-30 17:09:24',1),(6119,2,19,6,'2026-06-30 17:09:24',1),(6120,3,1,1,'2026-06-30 17:09:24',1),(6121,3,2,2,'2026-06-30 17:09:24',1),(6122,3,10,3,'2026-06-30 17:09:24',1),(6123,3,5,4,'2026-06-30 17:09:24',1),(6124,3,11,5,'2026-06-30 17:09:24',1),(6125,3,6,6,'2026-06-30 17:09:24',1),(6126,4,11,1,'2026-06-30 17:09:24',1),(6127,4,25,2,'2026-06-30 17:09:24',1),(6128,4,17,3,'2026-06-30 17:09:24',1),(6129,4,8,4,'2026-06-30 17:09:24',1),(6130,4,24,5,'2026-06-30 17:09:24',1),(6131,4,3,6,'2026-06-30 17:09:24',1),(6132,5,2,1,'2026-06-30 17:09:24',1),(6133,5,15,2,'2026-06-30 17:09:24',1),(6134,5,5,3,'2026-06-30 17:09:24',1),(6135,5,9,4,'2026-06-30 17:09:24',1),(6136,5,13,5,'2026-06-30 17:09:24',1),(6137,6,9,1,'2026-06-30 17:09:24',1),(6138,6,18,2,'2026-06-30 17:09:24',1),(6139,6,20,3,'2026-06-30 17:09:24',1),(6140,6,13,4,'2026-06-30 17:09:24',1),(6141,6,14,5,'2026-06-30 17:09:24',1),(6142,6,23,6,'2026-06-30 17:09:24',1),(6143,7,10,1,'2026-06-30 17:09:24',1),(6144,7,15,2,'2026-06-30 17:09:24',1),(6145,7,12,3,'2026-06-30 17:09:24',1),(6146,7,21,4,'2026-06-30 17:09:24',1),(6147,7,19,5,'2026-06-30 17:09:24',1),(6148,8,11,1,'2026-06-30 17:09:24',1),(6149,8,25,2,'2026-06-30 17:09:24',1),(6150,8,17,3,'2026-06-30 17:09:24',1),(6151,8,7,4,'2026-06-30 17:09:24',1),(6152,8,24,5,'2026-06-30 17:09:24',1),(6153,8,3,6,'2026-06-30 17:09:24',1),(6154,9,16,1,'2026-06-30 17:09:24',1),(6155,9,22,2,'2026-06-30 17:09:24',1),(6156,9,24,3,'2026-06-30 17:09:24',1),(6157,9,26,4,'2026-06-30 17:09:24',1),(6158,9,9,5,'2026-06-30 17:09:24',1),(6159,10,19,1,'2026-06-30 17:09:24',1),(6160,10,6,2,'2026-06-30 17:09:24',1),(6161,10,11,3,'2026-06-30 17:09:24',1),(6162,10,25,4,'2026-06-30 17:09:24',1),(6163,10,5,5,'2026-06-30 17:09:24',1),(6224,1,114,0,'2026-06-30 17:31:37',1),(6885,1,123,0,'2026-06-30 19:12:55',2),(6946,2,124,0,'2026-07-04 17:42:10',2);
/*!40000 ALTER TABLE `task_type_tool` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `tool_borrow_record`
--

DROP TABLE IF EXISTS `tool_borrow_record`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `tool_borrow_record` (
  `record_id` int(11) NOT NULL AUTO_INCREMENT COMMENT '借用记录主键ID',
  `flow_no` varchar(32) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '任务流水号，格式JH-MMDD-原因HHmm',
  `user_id` int(11) NOT NULL COMMENT '借用人ID，关联sys_user',
  `tool_id` int(11) NOT NULL COMMENT '工具ID，关联tool_info',
  `borrow_qty` int(11) NOT NULL DEFAULT '1' COMMENT '借用数量',
  `borrow_reason` varchar(64) COLLATE utf8mb4_unicode_ci NOT NULL DEFAULT '' COMMENT '借用原因：发动机维护/航电检修/日常巡检等',
  `borrow_time` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '借用时间',
  `expected_return_time` datetime DEFAULT NULL COMMENT '预计归还时间',
  `actual_return_time` datetime DEFAULT NULL COMMENT '实际归还时间',
  `status` varchar(16) COLLATE utf8mb4_unicode_ci DEFAULT 'borrowing' COMMENT '状态：borrowing借用中 returned已归还 overdue逾期',
  `operator_id` int(11) DEFAULT NULL COMMENT '操作人ID（管理员代操作时）',
  `remark` varchar(256) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '备注',
  `created_at` datetime DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
  `mapping_id` int(11) DEFAULT NULL,
  PRIMARY KEY (`record_id`),
  UNIQUE KEY `uk_flow_no` (`flow_no`),
  KEY `idx_user` (`user_id`),
  KEY `idx_tool` (`tool_id`),
  KEY `idx_status` (`status`),
  KEY `idx_borrow_time` (`borrow_time`),
  KEY `idx_mapping_id` (`mapping_id`),
  CONSTRAINT `fk_borrow_tool` FOREIGN KEY (`tool_id`) REFERENCES `tool_info` (`tool_id`) ON UPDATE CASCADE,
  CONSTRAINT `fk_borrow_user` FOREIGN KEY (`user_id`) REFERENCES `sys_user` (`user_id`) ON UPDATE CASCADE
) ENGINE=InnoDB AUTO_INCREMENT=121 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='工具借用记录表 - 追踪每次借用全生命周期';
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `tool_borrow_record`
--

LOCK TABLES `tool_borrow_record` WRITE;
/*!40000 ALTER TABLE `tool_borrow_record` DISABLE KEYS */;
INSERT INTO `tool_borrow_record` VALUES (1,'BR20260301001',2,1,1,'维修电动设备','2026-03-01 08:30:00','2026-03-01 17:00:00','2026-03-01 16:45:00','returned',NULL,'','2026-07-02 22:31:51',NULL),(2,'BR20260305001',3,2,2,'更换开口扳手组','2026-03-05 09:15:00','2026-03-05 18:00:00','2026-03-05 17:30:00','returned',NULL,'','2026-07-02 22:31:51',NULL),(3,'BR20260310001',4,5,1,'设备检修','2026-03-10 10:00:00','2026-03-10 17:00:00','2026-03-10 16:20:00','returned',NULL,'','2026-07-02 22:31:51',NULL),(4,'BR20260315001',2,10,1,'大修发动机','2026-03-15 08:45:00','2026-03-16 17:00:00','2026-03-16 15:30:00','returned',NULL,'','2026-07-02 22:31:51',NULL),(5,'BR20260402001',6,13,2,'日常维护','2026-04-02 11:00:00','2026-04-02 17:00:00','2026-04-02 16:50:00','returned',NULL,'','2026-07-02 22:31:51',NULL),(6,'BR20260408001',3,1,1,'紧急维修','2026-04-08 14:20:00','2026-04-08 18:00:00','2026-04-08 17:15:00','returned',NULL,'','2026-07-02 22:31:51',NULL),(7,'BR20260415001',5,4,3,'批量更换螺丝','2026-04-15 09:30:00','2026-04-15 18:00:00','2026-04-15 17:40:00','returned',NULL,'','2026-07-02 22:31:51',NULL),(8,'BR20260422001',2,11,1,'电气检测','2026-04-22 10:10:00','2026-04-22 17:00:00','2026-04-22 16:30:00','returned',NULL,'','2026-07-02 22:31:51',NULL),(9,'BR20260501001',4,7,1,'线路维修','2026-05-01 08:00:00','2026-05-01 12:00:00','2026-05-01 11:45:00','returned',NULL,'','2026-07-02 22:31:51',NULL),(10,'BR20260506002',1,6,1,'精密测量','2026-05-06 13:30:00','2026-05-06 18:00:00','2026-05-06 17:20:00','returned',NULL,'','2026-07-02 22:31:51',NULL),(11,'BR20260512001',6,22,1,'设备加热','2026-05-12 09:50:00','2026-05-12 17:00:00','2026-05-12 16:55:00','returned',NULL,'','2026-07-02 22:31:51',NULL),(12,'BR20260601001',3,9,1,'基础维修','2026-06-01 10:30:00','2026-06-01 17:00:00','2026-06-01 17:10:00','returned',NULL,'','2026-07-02 22:31:51',NULL),(13,'BR20260615001',5,19,1,'精密测量','2026-06-15 08:15:00','2026-06-15 17:00:00','2026-06-15 16:40:00','returned',NULL,'','2026-07-02 22:31:51',NULL),(14,'BR20260603002',4,3,1,'电器维修','2026-06-03 10:00:00','2026-06-03 17:00:00','2026-06-03 16:30:00','returned',NULL,'','2026-07-02 22:31:51',NULL),(15,'BR20260605003',2,14,2,'改锥头更换','2026-06-05 11:15:00','2026-06-05 18:00:00','2026-06-05 17:30:00','returned',NULL,'','2026-07-02 22:31:51',NULL),(16,'BR20260608001',5,22,1,'热风作业','2026-06-08 08:50:00','2026-06-08 17:00:00',NULL,'borrowing',NULL,'','2026-07-02 22:31:51',20),(17,'BR20260610001',4,7,1,'电工刀维修','2026-06-10 09:10:00','2026-06-10 17:00:00',NULL,'borrowing',NULL,'','2026-07-02 22:31:51',6),(18,'BR20260612001',2,14,1,'解锥头更换','2026-06-12 10:30:00','2026-06-12 18:00:00',NULL,'overdue',NULL,'','2026-07-02 22:31:51',12),(19,'BR20260609002',3,10,1,'发动机检修','2026-06-09 08:15:00','2026-06-09 17:00:00','2026-06-09 16:40:00','returned',NULL,'','2026-07-02 22:31:51',NULL),(20,'BR20260611003',6,5,1,'内六角保养','2026-06-11 13:00:00','2026-06-11 17:00:00',NULL,'borrowing',NULL,'','2026-07-02 22:31:51',5),(116,'JH-0703-PR1452-01',7,123,1,'航前检查','2026-07-03 14:52:55','2026-07-05 14:52:42','2026-07-03 14:52:55','returned',NULL,'系统修复-重复借出记录合并','2026-07-03 14:52:55',169),(117,'JH-0703-PR1452-02',7,123,1,'航前检查','2026-07-03 14:52:55','2026-07-05 14:52:42','2026-07-07 11:26:29','returned',7,' | 归还:正常(正常)','2026-07-03 14:52:55',158),(118,'JH-0703-PR1452-03',7,62,1,'航前检查','2026-07-03 14:52:55','2026-07-05 14:52:42',NULL,'borrowing',NULL,'','2026-07-03 14:52:55',60),(119,'JH-0704-PO1742-01',7,2,1,'航后维护','2026-07-04 17:42:44','2026-07-06 17:42:39','2026-07-04 17:48:43','returned',7,' | 归还:正常(正常)','2026-07-04 17:42:44',2),(120,'JH-0704-PO1742-02',7,124,1,'航后维护','2026-07-04 17:42:44','2026-07-06 17:42:39','2026-07-04 17:44:30','returned',7,' | 归还:正常(正常)','2026-07-04 17:42:44',174);
/*!40000 ALTER TABLE `tool_borrow_record` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `tool_cabinet`
--

DROP TABLE IF EXISTS `tool_cabinet`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `tool_cabinet` (
  `cabinet_id` int(11) NOT NULL AUTO_INCREMENT COMMENT '工具柜主键ID',
  `cabinet_name` varchar(32) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '柜体名称，如"A柜"、"B柜"',
  `cabinet_code` varchar(32) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '柜体编码，如CAB-A01',
  `location` varchar(128) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '存放位置描述',
  `ip_address` varchar(45) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '设备IP地址',
  `status` varchar(16) COLLATE utf8mb4_unicode_ci DEFAULT 'online' COMMENT '状态：online在线 offline离线 error故障',
  `created_at` datetime DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
  PRIMARY KEY (`cabinet_id`),
  UNIQUE KEY `uk_code` (`cabinet_code`)
) ENGINE=InnoDB AUTO_INCREMENT=4 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='工具柜物理设备表 - 与硬件实体一一对应';
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `tool_cabinet`
--

LOCK TABLES `tool_cabinet` WRITE;
/*!40000 ALTER TABLE `tool_cabinet` DISABLE KEYS */;
INSERT INTO `tool_cabinet` VALUES (1,'A柜','CAB-A01','维修车间东侧','192.168.1.101','online','2026-06-03 10:46:32'),(2,'B柜','CAB-B01','维修车间西侧','192.168.1.102','online','2026-06-03 10:46:32'),(3,'C柜','CAB-C01','备件仓库','192.168.1.103','online','2026-06-03 10:46:32');
/*!40000 ALTER TABLE `tool_cabinet` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `tool_category`
--

DROP TABLE IF EXISTS `tool_category`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `tool_category` (
  `category_id` int(11) NOT NULL AUTO_INCREMENT COMMENT '分类主键ID',
  `category_name` varchar(64) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '分类名称，如"电动工具"、"手动工具"',
  `parent_id` int(11) DEFAULT '0' COMMENT '上级分类ID，0为顶级分类',
  `sort_order` int(11) DEFAULT '0' COMMENT '排序权重',
  `icon` varchar(64) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '分类图标(emoji或icon名)',
  `created_at` datetime DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
  PRIMARY KEY (`category_id`),
  KEY `idx_parent` (`parent_id`)
) ENGINE=InnoDB AUTO_INCREMENT=8 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='工具分类表 - 树形层级结构';
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `tool_category`
--

LOCK TABLES `tool_category` WRITE;
/*!40000 ALTER TABLE `tool_category` DISABLE KEYS */;
INSERT INTO `tool_category` VALUES (1,'电动工具',0,1,'?','2026-06-03 10:46:32'),(2,'手动工具',0,2,'?','2026-06-03 10:46:32'),(3,'测量工具',0,3,'?','2026-06-03 10:46:32'),(4,'焊接工具',0,4,'?','2026-06-03 10:46:32'),(5,'照明工具',0,5,'?','2026-06-03 10:46:32'),(6,'紧固工具',1,1,'?','2026-06-03 10:46:32'),(7,'切割工具',2,1,'✂️','2026-06-03 10:46:32');
/*!40000 ALTER TABLE `tool_category` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `tool_checkin_record`
--

DROP TABLE IF EXISTS `tool_checkin_record`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `tool_checkin_record` (
  `record_id` int(11) NOT NULL AUTO_INCREMENT COMMENT '入库记录主键ID',
  `tool_id` int(11) NOT NULL COMMENT '工具ID（新增工具时可为NULL，表示全新录入）',
  `tool_code` varchar(32) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '工具编号（新增工具时必填）',
  `tool_name` varchar(128) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '工具名称',
  `checkin_qty` int(11) NOT NULL DEFAULT '1' COMMENT '入库数量',
  `supplier` varchar(64) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '供应商',
  `checkin_time` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '入库时间',
  `operator_id` int(11) NOT NULL COMMENT '操作管理员ID',
  `cabinet_id` int(11) DEFAULT NULL COMMENT '入库柜体ID',
  `remark` varchar(256) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '备注',
  `created_at` datetime DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
  PRIMARY KEY (`record_id`),
  KEY `idx_tool` (`tool_id`),
  KEY `idx_operator` (`operator_id`),
  KEY `idx_time` (`checkin_time`),
  CONSTRAINT `fk_checkin_operator` FOREIGN KEY (`operator_id`) REFERENCES `sys_user` (`user_id`) ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='工具入库记录表 - 新增/补货追踪';
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `tool_checkin_record`
--

LOCK TABLES `tool_checkin_record` WRITE;
/*!40000 ALTER TABLE `tool_checkin_record` DISABLE KEYS */;
/*!40000 ALTER TABLE `tool_checkin_record` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `tool_checkout_record`
--

DROP TABLE IF EXISTS `tool_checkout_record`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `tool_checkout_record` (
  `record_id` int(11) NOT NULL AUTO_INCREMENT COMMENT '出库记录主键ID',
  `tool_id` int(11) NOT NULL COMMENT '工具ID',
  `user_id` int(11) DEFAULT NULL COMMENT '执行出库的用户ID',
  `checkout_qty` int(11) NOT NULL DEFAULT '1' COMMENT '出库数量',
  `checkout_reason` varchar(64) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '出库原因：报废更换/损坏退役/调拨其他机组/升级替换/借出未还',
  `checkout_time` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '出库时间',
  `operator_id` int(11) NOT NULL COMMENT '操作管理员ID',
  `remark` varchar(256) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '备注',
  `created_at` datetime DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
  PRIMARY KEY (`record_id`),
  KEY `idx_tool` (`tool_id`),
  KEY `idx_operator` (`operator_id`),
  KEY `idx_time` (`checkout_time`),
  CONSTRAINT `fk_checkout_operator` FOREIGN KEY (`operator_id`) REFERENCES `sys_user` (`user_id`) ON UPDATE CASCADE,
  CONSTRAINT `fk_checkout_tool` FOREIGN KEY (`tool_id`) REFERENCES `tool_info` (`tool_id`) ON UPDATE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='工具出库记录表 - 永久移除追踪';
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `tool_checkout_record`
--

LOCK TABLES `tool_checkout_record` WRITE;
/*!40000 ALTER TABLE `tool_checkout_record` DISABLE KEYS */;
/*!40000 ALTER TABLE `tool_checkout_record` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `tool_info`
--

DROP TABLE IF EXISTS `tool_info`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `tool_info` (
  `tool_id` int(11) NOT NULL AUTO_INCREMENT COMMENT '工具主键ID',
  `tool_code` varchar(32) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '工具编号，如JZ01-CDQ(班组ID-规格缩写)',
  `tool_name` varchar(128) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '工具名称',
  `spec` varchar(64) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '规格型号',
  `category_id` int(11) DEFAULT NULL COMMENT '分类ID，关联tool_category',
  `cabinet_id` int(11) DEFAULT NULL COMMENT '所属柜体ID，关联tool_cabinet',
  `machine_group_id` int(11) DEFAULT NULL COMMENT '所属机组ID',
  `layer` varchar(8) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '所在层号',
  `position` varchar(16) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '具体位置',
  `total_qty` int(11) NOT NULL DEFAULT '0' COMMENT '总数量',
  `current_qty` int(11) NOT NULL DEFAULT '0' COMMENT '当前在库数量，借用时扣减/归还时增加',
  `unit` varchar(8) COLLATE utf8mb4_unicode_ci DEFAULT '件',
  `supplier` varchar(128) COLLATE utf8mb4_unicode_ci DEFAULT NULL COMMENT '供应商',
  `vision_tag` varchar(64) COLLATE utf8mb4_unicode_ci DEFAULT NULL,
  `status` varchar(16) COLLATE utf8mb4_unicode_ci DEFAULT 'in_stock' COMMENT '状态：in_stock在库 borrowed借出 maintenance维修中',
  `checkout_reason` varchar(64) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '最近出库原因',
  `is_recommended` tinyint(4) DEFAULT '0' COMMENT '是否推荐工具：1是 0否',
  `created_at` datetime DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
  `updated_at` datetime DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
  `recognition_method` varchar(16) COLLATE utf8mb4_unicode_ci DEFAULT 'vision',
  `document_path` varchar(512) COLLATE utf8mb4_unicode_ci DEFAULT '',
  `cabinet_name` varchar(255) COLLATE utf8mb4_unicode_ci DEFAULT '',
  PRIMARY KEY (`tool_id`),
  UNIQUE KEY `uk_tool_code` (`tool_code`),
  KEY `idx_category` (`category_id`),
  KEY `idx_cabinet` (`cabinet_id`),
  KEY `idx_status` (`status`),
  KEY `idx_rfid` (`vision_tag`),
  KEY `idx_machine_group` (`machine_group_id`),
  CONSTRAINT `fk_tool_cabinet` FOREIGN KEY (`cabinet_id`) REFERENCES `tool_cabinet` (`cabinet_id`) ON DELETE SET NULL ON UPDATE CASCADE,
  CONSTRAINT `fk_tool_category` FOREIGN KEY (`category_id`) REFERENCES `tool_category` (`category_id`) ON DELETE SET NULL ON UPDATE CASCADE,
  CONSTRAINT `fk_tool_machine_group` FOREIGN KEY (`machine_group_id`) REFERENCES `machine_group` (`group_id`) ON DELETE SET NULL ON UPDATE CASCADE
) ENGINE=InnoDB AUTO_INCREMENT=125 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='工具信息表 - 核心资产管理';
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `tool_info`
--

LOCK TABLES `tool_info` WRITE;
/*!40000 ALTER TABLE `tool_info` DISABLE KEYS */;
INSERT INTO `tool_info` VALUES (1,'JZ01-CDQ','充电式电动解锥','12V 锂电',1,1,1,'01','01',1,1,'件',NULL,'视觉-001','in_stock','',1,'2026-06-03 10:46:32','2026-09-24 10:09:41','vision','',''),(2,'JZ01-KB','9# 开口扳手','CR-V 9mm',2,1,1,'01','05',1,1,'件',NULL,'视觉-002','in_stock','',1,'2026-06-03 10:46:32','2026-09-24 10:09:41','vision','',''),(3,'JZ01-BX','保险丝钳','8寸 绝缘',2,1,1,'01','15',1,1,'件',NULL,'视觉-003','in_stock','',1,'2026-06-03 10:46:32','2026-09-24 10:09:41','vision','',''),(4,'JZ01-PH2','十字解锥头 2#','PH2-100mm',2,1,2,'03','12',1,1,'件',NULL,'视觉-004','in_stock','',0,'2026-06-03 10:46:32','2026-09-24 10:09:41','vision','',''),(5,'JZ01-NLJ','内六角扳手','6mm 内六角',2,1,3,'01','06',1,0,'件',NULL,'视觉-005','borrowed','',1,'2026-06-03 10:46:32','2026-09-24 10:09:41','vision','',''),(6,'JZ01-SB','塞尺','0.02-1.0mm',3,NULL,3,NULL,NULL,1,0,'件',NULL,'视觉-006','pending','',0,'2026-06-03 10:46:32','2026-09-24 10:09:41','vision','',''),(7,'JZ01-DB','电工刀','折叠式 绝缘',7,1,2,'01','09',1,0,'件',NULL,'视觉-007','borrowed','',0,'2026-06-03 10:46:32','2026-09-24 10:09:41','vision','',''),(8,'JZ01-YQ','压线钳','0.25-10mm²',2,1,2,'02','07',1,1,'件',NULL,'视觉-008','in_stock','',0,'2026-06-03 10:46:32','2026-09-24 10:09:41','vision','',''),(9,'JZ01-CZ','锤子','1.5磅 纤维柄',2,1,3,'03','14',1,1,'件',NULL,'视觉-009','in_stock','',0,'2026-06-03 10:46:32','2026-09-24 10:09:41','vision','',''),(10,'JZ01-TH','套筒扳手组','8-32mm 套装',6,1,1,'02','13',1,1,'件',NULL,'视觉-010','in_stock','',1,'2026-06-03 10:46:32','2026-09-24 10:09:41','vision','C:/Users/25007/AppData/Roaming/SmartCabinet/QtSmartCabinet/tool_documents/20260629090723_11.doc',''),(11,'JZ01-WY','万用表','数字DT9205A',3,1,2,'02','01',1,1,'件',NULL,'视觉-011','in_stock','',0,'2026-06-03 10:46:32','2026-09-24 10:09:41','vision','',''),(12,'JZ01-CRV','开口扳手（20×22）','CR-V 20×22',2,NULL,3,NULL,NULL,1,0,'件',NULL,'视觉-012','pending','',0,'2026-06-03 10:46:32','2026-09-24 10:09:41','vision','',''),(13,'JZ02-ZD','十字螺丝刀','3×150mm',2,2,5,'01','03',1,1,'件',NULL,'视觉-013','in_stock','',0,'2026-06-03 10:46:32','2026-09-24 10:09:41','vision','',''),(14,'JZ02-YZ','一字解锥头','8mm 工业级',2,2,4,'02','12',1,0,'件',NULL,'视觉-014','borrowed','',0,'2026-06-03 10:46:32','2026-09-24 10:09:41','vision','',''),(15,'JZ02-JL','棘轮扳手','10mm 棘轮',2,2,4,'02','03',1,1,'件',NULL,'视觉-015','in_stock','',0,'2026-06-03 10:46:32','2026-09-24 10:09:41','vision','',''),(16,'JZ02-HQ','焊枪','30W 恒温',4,2,5,'03','07',1,1,'件',NULL,'视觉-016','in_stock','',0,'2026-06-03 10:46:32','2026-09-24 10:09:41','vision','',''),(17,'JZ02-BZ','剥线钳','0.5-6mm²',2,2,5,'01','14',1,1,'件',NULL,'视觉-017','in_stock','',0,'2026-06-03 10:46:32','2026-09-24 10:09:41','vision','',''),(18,'JZ02-SG','手锯','12寸 可调',7,2,4,'01','11',1,1,'件',NULL,'视觉-018','in_stock','',0,'2026-06-03 10:46:32','2026-09-24 10:09:41','vision','',''),(19,'JZ02-CL','游标卡尺','0-150mm',3,2,7,'02','06',1,1,'件',NULL,'视觉-019','in_stock','',0,'2026-06-03 10:46:32','2026-09-24 10:09:41','vision','',''),(20,'JZ02-DS','电刷','铜丝 工业级',2,2,6,'01','08',1,1,'件',NULL,'视觉-020','in_stock','',0,'2026-06-03 10:46:32','2026-09-24 10:09:41','vision','',''),(21,'JZ03-QG','强光手电','LED-1000LM',5,2,6,'03','10',1,1,'件',NULL,'视觉-021','in_stock','',0,'2026-06-03 10:46:32','2026-09-24 10:09:41','vision','',''),(22,'JZ03-RF','热风枪','2000W 温控',4,3,6,'01','11',1,0,'件',NULL,'视觉-022','borrowed','',1,'2026-06-03 10:46:32','2026-09-24 10:09:41','vision','',''),(23,'JZ03-JQ','剪刀','6寸 不锈钢',7,3,8,'03','02',1,1,'件',NULL,'视觉-023','in_stock','',0,'2026-06-03 10:46:32','2026-09-24 10:09:41','vision','',''),(24,'JZ03-DJ','电烙铁','60W 调温',4,3,6,'02','09',1,1,'件',NULL,'视觉-024','in_stock','',0,'2026-06-03 10:46:32','2026-09-24 10:09:41','vision','',''),(25,'JZ03-YG','验电笔','100-500V',3,3,2,'01','02',1,1,'件',NULL,'视觉-025','in_stock','',0,'2026-06-03 10:46:32','2026-09-24 10:09:41','vision','',''),(26,'JZ03-XY','吸锡器','铝合金 30W',4,3,7,'03','05',1,1,'件',NULL,'视觉-026','in_stock','',0,'2026-06-03 10:46:32','2026-09-24 10:09:41','vision','',''),(27,'JZ01-M6','M6不锈钢螺栓','M6×30mm',6,1,1,'04','01',1,1,'件',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(28,'JZ01-M8','M8不锈钢螺栓','M8×40mm',6,1,1,'04','02',1,1,'件',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(29,'JZ01-M10','M10高强度螺栓','M10×50mm',6,1,1,'04','03',1,1,'件',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(30,'JZ01-M12','M12高强度螺栓','M12×60mm',6,1,1,'04','04',1,1,'件',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(31,'JZ01-N6','M6自锁螺母','M6',6,1,1,'04','05',1,1,'件',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(32,'JZ01-N8','M8自锁螺母','M8',6,1,1,'04','06',1,1,'件',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(33,'JZ01-N10','M10自锁螺母','M10',6,1,1,'04','07',1,1,'件',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(34,'JZ01-W6','M6平垫圈','M6',6,1,1,'04','08',1,1,'件',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(35,'JZ01-W8','M8弹簧垫圈','M8',6,1,1,'04','09',1,1,'件',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(36,'JZ01-W10','M10弹簧垫圈','M10',6,1,1,'04','10',1,1,'件',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(37,'JZ01-CAL150','游标卡尺150mm','0-150mm/0.02',3,1,1,'05','01',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(38,'JZ01-CAL300','游标卡尺300mm','0-300mm/0.02',3,1,1,'05','02',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(39,'JZ01-MIC25','千分尺25mm','0-25mm/0.001',3,1,1,'05','03',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(40,'JZ01-MIC50','千分尺50mm','25-50mm/0.001',3,1,1,'05','04',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(41,'JZ01-DT200','深度尺200mm','0-200mm',3,1,1,'05','05',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(42,'JZ01-PRO300','万能角度尺','0-320°',3,1,1,'05','06',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(43,'JZ01-TG300','螺纹规公制','M3-M12',3,1,1,'05','07',1,1,'套',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(44,'JZ01-R500','塞尺500mm','0.02-1mm',3,1,1,'05','08',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(45,'JZ01-LEV300','水平仪300mm','300mm',3,1,1,'05','09',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(46,'JZ01-DCL100','百分表100mm','0-10mm/0.01',3,1,1,'05','10',1,1,'块',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(47,'JZ01-DR12','充电式电钻12V','12V',1,1,1,'06','01',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(48,'JZ01-DR18','充电式电钻18V','18V',1,1,1,'06','02',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(49,'JZ01-GR4','角磨机100mm','100mm/720W',1,1,1,'06','03',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(50,'JZ01-GR5','角磨机125mm','125mm/850W',1,1,1,'06','04',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(51,'JZ01-CT3','电锤3功能','26mm/800W',1,1,1,'06','05',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(52,'JZ01-TR500','热风枪500W','50-650°C',1,1,1,'06','06',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(53,'JZ01-WL120','电焊机120A','120A逆变',1,1,1,'06','07',1,1,'台',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(54,'JZ01-CT60','切割机600W','355mm/1400W',1,1,1,'06','08',1,1,'台',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(55,'JZ01-DC100','除尘枪','100PSI',1,1,1,'06','09',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(56,'JZ01-PMP12','电动黄油枪','12V',1,1,1,'06','10',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(57,'JZ01-JK10','液压千斤顶10T','10T',2,1,1,'07','01',1,1,'台',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(58,'JZ01-JK20','液压千斤顶20T','20T',2,1,1,'07','02',1,1,'台',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(59,'JZ01-PC50','液压拉马','50T',2,1,1,'07','03',1,0,'套',NULL,'','checked_out','系统修复-补录出库原因',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(60,'JZ01-PT10','液压压力表','0-100MPa',2,1,1,'07','04',1,1,'块',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(61,'JZ01-PH15','液压软管15M','DN15',2,1,1,'07','05',1,1,'根',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(62,'JZ01-PH20','液压软管20M','DN20',2,1,1,'07','06',1,0,'根',NULL,'','borrowed','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(63,'JZ01-JC1','液压油桶','20L',2,1,1,'07','07',1,1,'桶',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(64,'JZ01-OIL46','抗磨液压油','ISO VG46',2,1,1,'07','08',1,1,'桶',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(65,'JZ01-OIL68','抗磨液压油','ISO VG68',2,1,1,'07','09',1,1,'桶',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(66,'JZ01-FH10','液压过滤器','HX-10',2,1,1,'07','10',1,1,'个',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(67,'JZ01-WG200','焊枪200A','200A',4,1,1,'08','01',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(68,'JZ01-WG315','焊枪315A','315A',4,1,1,'08','02',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(69,'JZ01-ER70','焊条ER70','Φ2.5/Φ3.2',4,1,1,'08','03',1,1,'盒',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(70,'JZ01-ER80','焊条ER80','Φ4.0',4,1,1,'08','04',1,1,'盒',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(71,'JZ01-WM3','焊接面罩自动变光','3-13级',4,1,1,'08','05',1,1,'个',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(72,'JZ01-WL100','焊锡丝','Φ1.0mm/100g',4,1,1,'08','06',1,1,'卷',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(73,'JZ01-GVN','绝缘手套','10kV',2,1,1,'09','01',1,1,'双',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(74,'JZ01-GVL','皮手套','加厚',2,1,1,'09','02',1,1,'双',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(75,'JZ01-GLL','棉纱手套','12支',2,1,1,'09','03',1,1,'双',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(76,'JZ01-GOG','防护眼镜','防冲击',2,1,1,'09','04',1,1,'副',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(77,'JZ01-MASK','防尘口罩','KN95',2,1,1,'09','05',1,1,'个',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(78,'JZ01-HEL','安全帽','V型',2,1,1,'09','06',1,1,'顶',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(79,'JZ01-VEST','反光背心','黄/橙',2,1,1,'09','07',1,1,'件',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(80,'JZ01-EBT','安全带','五点式',2,1,1,'09','08',1,1,'套',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(81,'JZ01-EAR','耳塞','NRR33dB',2,1,1,'09','09',1,1,'副',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(82,'JZ01-FIR','灭火器','4kg干粉',2,1,1,'09','10',1,1,'个',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(83,'JZ01-MUL1','数字万用表','6000字',3,1,1,'10','01',1,1,'块',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(84,'JZ01-MUL2','钳形万用表','600A',3,1,1,'10','02',1,1,'块',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(85,'JZ01-INS1','绝缘电阻测试仪','1000V',3,1,1,'10','03',1,1,'台',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(86,'JZ01-INS2','接地电阻测试仪','0-200Ω',3,1,1,'10','04',1,1,'台',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(87,'JZ01-PEN','验电笔','100-500V',3,1,1,'10','05',1,1,'支',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(88,'JZ01-PEN2','高压验电笔','10kV',3,1,1,'10','06',1,1,'支',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(89,'JZ01-SCOPE','示波器','100MHz',3,1,1,'10','07',1,1,'台',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(90,'JZ01-PSU','直流稳压电源','30V/3A',3,1,1,'10','08',1,1,'台',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(91,'JZ01-SIG','信号发生器','1Hz-10MHz',3,1,1,'10','09',1,1,'台',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(92,'JZ01-RLY','继电器测试仪','通用型',3,1,1,'10','10',1,1,'台',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(93,'JZ01-DRL3','麻花钻3mm','HSS Φ3',7,1,1,'11','01',1,1,'支',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(94,'JZ01-DRL6','麻花钻6mm','HSS Φ6',7,1,1,'11','02',1,1,'支',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(95,'JZ01-DRL10','麻花钻10mm','HSS Φ10',7,1,1,'11','03',1,1,'支',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(96,'JZ01-TAP6','丝锥M6','HSS M6',7,1,1,'11','04',1,1,'支',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(97,'JZ01-TAP8','丝锥M8','HSS M8',7,1,1,'11','05',1,1,'支',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(98,'JZ01-MIL6','铣刀6mm','硬质合金Φ6',7,1,1,'11','06',1,1,'支',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(99,'JZ01-MIL10','铣刀10mm','硬质合金Φ10',7,1,1,'11','07',1,1,'支',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(100,'JZ01-SAW1','手锯条','300mm 24T',7,1,1,'11','08',1,1,'根',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(101,'JZ01-LIGHT','LED工作灯','10W可充电',5,1,1,'12','01',1,1,'个',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(102,'JZ01-FLASH','强光手电','500流明',5,1,1,'12','02',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(103,'JZ01-BAG','工具包','17件套',2,1,1,'12','03',1,1,'个',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(104,'JZ01-BOX','零件盒','400×300mm',2,1,1,'12','04',1,1,'个',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','C:/Users/25007/AppData/Roaming/SmartCabinet/QtSmartCabinet/tool_documents/20260628080706_11.doc',''),(105,'JZ01-MAG','磁性拾取器','600mm/2kg',2,1,1,'12','05',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(106,'JZ01-CLOTH','清洁布','超细纤维',2,1,1,'12','06',1,1,'条',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-09-24 10:09:41','vision','',''),(123,'test','test','test',1,1,1,'06','14',8,0,'把','史丹利工具','','borrowed','',0,'2026-06-30 19:03:10','2026-09-24 10:09:41','vision','C:/Users/25007/AppData/Roaming/SmartCabinet/QtSmartCabinet/tool_documents/20260630190308_智能工具柜管理系统_部署手册_V2.00.docx',''),(124,'testnew','testnew','testnew',1,NULL,1,'','',1,1,'把','史丹利工具','','in_stock','',0,'2026-07-04 17:34:08','2026-09-24 10:09:41','vision','C:/Users/25007/AppData/Roaming/SmartCabinet/QtSmartCabinet/tool_documents/20260704173405_11.doc','');
/*!40000 ALTER TABLE `tool_info` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `tool_info_bak_20260924`
--

DROP TABLE IF EXISTS `tool_info_bak_20260924`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `tool_info_bak_20260924` (
  `tool_id` int(11) NOT NULL DEFAULT '0' COMMENT '工具主键ID',
  `tool_code` varchar(32) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '工具编号，如JZ01-CDQ(班组ID-规格缩写)',
  `tool_name` varchar(128) COLLATE utf8mb4_unicode_ci NOT NULL COMMENT '工具名称',
  `spec` varchar(64) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '规格型号',
  `category_id` int(11) DEFAULT NULL COMMENT '分类ID，关联tool_category',
  `cabinet_id` int(11) DEFAULT NULL COMMENT '所属柜体ID，关联tool_cabinet',
  `machine_group_id` int(11) DEFAULT NULL COMMENT '所属机组ID',
  `layer` varchar(8) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '所在层号',
  `position` varchar(16) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '具体位置',
  `total_qty` int(11) NOT NULL DEFAULT '0' COMMENT '总数量',
  `current_qty` int(11) NOT NULL DEFAULT '0' COMMENT '当前在库数量，借用时扣减/归还时增加',
  `unit` varchar(8) COLLATE utf8mb4_unicode_ci DEFAULT '件',
  `supplier` varchar(128) COLLATE utf8mb4_unicode_ci DEFAULT NULL COMMENT '供应商',
  `rfid_tag` varchar(64) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT 'RFID标签编号',
  `status` varchar(16) COLLATE utf8mb4_unicode_ci DEFAULT 'in_stock' COMMENT '状态：in_stock在库 borrowed借出 maintenance维修中',
  `checkout_reason` varchar(64) COLLATE utf8mb4_unicode_ci DEFAULT '' COMMENT '最近出库原因',
  `is_recommended` tinyint(4) DEFAULT '0' COMMENT '是否推荐工具：1是 0否',
  `created_at` datetime DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
  `updated_at` datetime DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
  `recognition_method` varchar(16) COLLATE utf8mb4_unicode_ci DEFAULT 'rfid',
  `document_path` varchar(512) COLLATE utf8mb4_unicode_ci DEFAULT ''
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `tool_info_bak_20260924`
--

LOCK TABLES `tool_info_bak_20260924` WRITE;
/*!40000 ALTER TABLE `tool_info_bak_20260924` DISABLE KEYS */;
INSERT INTO `tool_info_bak_20260924` VALUES (1,'JZ01-CDQ','充电式电动解锥','12V 锂电',1,1,1,'01','01',1,1,'件',NULL,'RFID-001','in_stock','',1,'2026-06-03 10:46:32','2026-07-03 17:15:25','rfid',''),(2,'JZ01-KB','9# 开口扳手','CR-V 9mm',2,1,1,'01','05',1,1,'件',NULL,'RFID-002','in_stock','',1,'2026-06-03 10:46:32','2026-07-03 17:15:25','rfid',''),(3,'JZ01-BX','保险丝钳','8寸 绝缘',2,1,1,'01','15',1,1,'件',NULL,'RFID-003','in_stock','',1,'2026-06-03 10:46:32','2026-07-03 17:15:25','rfid',''),(4,'JZ01-PH2','十字解锥头 2#','PH2-100mm',2,1,2,'03','12',1,1,'件',NULL,'RFID-004','in_stock','',0,'2026-06-03 10:46:32','2026-06-29 19:40:53','rfid',''),(5,'JZ01-NLJ','内六角扳手','6mm 内六角',2,1,3,'01','06',1,0,'件',NULL,'RFID-005','borrowed','',1,'2026-06-03 10:46:32','2026-07-05 05:42:15','rfid',''),(6,'JZ01-SB','塞尺','0.02-1.0mm',3,NULL,3,NULL,NULL,1,0,'件',NULL,'RFID-006','pending','',0,'2026-06-03 10:46:32','2026-06-30 11:04:39','rfid',''),(7,'JZ01-DB','电工刀','折叠式 绝缘',7,1,2,'01','09',1,0,'件',NULL,'RFID-007','borrowed','',0,'2026-06-03 10:46:32','2026-07-05 05:42:15','rfid',''),(8,'JZ01-YQ','压线钳','0.25-10mm²',2,1,2,'02','07',1,1,'件',NULL,'RFID-008','in_stock','',0,'2026-06-03 10:46:32','2026-06-29 19:40:53','rfid',''),(9,'JZ01-CZ','锤子','1.5磅 纤维柄',2,1,3,'03','14',1,1,'件',NULL,'RFID-009','in_stock','',0,'2026-06-03 10:46:32','2026-06-29 19:40:53','rfid',''),(10,'JZ01-TH','套筒扳手组','8-32mm 套装',6,1,1,'02','13',1,1,'件',NULL,'RFID-010','in_stock','',1,'2026-06-03 10:46:32','2026-07-03 17:15:25','rfid','C:/Users/25007/AppData/Roaming/SmartCabinet/QtSmartCabinet/tool_documents/20260629090723_11.doc'),(11,'JZ01-WY','万用表','数字DT9205A',3,1,2,'02','01',1,1,'件',NULL,'RFID-011','in_stock','',0,'2026-06-03 10:46:32','2026-06-29 19:40:53','rfid',''),(12,'JZ01-CRV','开口扳手（20×22）','CR-V 20×22',2,NULL,3,NULL,NULL,1,0,'件',NULL,'RFID-012','pending','',0,'2026-06-03 10:46:32','2026-07-05 06:49:44','rfid',''),(13,'JZ02-ZD','十字螺丝刀','3×150mm',2,2,5,'01','03',1,1,'件',NULL,'RFID-013','in_stock','',0,'2026-06-03 10:46:32','2026-06-29 19:40:53','rfid',''),(14,'JZ02-YZ','一字解锥头','8mm 工业级',2,2,4,'02','12',1,0,'件',NULL,'RFID-014','borrowed','',0,'2026-06-03 10:46:32','2026-07-05 05:42:15','rfid',''),(15,'JZ02-JL','棘轮扳手','10mm 棘轮',2,2,4,'02','03',1,1,'件',NULL,'RFID-015','in_stock','',0,'2026-06-03 10:46:32','2026-06-29 19:40:53','rfid',''),(16,'JZ02-HQ','焊枪','30W 恒温',4,2,5,'03','07',1,1,'件',NULL,'RFID-016','in_stock','',0,'2026-06-03 10:46:32','2026-06-29 19:40:53','rfid',''),(17,'JZ02-BZ','剥线钳','0.5-6mm²',2,2,5,'01','14',1,1,'件',NULL,'RFID-017','in_stock','',0,'2026-06-03 10:46:32','2026-06-29 19:40:53','rfid',''),(18,'JZ02-SG','手锯','12寸 可调',7,2,4,'01','11',1,1,'件',NULL,'RFID-018','in_stock','',0,'2026-06-03 10:46:32','2026-06-29 19:40:53','rfid',''),(19,'JZ02-CL','游标卡尺','0-150mm',3,2,7,'02','06',1,1,'件',NULL,'RFID-019','in_stock','',0,'2026-06-03 10:46:32','2026-06-29 19:40:53','rfid',''),(20,'JZ02-DS','电刷','铜丝 工业级',2,2,6,'01','08',1,1,'件',NULL,'RFID-020','in_stock','',0,'2026-06-03 10:46:32','2026-06-29 19:40:53','rfid',''),(21,'JZ03-QG','强光手电','LED-1000LM',5,2,6,'03','10',1,1,'件',NULL,'RFID-021','in_stock','',0,'2026-06-03 10:46:32','2026-06-29 19:40:53','rfid',''),(22,'JZ03-RF','热风枪','2000W 温控',4,3,6,'01','11',1,0,'件',NULL,'RFID-022','borrowed','',1,'2026-06-03 10:46:32','2026-07-05 05:42:15','rfid',''),(23,'JZ03-JQ','剪刀','6寸 不锈钢',7,3,8,'03','02',1,1,'件',NULL,'RFID-023','in_stock','',0,'2026-06-03 10:46:32','2026-06-29 19:40:53','rfid',''),(24,'JZ03-DJ','电烙铁','60W 调温',4,3,6,'02','09',1,1,'件',NULL,'RFID-024','in_stock','',0,'2026-06-03 10:46:32','2026-06-29 19:40:53','rfid',''),(25,'JZ03-YG','验电笔','100-500V',3,3,2,'01','02',1,1,'件',NULL,'RFID-025','in_stock','',0,'2026-06-03 10:46:32','2026-06-29 19:40:53','rfid',''),(26,'JZ03-XY','吸锡器','铝合金 30W',4,3,7,'03','05',1,1,'件',NULL,'RFID-026','in_stock','',0,'2026-06-03 10:46:32','2026-06-29 19:40:53','rfid',''),(27,'JZ01-M6','M6不锈钢螺栓','M6×30mm',6,1,1,'04','01',1,1,'件',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(28,'JZ01-M8','M8不锈钢螺栓','M8×40mm',6,1,1,'04','02',1,1,'件',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(29,'JZ01-M10','M10高强度螺栓','M10×50mm',6,1,1,'04','03',1,1,'件',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(30,'JZ01-M12','M12高强度螺栓','M12×60mm',6,1,1,'04','04',1,1,'件',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(31,'JZ01-N6','M6自锁螺母','M6',6,1,1,'04','05',1,1,'件',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(32,'JZ01-N8','M8自锁螺母','M8',6,1,1,'04','06',1,1,'件',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(33,'JZ01-N10','M10自锁螺母','M10',6,1,1,'04','07',1,1,'件',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(34,'JZ01-W6','M6平垫圈','M6',6,1,1,'04','08',1,1,'件',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(35,'JZ01-W8','M8弹簧垫圈','M8',6,1,1,'04','09',1,1,'件',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(36,'JZ01-W10','M10弹簧垫圈','M10',6,1,1,'04','10',1,1,'件',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(37,'JZ01-CAL150','游标卡尺150mm','0-150mm/0.02',3,1,1,'05','01',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(38,'JZ01-CAL300','游标卡尺300mm','0-300mm/0.02',3,1,1,'05','02',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(39,'JZ01-MIC25','千分尺25mm','0-25mm/0.001',3,1,1,'05','03',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(40,'JZ01-MIC50','千分尺50mm','25-50mm/0.001',3,1,1,'05','04',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(41,'JZ01-DT200','深度尺200mm','0-200mm',3,1,1,'05','05',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(42,'JZ01-PRO300','万能角度尺','0-320°',3,1,1,'05','06',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(43,'JZ01-TG300','螺纹规公制','M3-M12',3,1,1,'05','07',1,1,'套',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(44,'JZ01-R500','塞尺500mm','0.02-1mm',3,1,1,'05','08',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(45,'JZ01-LEV300','水平仪300mm','300mm',3,1,1,'05','09',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(46,'JZ01-DCL100','百分表100mm','0-10mm/0.01',3,1,1,'05','10',1,1,'块',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(47,'JZ01-DR12','充电式电钻12V','12V',1,1,1,'06','01',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(48,'JZ01-DR18','充电式电钻18V','18V',1,1,1,'06','02',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(49,'JZ01-GR4','角磨机100mm','100mm/720W',1,1,1,'06','03',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(50,'JZ01-GR5','角磨机125mm','125mm/850W',1,1,1,'06','04',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(51,'JZ01-CT3','电锤3功能','26mm/800W',1,1,1,'06','05',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(52,'JZ01-TR500','热风枪500W','50-650°C',1,1,1,'06','06',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(53,'JZ01-WL120','电焊机120A','120A逆变',1,1,1,'06','07',1,1,'台',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(54,'JZ01-CT60','切割机600W','355mm/1400W',1,1,1,'06','08',1,1,'台',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(55,'JZ01-DC100','除尘枪','100PSI',1,1,1,'06','09',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(56,'JZ01-PMP12','电动黄油枪','12V',1,1,1,'06','10',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(57,'JZ01-JK10','液压千斤顶10T','10T',2,1,1,'07','01',1,1,'台',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(58,'JZ01-JK20','液压千斤顶20T','20T',2,1,1,'07','02',1,1,'台',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(59,'JZ01-PC50','液压拉马','50T',2,1,1,'07','03',1,0,'套',NULL,'','checked_out','系统修复-补录出库原因',0,'2026-06-27 12:38:26','2026-07-05 05:42:17','rfid',''),(60,'JZ01-PT10','液压压力表','0-100MPa',2,1,1,'07','04',1,1,'块',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(61,'JZ01-PH15','液压软管15M','DN15',2,1,1,'07','05',1,1,'根',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(62,'JZ01-PH20','液压软管20M','DN20',2,1,1,'07','06',1,0,'根',NULL,'','borrowed','',0,'2026-06-27 12:38:26','2026-07-05 05:42:15','rfid',''),(63,'JZ01-JC1','液压油桶','20L',2,1,1,'07','07',1,1,'桶',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(64,'JZ01-OIL46','抗磨液压油','ISO VG46',2,1,1,'07','08',1,1,'桶',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(65,'JZ01-OIL68','抗磨液压油','ISO VG68',2,1,1,'07','09',1,1,'桶',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(66,'JZ01-FH10','液压过滤器','HX-10',2,1,1,'07','10',1,1,'个',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(67,'JZ01-WG200','焊枪200A','200A',4,1,1,'08','01',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(68,'JZ01-WG315','焊枪315A','315A',4,1,1,'08','02',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(69,'JZ01-ER70','焊条ER70','Φ2.5/Φ3.2',4,1,1,'08','03',1,1,'盒',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(70,'JZ01-ER80','焊条ER80','Φ4.0',4,1,1,'08','04',1,1,'盒',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(71,'JZ01-WM3','焊接面罩自动变光','3-13级',4,1,1,'08','05',1,1,'个',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(72,'JZ01-WL100','焊锡丝','Φ1.0mm/100g',4,1,1,'08','06',1,1,'卷',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(73,'JZ01-GVN','绝缘手套','10kV',2,1,1,'09','01',1,1,'双',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(74,'JZ01-GVL','皮手套','加厚',2,1,1,'09','02',1,1,'双',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(75,'JZ01-GLL','棉纱手套','12支',2,1,1,'09','03',1,1,'双',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(76,'JZ01-GOG','防护眼镜','防冲击',2,1,1,'09','04',1,1,'副',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(77,'JZ01-MASK','防尘口罩','KN95',2,1,1,'09','05',1,1,'个',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(78,'JZ01-HEL','安全帽','V型',2,1,1,'09','06',1,1,'顶',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(79,'JZ01-VEST','反光背心','黄/橙',2,1,1,'09','07',1,1,'件',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(80,'JZ01-EBT','安全带','五点式',2,1,1,'09','08',1,1,'套',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(81,'JZ01-EAR','耳塞','NRR33dB',2,1,1,'09','09',1,1,'副',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(82,'JZ01-FIR','灭火器','4kg干粉',2,1,1,'09','10',1,1,'个',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(83,'JZ01-MUL1','数字万用表','6000字',3,1,1,'10','01',1,1,'块',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(84,'JZ01-MUL2','钳形万用表','600A',3,1,1,'10','02',1,1,'块',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(85,'JZ01-INS1','绝缘电阻测试仪','1000V',3,1,1,'10','03',1,1,'台',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(86,'JZ01-INS2','接地电阻测试仪','0-200Ω',3,1,1,'10','04',1,1,'台',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(87,'JZ01-PEN','验电笔','100-500V',3,1,1,'10','05',1,1,'支',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(88,'JZ01-PEN2','高压验电笔','10kV',3,1,1,'10','06',1,1,'支',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(89,'JZ01-SCOPE','示波器','100MHz',3,1,1,'10','07',1,1,'台',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(90,'JZ01-PSU','直流稳压电源','30V/3A',3,1,1,'10','08',1,1,'台',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(91,'JZ01-SIG','信号发生器','1Hz-10MHz',3,1,1,'10','09',1,1,'台',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(92,'JZ01-RLY','继电器测试仪','通用型',3,1,1,'10','10',1,1,'台',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(93,'JZ01-DRL3','麻花钻3mm','HSS Φ3',7,1,1,'11','01',1,1,'支',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(94,'JZ01-DRL6','麻花钻6mm','HSS Φ6',7,1,1,'11','02',1,1,'支',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(95,'JZ01-DRL10','麻花钻10mm','HSS Φ10',7,1,1,'11','03',1,1,'支',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(96,'JZ01-TAP6','丝锥M6','HSS M6',7,1,1,'11','04',1,1,'支',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(97,'JZ01-TAP8','丝锥M8','HSS M8',7,1,1,'11','05',1,1,'支',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(98,'JZ01-MIL6','铣刀6mm','硬质合金Φ6',7,1,1,'11','06',1,1,'支',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(99,'JZ01-MIL10','铣刀10mm','硬质合金Φ10',7,1,1,'11','07',1,1,'支',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(100,'JZ01-SAW1','手锯条','300mm 24T',7,1,1,'11','08',1,1,'根',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(101,'JZ01-LIGHT','LED工作灯','10W可充电',5,1,1,'12','01',1,1,'个',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(102,'JZ01-FLASH','强光手电','500流明',5,1,1,'12','02',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(103,'JZ01-BAG','工具包','17件套',2,1,1,'12','03',1,1,'个',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(104,'JZ01-BOX','零件盒','400×300mm',2,1,1,'12','04',1,1,'个',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid','C:/Users/25007/AppData/Roaming/SmartCabinet/QtSmartCabinet/tool_documents/20260628080706_11.doc'),(105,'JZ01-MAG','磁性拾取器','600mm/2kg',2,1,1,'12','05',1,1,'把',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(106,'JZ01-CLOTH','清洁布','超细纤维',2,1,1,'12','06',1,1,'条',NULL,'','in_stock','',0,'2026-06-27 12:38:26','2026-06-29 19:40:53','rfid',''),(123,'test','test','test',1,1,1,'06','14',8,0,'把','史丹利工具','','borrowed','',0,'2026-06-30 19:03:10','2026-07-05 05:42:15','rfid','C:/Users/25007/AppData/Roaming/SmartCabinet/QtSmartCabinet/tool_documents/20260630190308_智能工具柜管理系统_部署手册_V2.00.docx'),(124,'testnew','testnew','testnew',1,NULL,1,'','',1,1,'把','史丹利工具','','in_stock','',0,'2026-07-04 17:34:08','2026-07-04 17:39:16','rfid','C:/Users/25007/AppData/Roaming/SmartCabinet/QtSmartCabinet/tool_documents/20260704173405_11.doc');
/*!40000 ALTER TABLE `tool_info_bak_20260924` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Table structure for table `tool_position_mapping`
--

DROP TABLE IF EXISTS `tool_position_mapping`;
/*!40101 SET @saved_cs_client     = @@character_set_client */;
/*!40101 SET character_set_client = utf8 */;
CREATE TABLE `tool_position_mapping` (
  `mapping_id` int(11) NOT NULL AUTO_INCREMENT,
  `tool_id` int(11) NOT NULL,
  `cabinet_id` int(11) NOT NULL,
  `layer` varchar(16) COLLATE utf8mb4_unicode_ci NOT NULL,
  `position` varchar(16) COLLATE utf8mb4_unicode_ci NOT NULL,
  `created_at` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `status` varchar(16) COLLATE utf8mb4_unicode_ci NOT NULL DEFAULT 'pending',
  PRIMARY KEY (`mapping_id`),
  UNIQUE KEY `cabinet_id` (`cabinet_id`,`layer`,`position`),
  KEY `tool_id` (`tool_id`),
  CONSTRAINT `tool_position_mapping_ibfk_1` FOREIGN KEY (`tool_id`) REFERENCES `tool_info` (`tool_id`),
  CONSTRAINT `tool_position_mapping_ibfk_2` FOREIGN KEY (`cabinet_id`) REFERENCES `tool_cabinet` (`cabinet_id`)
) ENGINE=InnoDB AUTO_INCREMENT=188 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
/*!40101 SET character_set_client = @saved_cs_client */;

--
-- Dumping data for table `tool_position_mapping`
--

LOCK TABLES `tool_position_mapping` WRITE;
/*!40000 ALTER TABLE `tool_position_mapping` DISABLE KEYS */;
INSERT INTO `tool_position_mapping` VALUES (1,1,1,'01','01','2026-06-30 06:19:12','in_stock'),(2,2,1,'01','05','2026-06-30 06:19:12','in_stock'),(3,3,1,'01','15','2026-06-30 06:19:12','in_stock'),(4,4,1,'03','12','2026-06-30 06:19:12','in_stock'),(5,5,1,'01','06','2026-06-30 06:19:12','borrowed'),(6,7,1,'01','09','2026-06-30 06:19:12','borrowed'),(7,8,1,'02','07','2026-06-30 06:19:12','in_stock'),(8,9,1,'03','14','2026-06-30 06:19:12','in_stock'),(9,10,1,'02','13','2026-06-30 06:19:12','in_stock'),(10,11,1,'02','01','2026-06-30 06:19:12','in_stock'),(11,13,2,'01','03','2026-06-30 06:19:12','in_stock'),(12,14,2,'02','12','2026-06-30 06:19:12','borrowed'),(13,15,2,'02','03','2026-06-30 06:19:12','in_stock'),(14,16,2,'03','07','2026-06-30 06:19:12','in_stock'),(15,17,2,'01','14','2026-06-30 06:19:12','in_stock'),(16,18,2,'01','11','2026-06-30 06:19:12','in_stock'),(17,19,2,'02','06','2026-06-30 06:19:12','in_stock'),(18,20,2,'01','08','2026-06-30 06:19:12','in_stock'),(19,21,2,'03','10','2026-06-30 06:19:12','in_stock'),(20,22,3,'01','11','2026-06-30 06:19:12','borrowed'),(21,23,3,'03','02','2026-06-30 06:19:12','in_stock'),(22,24,3,'02','09','2026-06-30 06:19:12','in_stock'),(23,25,3,'01','02','2026-06-30 06:19:12','in_stock'),(24,26,3,'03','05','2026-06-30 06:19:12','in_stock'),(25,27,1,'04','01','2026-06-30 06:19:12','in_stock'),(26,28,1,'04','02','2026-06-30 06:19:12','in_stock'),(27,29,1,'04','03','2026-06-30 06:19:12','in_stock'),(28,30,1,'04','04','2026-06-30 06:19:12','in_stock'),(29,31,1,'04','05','2026-06-30 06:19:12','in_stock'),(30,32,1,'04','06','2026-06-30 06:19:12','in_stock'),(31,33,1,'04','07','2026-06-30 06:19:12','in_stock'),(32,34,1,'04','08','2026-06-30 06:19:12','in_stock'),(33,35,1,'04','09','2026-06-30 06:19:12','in_stock'),(34,36,1,'04','10','2026-06-30 06:19:12','in_stock'),(35,37,1,'05','01','2026-06-30 06:19:12','in_stock'),(36,38,1,'05','02','2026-06-30 06:19:12','in_stock'),(37,39,1,'05','03','2026-06-30 06:19:12','in_stock'),(38,40,1,'05','04','2026-06-30 06:19:12','in_stock'),(39,41,1,'05','05','2026-06-30 06:19:12','in_stock'),(40,42,1,'05','06','2026-06-30 06:19:12','in_stock'),(41,43,1,'05','07','2026-06-30 06:19:12','in_stock'),(42,44,1,'05','08','2026-06-30 06:19:12','in_stock'),(43,45,1,'05','09','2026-06-30 06:19:12','in_stock'),(44,46,1,'05','10','2026-06-30 06:19:12','in_stock'),(45,47,1,'06','01','2026-06-30 06:19:12','in_stock'),(46,48,1,'06','02','2026-06-30 06:19:12','in_stock'),(47,49,1,'06','03','2026-06-30 06:19:12','in_stock'),(48,50,1,'06','04','2026-06-30 06:19:12','in_stock'),(49,51,1,'06','05','2026-06-30 06:19:12','in_stock'),(50,52,1,'06','06','2026-06-30 06:19:12','in_stock'),(51,53,1,'06','07','2026-06-30 06:19:12','in_stock'),(52,54,1,'06','08','2026-06-30 06:19:12','in_stock'),(53,55,1,'06','09','2026-06-30 06:19:12','in_stock'),(54,56,1,'06','10','2026-06-30 06:19:12','in_stock'),(55,57,1,'07','01','2026-06-30 06:19:12','in_stock'),(56,58,1,'07','02','2026-06-30 06:19:12','in_stock'),(58,60,1,'07','04','2026-06-30 06:19:12','in_stock'),(59,61,1,'07','05','2026-06-30 06:19:12','in_stock'),(60,62,1,'07','06','2026-06-30 06:19:12','borrowed'),(61,63,1,'07','07','2026-06-30 06:19:12','in_stock'),(62,64,1,'07','08','2026-06-30 06:19:12','in_stock'),(63,65,1,'07','09','2026-06-30 06:19:12','in_stock'),(64,66,1,'07','10','2026-06-30 06:19:12','in_stock'),(65,67,1,'08','01','2026-06-30 06:19:12','in_stock'),(66,68,1,'08','02','2026-06-30 06:19:12','in_stock'),(67,69,1,'08','03','2026-06-30 06:19:12','in_stock'),(68,70,1,'08','04','2026-06-30 06:19:12','in_stock'),(69,71,1,'08','05','2026-06-30 06:19:12','in_stock'),(70,72,1,'08','06','2026-06-30 06:19:12','in_stock'),(71,73,1,'09','01','2026-06-30 06:19:12','in_stock'),(72,74,1,'09','02','2026-06-30 06:19:12','in_stock'),(73,75,1,'09','03','2026-06-30 06:19:12','in_stock'),(74,76,1,'09','04','2026-06-30 06:19:12','in_stock'),(75,77,1,'09','05','2026-06-30 06:19:12','in_stock'),(76,78,1,'09','06','2026-06-30 06:19:12','in_stock'),(77,79,1,'09','07','2026-06-30 06:19:12','in_stock'),(78,80,1,'09','08','2026-06-30 06:19:12','in_stock'),(79,81,1,'09','09','2026-06-30 06:19:12','in_stock'),(80,82,1,'09','10','2026-06-30 06:19:12','in_stock'),(81,83,1,'10','01','2026-06-30 06:19:12','in_stock'),(82,84,1,'10','02','2026-06-30 06:19:12','in_stock'),(83,85,1,'10','03','2026-06-30 06:19:12','in_stock'),(84,86,1,'10','04','2026-06-30 06:19:12','in_stock'),(85,87,1,'10','05','2026-06-30 06:19:12','in_stock'),(86,88,1,'10','06','2026-06-30 06:19:12','in_stock'),(87,89,1,'10','07','2026-06-30 06:19:12','in_stock'),(88,90,1,'10','08','2026-06-30 06:19:12','in_stock'),(89,91,1,'10','09','2026-06-30 06:19:12','in_stock'),(90,92,1,'10','10','2026-06-30 06:19:12','in_stock'),(91,93,1,'11','01','2026-06-30 06:19:12','in_stock'),(92,94,1,'11','02','2026-06-30 06:19:12','in_stock'),(93,95,1,'11','03','2026-06-30 06:19:12','in_stock'),(94,96,1,'11','04','2026-06-30 06:19:12','in_stock'),(95,97,1,'11','05','2026-06-30 06:19:12','in_stock'),(96,98,1,'11','06','2026-06-30 06:19:12','in_stock'),(97,99,1,'11','07','2026-06-30 06:19:12','in_stock'),(98,100,1,'11','08','2026-06-30 06:19:12','in_stock'),(99,101,1,'12','01','2026-06-30 06:19:12','in_stock'),(100,102,1,'12','02','2026-06-30 06:19:12','in_stock'),(101,103,1,'12','03','2026-06-30 06:19:12','in_stock'),(102,104,1,'12','04','2026-06-30 06:19:12','in_stock'),(103,105,1,'12','05','2026-06-30 06:19:12','in_stock'),(104,106,1,'12','06','2026-06-30 06:19:12','in_stock'),(158,123,1,'06','14','2026-06-30 11:03:40','in_stock'),(174,124,3,'07','01','2026-07-04 09:34:27','in_stock'),(183,59,1,'07','03','2026-07-04 21:53:41','pending');
/*!40000 ALTER TABLE `tool_position_mapping` ENABLE KEYS */;
UNLOCK TABLES;

--
-- Temporary table structure for view `v_borrowing_tools`
--

DROP TABLE IF EXISTS `v_borrowing_tools`;
/*!50001 DROP VIEW IF EXISTS `v_borrowing_tools`*/;
SET @saved_cs_client     = @@character_set_client;
SET character_set_client = utf8;
/*!50001 CREATE VIEW `v_borrowing_tools` AS SELECT 
 1 AS `record_id`,
 1 AS `flow_no`,
 1 AS `borrow_time`,
 1 AS `borrow_qty`,
 1 AS `borrow_reason`,
 1 AS `status`,
 1 AS `borrower_name`,
 1 AS `borrower_work_no`,
 1 AS `department`,
 1 AS `tool_code`,
 1 AS `tool_name`,
 1 AS `spec`,
 1 AS `cabinet_id`,
 1 AS `cabinet_name`,
 1 AS `position_desc`*/;
SET character_set_client = @saved_cs_client;

--
-- Temporary table structure for view `v_dashboard_stats`
--

DROP TABLE IF EXISTS `v_dashboard_stats`;
/*!50001 DROP VIEW IF EXISTS `v_dashboard_stats`*/;
SET @saved_cs_client     = @@character_set_client;
SET character_set_client = utf8;
/*!50001 CREATE VIEW `v_dashboard_stats` AS SELECT 
 1 AS `total_tools`,
 1 AS `in_stock_qty`,
 1 AS `borrowing_count`,
 1 AS `unhandled_alerts`*/;
SET character_set_client = @saved_cs_client;

--
-- Temporary table structure for view `v_tool_latest_operation`
--

DROP TABLE IF EXISTS `v_tool_latest_operation`;
/*!50001 DROP VIEW IF EXISTS `v_tool_latest_operation`*/;
SET @saved_cs_client     = @@character_set_client;
SET character_set_client = utf8;
/*!50001 CREATE VIEW `v_tool_latest_operation` AS SELECT 
 1 AS `tool_id`,
 1 AS `latest_op_time`,
 1 AS `latest_op_type`,
 1 AS `latest_op_user`*/;
SET character_set_client = @saved_cs_client;

--
-- Temporary table structure for view `v_tool_stats`
--

DROP TABLE IF EXISTS `v_tool_stats`;
/*!50001 DROP VIEW IF EXISTS `v_tool_stats`*/;
SET @saved_cs_client     = @@character_set_client;
SET character_set_client = utf8;
/*!50001 CREATE VIEW `v_tool_stats` AS SELECT 
 1 AS `total_tools`,
 1 AS `in_stock_count`,
 1 AS `borrowed_count`,
 1 AS `maintenance_count`,
 1 AS `total_qty`,
 1 AS `in_stock_qty`,
 1 AS `borrowed_qty`*/;
SET character_set_client = @saved_cs_client;

--
-- Dumping routines for database 'smart_cabinet'
--

--
-- Current Database: `smart_cabinet`
--

USE `smart_cabinet`;

--
-- Final view structure for view `v_borrowing_tools`
--

/*!50001 DROP VIEW IF EXISTS `v_borrowing_tools`*/;
/*!50001 SET @saved_cs_client          = @@character_set_client */;
/*!50001 SET @saved_cs_results         = @@character_set_results */;
/*!50001 SET @saved_col_connection     = @@collation_connection */;
/*!50001 SET character_set_client      = utf8mb4 */;
/*!50001 SET character_set_results     = utf8mb4 */;
/*!50001 SET collation_connection      = utf8mb4_general_ci */;
/*!50001 CREATE ALGORITHM=UNDEFINED */
/*!50013 DEFINER=`root`@`localhost` SQL SECURITY DEFINER */
/*!50001 VIEW `v_borrowing_tools` AS select `br`.`record_id` AS `record_id`,`br`.`flow_no` AS `flow_no`,`br`.`borrow_time` AS `borrow_time`,`br`.`borrow_qty` AS `borrow_qty`,`br`.`borrow_reason` AS `borrow_reason`,`br`.`status` AS `status`,`u`.`real_name` AS `borrower_name`,`u`.`work_no` AS `borrower_work_no`,`u`.`department` AS `department`,`ti`.`tool_code` AS `tool_code`,`ti`.`tool_name` AS `tool_name`,`ti`.`spec` AS `spec`,`ti`.`cabinet_id` AS `cabinet_id`,`tc`.`cabinet_name` AS `cabinet_name`,concat(`tc`.`cabinet_name`,'-',`ti`.`layer`,'层-',`ti`.`position`) AS `position_desc` from (((`tool_borrow_record` `br` join `sys_user` `u` on((`br`.`user_id` = `u`.`user_id`))) join `tool_info` `ti` on((`br`.`tool_id` = `ti`.`tool_id`))) left join `tool_cabinet` `tc` on((`ti`.`cabinet_id` = `tc`.`cabinet_id`))) where (`br`.`status` = 'borrowing') */;
/*!50001 SET character_set_client      = @saved_cs_client */;
/*!50001 SET character_set_results     = @saved_cs_results */;
/*!50001 SET collation_connection      = @saved_col_connection */;

--
-- Final view structure for view `v_dashboard_stats`
--

/*!50001 DROP VIEW IF EXISTS `v_dashboard_stats`*/;
/*!50001 SET @saved_cs_client          = @@character_set_client */;
/*!50001 SET @saved_cs_results         = @@character_set_results */;
/*!50001 SET @saved_col_connection     = @@collation_connection */;
/*!50001 SET character_set_client      = utf8mb4 */;
/*!50001 SET character_set_results     = utf8mb4 */;
/*!50001 SET collation_connection      = utf8mb4_general_ci */;
/*!50001 CREATE ALGORITHM=UNDEFINED */
/*!50013 DEFINER=`root`@`localhost` SQL SECURITY DEFINER */
/*!50001 VIEW `v_dashboard_stats` AS select (select count(0) from `tool_info`) AS `total_tools`,(select coalesce(sum(`tool_info`.`current_qty`),0) from `tool_info` where (`tool_info`.`status` = 'in_stock')) AS `in_stock_qty`,(select count(0) from `tool_borrow_record` where (`tool_borrow_record`.`status` = 'borrowing')) AS `borrowing_count`,(select count(0) from `sys_alert` where (`sys_alert`.`status` = 'unhandled')) AS `unhandled_alerts` */;
/*!50001 SET character_set_client      = @saved_cs_client */;
/*!50001 SET character_set_results     = @saved_cs_results */;
/*!50001 SET collation_connection      = @saved_col_connection */;

--
-- Final view structure for view `v_tool_latest_operation`
--

/*!50001 DROP VIEW IF EXISTS `v_tool_latest_operation`*/;
/*!50001 SET @saved_cs_client          = @@character_set_client */;
/*!50001 SET @saved_cs_results         = @@character_set_results */;
/*!50001 SET @saved_col_connection     = @@collation_connection */;
/*!50001 SET character_set_client      = utf8 */;
/*!50001 SET character_set_results     = utf8 */;
/*!50001 SET collation_connection      = utf8_general_ci */;
/*!50001 CREATE ALGORITHM=UNDEFINED */
/*!50013 DEFINER=`root`@`localhost` SQL SECURITY DEFINER */
/*!50001 VIEW `v_tool_latest_operation` AS select `t`.`tool_id` AS `tool_id`,coalesce(`br`.`latest_borrow_time`,'') AS `latest_op_time`,(case when (`br`.`latest_borrow_time` is not null) then 'borrow' else 'checkin' end) AS `latest_op_type`,coalesce((select `u`.`real_name` from (`smart_cabinet`.`tool_borrow_record` `tbr2` left join `smart_cabinet`.`sys_user` `u` on((`u`.`user_id` = `tbr2`.`user_id`))) where ((`tbr2`.`tool_id` = `t`.`tool_id`) and (`tbr2`.`status` in ('borrowing','overdue'))) order by `tbr2`.`borrow_time` desc limit 1),'') AS `latest_op_user` from (`smart_cabinet`.`tool_info` `t` left join (select `smart_cabinet`.`tool_borrow_record`.`tool_id` AS `tool_id`,max(`smart_cabinet`.`tool_borrow_record`.`borrow_time`) AS `latest_borrow_time` from `smart_cabinet`.`tool_borrow_record` where (`smart_cabinet`.`tool_borrow_record`.`status` in ('borrowing','overdue')) group by `smart_cabinet`.`tool_borrow_record`.`tool_id`) `br` on((`t`.`tool_id` = `br`.`tool_id`))) */;
/*!50001 SET character_set_client      = @saved_cs_client */;
/*!50001 SET character_set_results     = @saved_cs_results */;
/*!50001 SET collation_connection      = @saved_col_connection */;

--
-- Final view structure for view `v_tool_stats`
--

/*!50001 DROP VIEW IF EXISTS `v_tool_stats`*/;
/*!50001 SET @saved_cs_client          = @@character_set_client */;
/*!50001 SET @saved_cs_results         = @@character_set_results */;
/*!50001 SET @saved_col_connection     = @@collation_connection */;
/*!50001 SET character_set_client      = utf8 */;
/*!50001 SET character_set_results     = utf8 */;
/*!50001 SET collation_connection      = utf8_general_ci */;
/*!50001 CREATE ALGORITHM=UNDEFINED */
/*!50013 DEFINER=`root`@`localhost` SQL SECURITY DEFINER */
/*!50001 VIEW `v_tool_stats` AS select (select count(0) from `tool_info`) AS `total_tools`,(select count(0) from `tool_info` where (`tool_info`.`status` = 'in_stock')) AS `in_stock_count`,(select count(distinct `tool_borrow_record`.`tool_id`) from `tool_borrow_record` where (`tool_borrow_record`.`status` in ('borrowing','overdue'))) AS `borrowed_count`,(select count(0) from `tool_info` where (`tool_info`.`status` = 'maintenance')) AS `maintenance_count`,(select coalesce(sum(`tool_info`.`total_qty`),0) from `tool_info`) AS `total_qty`,(select coalesce(sum(`tool_info`.`current_qty`),0) from `tool_info`) AS `in_stock_qty`,(select coalesce(sum(`tool_borrow_record`.`borrow_qty`),0) from `tool_borrow_record` where (`tool_borrow_record`.`status` in ('borrowing','overdue'))) AS `borrowed_qty` */;
/*!50001 SET character_set_client      = @saved_cs_client */;
/*!50001 SET character_set_results     = @saved_cs_results */;
/*!50001 SET collation_connection      = @saved_col_connection */;
/*!40103 SET TIME_ZONE=@OLD_TIME_ZONE */;

/*!40101 SET SQL_MODE=@OLD_SQL_MODE */;
/*!40014 SET FOREIGN_KEY_CHECKS=@OLD_FOREIGN_KEY_CHECKS */;
/*!40014 SET UNIQUE_CHECKS=@OLD_UNIQUE_CHECKS */;
/*!40101 SET CHARACTER_SET_CLIENT=@OLD_CHARACTER_SET_CLIENT */;
/*!40101 SET CHARACTER_SET_RESULTS=@OLD_CHARACTER_SET_RESULTS */;
/*!40101 SET COLLATION_CONNECTION=@OLD_COLLATION_CONNECTION */;
/*!40111 SET SQL_NOTES=@OLD_SQL_NOTES */;

-- Dump completed on 2026-10-05 11:12:03
