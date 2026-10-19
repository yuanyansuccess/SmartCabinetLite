-- ============================================================================
-- db_dirty_check3.sql — 删除前置核查：test 系列全名单 + 全库外键引用扫描
-- ============================================================================

-- L. test 系列工具全名单（可能不止 123/124）
SELECT 'L_test_like_tools' AS chk;
SELECT tool_id, tool_code, tool_name, status, total_qty, current_qty,
       machine_group_id, created_at
FROM tool_info
WHERE tool_code LIKE 'test%' OR tool_name LIKE 'test%' OR tool_code = '' OR tool_name = '';

-- M. 全库哪些表有 tool_id 列（判断删除影响面）
SELECT 'M_tables_with_tool_id' AS chk;
SELECT table_name, column_name
FROM information_schema.columns
WHERE table_schema = 'smart_cabinet' AND column_name = 'tool_id'
ORDER BY table_name;

-- N. mapping 中指向 test 系列的行（删工具前先删映射）
SELECT 'N_mapping_of_test_tools' AS chk;
SELECT m.mapping_id, m.tool_id, t.tool_code, m.cabinet_id, m.layer, m.position, m.status
FROM tool_position_mapping m
JOIN tool_info t ON t.tool_id = m.tool_id
WHERE t.tool_code LIKE 'test%' OR t.tool_name LIKE 'test%';
