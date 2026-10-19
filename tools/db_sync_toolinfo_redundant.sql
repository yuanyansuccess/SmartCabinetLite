-- ============================================================
-- 主档冗余列同步核查（只查不改）
-- 目的：找出 tool_info 主档 cabinet_id / cabinet_name / layer / position
--       与 in_stock 位置映射不一致的所有工具
-- ============================================================

-- 1) 重点看 JZ01-SB / JZ01-CRV 主档现状
SELECT t.tool_id, t.tool_code, t.status,
       t.cabinet_id AS ti_cab, t.cabinet_name AS ti_cabname,
       t.layer AS ti_layer, t.position AS ti_pos,
       m.cabinet_id AS map_cab, m.layer AS map_layer, m.position AS map_pos,
       cb.cabinet_name AS map_cabname
FROM tool_info t
JOIN tool_position_mapping m ON m.tool_id = t.tool_id AND m.status = 'in_stock'
LEFT JOIN tool_cabinet cb ON cb.cabinet_id = m.cabinet_id
WHERE t.tool_code IN ('JZ01-SB','JZ01-CRV');

-- 2) 全量漂移清单：主档与 in_stock 映射不一致的所有工具（含柜名为空/不一致）
SELECT t.tool_id, t.tool_code, t.tool_name,
       t.cabinet_id AS ti_cab, t.cabinet_name AS ti_cabname,
       t.layer AS ti_layer, t.position AS ti_pos,
       m.cabinet_id AS map_cab, cb.cabinet_name AS map_cabname,
       m.layer AS map_layer, m.position AS map_pos
FROM tool_info t
JOIN tool_position_mapping m ON m.tool_id = t.tool_id AND m.status = 'in_stock'
LEFT JOIN tool_cabinet cb ON cb.cabinet_id = m.cabinet_id
WHERE t.cabinet_id IS NULL
   OR t.cabinet_id <> m.cabinet_id
   OR NOT (t.layer <=> m.layer)
   OR NOT (t.position <=> m.position)
   OR (t.cabinet_name IS NULL OR t.cabinet_name = '')
   OR t.cabinet_name <> cb.cabinet_name
ORDER BY t.tool_id;
