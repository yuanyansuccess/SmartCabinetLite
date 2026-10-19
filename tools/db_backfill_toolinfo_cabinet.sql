-- ============================================================
-- 主档冗余列回填：tool_info.cabinet_id/layer/position ← in_stock 映射
-- 范围：仅 tool_id IN (6,12)（核查确认全库仅这2条漂移）
-- 幂等：重复执行无副作用
-- ============================================================

-- 1) 回填
UPDATE tool_info t
JOIN tool_position_mapping m ON m.tool_id = t.tool_id AND m.status = 'in_stock'
SET t.cabinet_id = m.cabinet_id,
    t.layer      = m.layer,
    t.position   = m.position
WHERE t.tool_id IN (6, 12);

-- 2) 验证：主档与映射应完全一致、cabinet_id 非空
SELECT t.tool_id, t.tool_code, t.status,
       t.cabinet_id, cb.cabinet_name, t.layer, t.position,
       m.cabinet_id AS map_cab, m.layer AS map_layer, m.position AS map_pos
FROM tool_info t
JOIN tool_position_mapping m ON m.tool_id = t.tool_id AND m.status = 'in_stock'
LEFT JOIN tool_cabinet cb ON cb.cabinet_id = t.cabinet_id
WHERE t.tool_id IN (6, 12);

-- 3) 复查：全库不应再有主档/映射漂移（期望 0 行）
SELECT t.tool_id, t.tool_code
FROM tool_info t
JOIN tool_position_mapping m ON m.tool_id = t.tool_id AND m.status = 'in_stock'
WHERE t.cabinet_id IS NULL OR t.cabinet_id <> m.cabinet_id
   OR NOT (t.layer <=> m.layer) OR NOT (t.position <=> m.position);
