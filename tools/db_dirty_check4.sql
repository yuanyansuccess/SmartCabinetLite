-- ============================================================================
-- db_dirty_check4.sql — 对 123/124 的全库引用计数（决定物理删 or 逻辑删）
-- ============================================================================

SELECT 'O_reference_counts_for_123_124' AS chk;
SELECT 'sys_alert' AS tbl, COUNT(*) AS ref_cnt FROM sys_alert WHERE tool_id IN (123,124)
UNION ALL SELECT 'task_type_tool', COUNT(*) FROM task_type_tool WHERE tool_id IN (123,124)
UNION ALL SELECT 'tool_borrow_record', COUNT(*) FROM tool_borrow_record WHERE tool_id IN (123,124)
UNION ALL SELECT 'tool_checkin_record', COUNT(*) FROM tool_checkin_record WHERE tool_id IN (123,124)
UNION ALL SELECT 'tool_checkout_record', COUNT(*) FROM tool_checkout_record WHERE tool_id IN (123,124)
UNION ALL SELECT '_borrow_overflow', COUNT(*) FROM _borrow_overflow WHERE tool_id IN (123,124);

-- P. 明细：若 borrow/checkout 有引用，列出来看性质
SELECT 'P_borrow_records_detail' AS chk;
SELECT * FROM tool_borrow_record WHERE tool_id IN (123,124) LIMIT 20;

SELECT 'P_checkout_records_detail' AS chk;
SELECT * FROM tool_checkout_record WHERE tool_id IN (123,124) LIMIT 20;
