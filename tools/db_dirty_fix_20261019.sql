-- ============================================================================
-- db_dirty_fix_20261019.sql — 清理测试脏数据 tool_id=123('test') / 124('testnew')
-- 原则：先备份 → 后删除 → 再验证（删除顺序按外键依赖：绑定→流水→映射→工具）
-- 回滚：备份表均保留，可随时 INSERT 回去
-- ============================================================================

-- ── 1. 备份（按 bak_YYYYMMDD 惯例，四张表各留一份快照备份，便于随时回滚） ──────
CREATE TABLE IF NOT EXISTS bak_20261019_tool_info AS
  SELECT * FROM tool_info WHERE tool_id IN (123, 124);
CREATE TABLE IF NOT EXISTS bak_20261019_mapping AS
  SELECT * FROM tool_position_mapping WHERE tool_id IN (123, 124);
CREATE TABLE IF NOT EXISTS bak_20261019_task_type_tool AS
  SELECT * FROM task_type_tool WHERE tool_id IN (123, 124);
CREATE TABLE IF NOT EXISTS bak_20261019_borrow_record AS
  SELECT * FROM tool_borrow_record WHERE tool_id IN (123, 124);

-- ── 2. 删除（子表在前，避免撞外键） ──────────────────────────────────────────
-- 2.1 任务类型推荐绑定（6885: type 1→123；6946: type 2→124）
DELETE FROM task_type_tool WHERE tool_id IN (123, 124);
-- 2.2 测试期借用流水（116/117/120，均已归还）
DELETE FROM tool_borrow_record WHERE tool_id IN (123, 124);
-- 2.3 位置映射（158: 1柜06层14位；174: 3柜07层01位 —— 释放这两个坑位）
DELETE FROM tool_position_mapping WHERE tool_id IN (123, 124);
-- 2.4 工具主档
DELETE FROM tool_info WHERE tool_id IN (123, 124);

-- ── 3. 验证（应全部为 0） ────────────────────────────────────────────────────
SELECT 'V_tool_info' AS chk, COUNT(*) AS remain FROM tool_info WHERE tool_id IN (123, 124)
UNION ALL SELECT 'V_mapping',        COUNT(*) FROM tool_position_mapping WHERE tool_id IN (123, 124)
UNION ALL SELECT 'V_task_type_tool', COUNT(*) FROM task_type_tool WHERE tool_id IN (123, 124)
UNION ALL SELECT 'V_borrow_record',  COUNT(*) FROM tool_borrow_record WHERE tool_id IN (123, 124)
UNION ALL SELECT 'V_backup_tool',    COUNT(*) FROM bak_20261019_tool_info
UNION ALL SELECT 'V_backup_mapping', COUNT(*) FROM bak_20261019_mapping;

-- 3.1 复跑体检：current_qty 失真应为 0 条、状态漂移应为 0 条
SELECT 'V_qty_mismatch_after' AS chk;
SELECT t.tool_id, t.tool_code, t.current_qty, COUNT(m.mapping_id) AS real_instock
FROM tool_info t
LEFT JOIN tool_position_mapping m ON m.tool_id = t.tool_id AND m.status = 'in_stock'
GROUP BY t.tool_id, t.tool_code, t.current_qty
HAVING t.current_qty <> COUNT(m.mapping_id);

SELECT 'V_status_drift_after' AS chk;
SELECT t.tool_id, t.tool_code, t.status AS ti_status,
       COALESCE(MAX(m.status), 'NONE') AS map_status
FROM tool_info t
LEFT JOIN tool_position_mapping m ON m.tool_id = t.tool_id
GROUP BY t.tool_id, t.tool_code, t.status
HAVING t.status <> COALESCE(MAX(m.status), 'pending');
