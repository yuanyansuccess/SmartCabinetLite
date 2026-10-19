-- ============================================================================
-- db_dirty_check.sql — 脏数据全面体检（只查不改）
-- 背景：袁总发现 JZ01-SB / JZ01-CRV 两行 柜名/层/位 全 NULL，怀疑脏数据
-- ============================================================================

-- A. 没有 in_stock 位置映射的工具（图里 NULL 的形态）
SELECT 'A_tools_without_instock_mapping' AS chk;
SELECT t.tool_id, t.tool_code, t.tool_name, t.status AS tool_status,
       t.cabinet_id AS ti_cab, t.layer AS ti_layer, t.position AS ti_pos,
       t.machine_group_id AS mg_id
FROM tool_info t
LEFT JOIN tool_position_mapping m ON m.tool_id = t.tool_id AND m.status = 'in_stock'
WHERE m.mapping_id IS NULL;

-- B. 嫌疑两条工具在映射表里的全部行（区分"没灌"vs"灌坏了"）
SELECT 'B_mapping_rows_of_suspect_tools' AS chk;
SELECT m.mapping_id, m.tool_id, t.tool_code, m.cabinet_id, m.layer, m.position,
       m.status, m.created_at
FROM tool_position_mapping m
JOIN tool_info t ON t.tool_id = m.tool_id
WHERE t.tool_code IN ('JZ01-SB', 'JZ01-CRV');

-- C. 孤立映射：mapping 指向不存在的 tool
SELECT 'C_orphan_mapping_no_tool' AS chk;
SELECT m.mapping_id, m.tool_id, m.status
FROM tool_position_mapping m
LEFT JOIN tool_info t ON t.tool_id = m.tool_id
WHERE t.tool_id IS NULL;

-- D. 映射行位置字段为 NULL（违反"位置必填"铁律）
SELECT 'D_mapping_null_position_fields' AS chk;
SELECT mapping_id, tool_id, cabinet_id, layer, position, status
FROM tool_position_mapping
WHERE cabinet_id IS NULL OR layer IS NULL OR position IS NULL;

-- E. 位置冲突：同柜同层同位多行（应被唯一约束挡住，若出现说明约束缺失）
SELECT 'E_duplicate_positions' AS chk;
SELECT cabinet_id, layer, position, COUNT(*) AS cnt
FROM tool_position_mapping
GROUP BY cabinet_id, layer, position HAVING cnt > 1;

-- F. current_qty 失真：与 in_stock 映射实际条数不符
SELECT 'F_current_qty_mismatch' AS chk;
SELECT t.tool_id, t.tool_code, t.tool_name, t.current_qty,
       COUNT(m.mapping_id) AS real_instock
FROM tool_info t
LEFT JOIN tool_position_mapping m ON m.tool_id = t.tool_id AND m.status = 'in_stock'
GROUP BY t.tool_id, t.tool_code, t.tool_name, t.current_qty
HAVING t.current_qty <> COUNT(m.mapping_id);

-- G. 机组为空的工具（机组隔离铁律）
SELECT 'G_tools_without_machine_group' AS chk;
SELECT tool_id, tool_code, tool_name FROM tool_info WHERE machine_group_id IS NULL;

-- H. 状态分布（看有没有非法状态值）
SELECT 'H_status_distribution' AS chk;
SELECT 'tool_info' AS tbl, status, COUNT(*) AS cnt FROM tool_info GROUP BY status
UNION ALL
SELECT 'mapping', status, COUNT(*) FROM tool_position_mapping GROUP BY status;
