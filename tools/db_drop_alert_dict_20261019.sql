-- ============================================================================
-- db_drop_alert_dict_20261019.sql — 告警字典冗余列清理
-- 原则：类型编码与级别只在字典表 sys_alert_type 存一份，
--       sys_alert 只存 type_id，展示时 JOIN 字典表取名
-- 前置：全库备份 db_backup/smart_cabinet_20261019_before_drop_alert_dict.sql
-- ============================================================================

USE smart_cabinet;
ALTER TABLE sys_alert DROP COLUMN alert_type;
ALTER TABLE sys_alert DROP COLUMN alert_level;

USE smart_cabinet_test;
ALTER TABLE sys_alert DROP COLUMN alert_type;
ALTER TABLE sys_alert DROP COLUMN alert_level;
