-- ============================================================================
-- db_drop_redundant_20261019.sql — 冗余列清理（工具名称/操作人用户名）
-- 原则：名称只在主表存一份，流水表只存ID，展示时 LEFT JOIN 主表取名
-- 步骤：按工具编号回填 tool_id → 校验全部可关联 → 删冗余列 → 重建失效视图
-- ============================================================================

-- ── 1. 回填：历史告警只存了 tool_code，按编号补回 tool_id ──────────────────
UPDATE sys_alert a
JOIN tool_info t ON a.tool_code = t.tool_code
SET a.tool_id = t.tool_id
WHERE (a.tool_id IS NULL OR a.tool_id = 0)
  AND a.tool_code IS NOT NULL AND a.tool_code <> '';

-- ── 2. 校验：有 tool_id 的告警必须全部能关联到 tool_info（期望 0）─────────
SELECT 'V_alert_toolid_miss' AS chk, COUNT(*) AS cnt
FROM sys_alert a
LEFT JOIN tool_info t ON a.tool_id = t.tool_id
WHERE a.tool_id IS NOT NULL AND a.tool_id > 0 AND t.tool_id IS NULL;

-- ── 3. 删冗余列 ────────────────────────────────────────────────────────────
-- sys_alert.tool_code：改由 tool_id 关联 tool_info 取编号
ALTER TABLE sys_alert DROP COLUMN tool_code;
-- sys_operation_log.username：改由 user_id 关联 sys_user 取账号
ALTER TABLE sys_operation_log DROP COLUMN username;
-- tool_checkin_record.tool_name/tool_code：改由 tool_id 关联 tool_info 取名称与编号
ALTER TABLE tool_checkin_record DROP COLUMN tool_name;
ALTER TABLE tool_checkin_record DROP COLUMN tool_code;

-- ── 4. 清理视图 v_borrowing_tools：借用明细由 RecordDAO 直查多表，视图无调用方 ──
DROP VIEW IF EXISTS v_borrowing_tools;
