-- ============================================================================
-- db_dirty_check2.sql — 定点核查：test 记录全貌 + 状态漂移 + 冗余列漂移
-- ============================================================================

-- I. test 记录（tool_id=123）全貌：tool_info 行 + 其全部映射行
SELECT 'I_test_tool_123' AS chk;
SELECT t.tool_id, t.tool_code, t.tool_name, t.spec, t.status, t.total_qty,
       t.current_qty, t.category_id, t.machine_group_id, t.created_at,
       m.mapping_id, m.cabinet_id, m.layer, m.position, m.status AS map_status
FROM tool_info t
LEFT JOIN tool_position_mapping m ON m.tool_id = t.tool_id
WHERE t.tool_id = 123;

-- J. 状态漂移：tool_info.status 与其映射推导状态不一致的（无映射→应为pending）
SELECT 'J_status_drift' AS chk;
SELECT t.tool_id, t.tool_code, t.tool_name, t.status AS ti_status,
       COALESCE(MAX(m.status), 'NONE') AS map_status,
       COUNT(m.mapping_id) AS map_rows
FROM tool_info t
LEFT JOIN tool_position_mapping m ON m.tool_id = t.tool_id
GROUP BY t.tool_id, t.tool_code, t.tool_name, t.status
HAVING t.status <> COALESCE(MAX(m.status), 'pending');

-- K. 冗余列漂移：tool_info.cabinet_id/layer/position 与 in_stock 映射不一致的
SELECT 'K_redundant_col_drift' AS chk;
SELECT t.tool_id, t.tool_code, t.tool_name,
       t.cabinet_id AS ti_cab, t.layer AS ti_layer, t.position AS ti_pos,
       m.cabinet_id AS m_cab, m.layer AS m_layer, m.position AS m_pos
FROM tool_info t
JOIN tool_position_mapping m ON m.tool_id = t.tool_id AND m.status = 'in_stock'
WHERE NOT (t.cabinet_id <=> m.cabinet_id
       AND t.layer <=> m.layer
       AND t.position <=> m.position);
