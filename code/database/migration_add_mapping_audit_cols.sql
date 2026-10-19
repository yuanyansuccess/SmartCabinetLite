-- ============================================================================
-- migration_add_mapping_audit_cols.sql
-- 用途：为 tool_position_mapping 增加审计字段，可追溯"谁、何时"灌库/改位置
--       对应袁总优化建议中的 🟢低优先级项（mapping 缺审计字段）
-- 影响程度：低 —— 仅【新增列】，不删不改任何现有列，不影响任何现有功能
-- 执行时机：升级上位机软件、灌库之前（或随时）
-- 回滚：ALTER TABLE tool_position_mapping DROP COLUMN updated_at, DROP COLUMN created_by;
-- 注意：mapping 表已有 created_at，本脚本补 updated_at 与 created_by
-- ============================================================================

ALTER TABLE tool_position_mapping
  ADD COLUMN updated_at DATETIME NULL
    DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
    COMMENT '位置状态最后变更时间（借用/出库/灌库均会刷新）',
  ADD COLUMN created_by VARCHAR(64) NULL
    COMMENT '创建/灌库人（升级灌库脚本执行时记录操作人）';
