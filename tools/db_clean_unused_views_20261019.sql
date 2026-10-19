-- ============================================================================
-- db_clean_unused_views_20261019.sql — 清理无代码引用的数据库视图
-- 原则：视图只为代码查询服务，代码零引用即清理；删除前保留原定义便于回滚
-- 核查依据：src / tests / tools 全量检索四个视图名的引用情况
--   v_tool_latest_operation：ToolDAOToolEntity.cpp 两处 LEFT JOIN 使用 → 保留
--   v_tool_stats：仅出现在两处注释里，无任何 SQL 调用 → 清理
--   v_borrowing_tools / v_dashboard_stats：库中已不存在，脚本内 DROP IF EXISTS 幂等兜底
-- ============================================================================

-- ── 1. 删除无代码引用的视图 ─────────────────────────────────────────────────
DROP VIEW IF EXISTS v_tool_stats;
DROP VIEW IF EXISTS v_borrowing_tools;
DROP VIEW IF EXISTS v_dashboard_stats;

-- ── 2. 校验：剩余视图清单（期望只剩 v_tool_latest_operation）────────────────
SELECT TABLE_NAME AS remaining_view
FROM information_schema.VIEWS
WHERE TABLE_SCHEMA = DATABASE();

-- ── 3. 回滚用原定义（需要恢复时取消注释执行）────────────────────────────────
-- CREATE VIEW v_tool_stats AS
-- SELECT (SELECT COUNT(*) FROM tool_info) AS total_tools,
--        (SELECT COUNT(*) FROM tool_info WHERE status = 'in_stock') AS in_stock_count,
--        (SELECT COUNT(DISTINCT tool_id) FROM tool_borrow_record
--          WHERE status IN ('borrowing','overdue')) AS borrowed_count,
--        (SELECT COUNT(*) FROM tool_info WHERE status = 'maintenance') AS maintenance_count,
--        (SELECT COALESCE(SUM(total_qty),0) FROM tool_info) AS total_qty,
--        (SELECT COALESCE(SUM(current_qty),0) FROM tool_info) AS in_stock_qty,
--        (SELECT COALESCE(SUM(borrow_qty),0) FROM tool_borrow_record
--          WHERE status IN ('borrowing','overdue')) AS borrowed_qty;
