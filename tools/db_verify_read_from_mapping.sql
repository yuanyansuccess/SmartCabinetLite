-- 验证：mpm 子查询能【独立】按 tool_id 取到映射表位置（与 ti 冗余列无关）
-- 这段直接模拟代码里 mpm.mapping_id=(SELECT MIN(...) WHERE ... AND status='in_stock')
SELECT ti.tool_id, ti.tool_code,
       (SELECT MIN(m.mapping_id) FROM tool_position_mapping m
        WHERE m.tool_id=ti.tool_id AND m.status='in_stock') AS mpm_mapping_id,
       (SELECT m.cabinet_id FROM tool_position_mapping m
        WHERE m.mapping_id=(SELECT MIN(m2.mapping_id) FROM tool_position_mapping m2
                            WHERE m2.tool_id=ti.tool_id AND m2.status='in_stock')) AS mpm_cabinet_id,
       (SELECT cb.cabinet_name FROM tool_position_mapping m
        JOIN tool_cabinet cb ON cb.cabinet_id=m.cabinet_id
        WHERE m.mapping_id=(SELECT MIN(m2.mapping_id) FROM tool_position_mapping m2
                            WHERE m2.tool_id=ti.tool_id AND m2.status='in_stock')) AS mpm_cabinet_name,
       (SELECT m.layer FROM tool_position_mapping m
        WHERE m.mapping_id=(SELECT MIN(m2.mapping_id) FROM tool_position_mapping m2
                            WHERE m2.tool_id=ti.tool_id AND m2.status='in_stock')) AS mpm_layer,
       (SELECT m.position FROM tool_position_mapping m
        WHERE m.mapping_id=(SELECT MIN(m2.mapping_id) FROM tool_position_mapping m2
                            WHERE m2.tool_id=ti.tool_id AND m2.status='in_stock')) AS mpm_position
FROM tool_info ti
WHERE ti.tool_code IN ('JZ01-SB','JZ01-CRV','JZ01-CDQ','JZ03-QG');
