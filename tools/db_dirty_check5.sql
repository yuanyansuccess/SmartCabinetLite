-- ============================================================================
-- db_dirty_check5.sql — 最后细节：task_type 绑定明细 + 外键约束确认
-- ============================================================================

SELECT 'Q_task_type_binding_detail' AS chk;
SELECT tt.task_type_id, tt.tool_id, t.tool_code
FROM task_type_tool tt
JOIN tool_info t ON t.tool_id = tt.tool_id
WHERE tt.tool_id IN (123, 124);

SELECT 'R_fk_constraints' AS chk;
SELECT table_name, constraint_name, referenced_table_name
FROM information_schema.referential_constraints
WHERE constraint_schema = 'smart_cabinet'
  AND (referenced_table_name IN ('tool_info', 'tool_borrow_record', 'tool_position_mapping')
       OR table_name IN ('tool_borrow_record', 'tool_position_mapping', 'task_type_tool'));

SELECT 'S_checkin_refs_to_borrow' AS chk;
SELECT table_name, column_name
FROM information_schema.columns
WHERE table_schema = 'smart_cabinet'
  AND (column_name LIKE '%borrow%record%' OR column_name = 'borrow_record_id');
