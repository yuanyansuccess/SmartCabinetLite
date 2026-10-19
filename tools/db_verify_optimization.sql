-- ============================================================================
-- 数据库优化验证脚本（B 改造后）
-- 目标：确认"位置一律从 tool_position_mapping 取，tool_info 冗余列只读不写"已落地
--   1) 全库主档冗余列 vs 映射表 一致性（期望 0 行漂移）
--   2) 孤儿映射（映射的 tool_id 不存在于 tool_info，期望 0 行）
--   3) 模拟改造后的 TaskTypeDAO / AlertDAO 查询：按 mapping 取位置，
--      确认 JZ01-SB / JZ01-CRV 取到正确柜名（不再 [NULL]）
-- ============================================================================
USE smart_cabinet;

SELECT '=== 1) 主档冗余列 vs 映射表 漂移检查 (期望 0 行) ===' AS step;
SELECT ti.tool_id, ti.tool_code,
       ti.cabinet_id AS ti_cab, ti.layer AS ti_lay, ti.position AS ti_pos,
       m.cabinet_id AS m_cab,  m.layer  AS m_lay,  m.position  AS m_pos
FROM tool_info ti
JOIN tool_position_mapping m
  ON m.tool_id = ti.tool_id AND m.status = 'in_stock'
WHERE NOT ( ti.cabinet_id <=> m.cabinet_id
       AND ti.layer      <=> m.layer
       AND ti.position   <=> m.position );

SELECT '=== 2) 孤儿映射 (tool_id 不在 tool_info, 期望 0 行) ===' AS step;
SELECT m.mapping_id, m.tool_id, m.cabinet_id, m.layer, m.position, m.status
FROM tool_position_mapping m
LEFT JOIN tool_info ti ON ti.tool_id = m.tool_id
WHERE ti.tool_id IS NULL;

SELECT '=== 3) 模拟改造后查询：位置从 mapping 取 ===' AS step;
SELECT ti.tool_code,
       (SELECT cb.cabinet_name
          FROM tool_position_mapping m
          JOIN tool_cabinet cb ON m.cabinet_id = cb.cabinet_id
         WHERE m.tool_id = ti.tool_id AND m.status = 'in_stock'
         LIMIT 1) AS cab_name_from_mapping,
       (SELECT m.layer   FROM tool_position_mapping m WHERE m.tool_id = ti.tool_id AND m.status = 'in_stock' LIMIT 1) AS layer_from_mapping,
       (SELECT m.position FROM tool_position_mapping m WHERE m.tool_id = ti.tool_id AND m.status = 'in_stock' LIMIT 1) AS pos_from_mapping,
       ti.cabinet_id AS ti_cab_id, ti.layer AS ti_lay, ti.position AS ti_pos
FROM tool_info ti
WHERE ti.tool_code IN ('JZ01-SB', 'JZ01-CRV');

SELECT '=== 4) 全库 in_stock 映射总览 ===' AS step;
SELECT cb.cabinet_name,
       COUNT(*) AS in_stock_cnt
FROM tool_position_mapping m
JOIN tool_cabinet cb ON m.cabinet_id = cb.cabinet_id
WHERE m.status = 'in_stock'
GROUP BY cb.cabinet_name
ORDER BY cb.cabinet_name;
