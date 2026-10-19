-- ============================================================================
-- query_never_stocked_tools.sql
-- 用途：查出「系统中存在对应关系（任务类型-工具关联），但从未入库」的工具
--       即：在 task_type_tool 有关联，却完全没有位置映射（mapping_id IS NULL）
-- 便于袁总核查哪些工具"有任务绑定却没位置"，需补灌位置
-- ============================================================================

-- ① 核心查询：有任务类型关联、但从未入库（无任何位置映射）的工具
SELECT
  t.tool_id,
  t.tool_code,
  t.tool_name,
  t.spec,
  t.status                                   AS tool_status,
  t.machine_group_id                         AS mg_id,
  tt.type_id                                 AS linked_type_id,
  (SELECT type_name FROM task_type tt2 WHERE tt2.type_id = tt.type_id) AS type_name,
  '有任务关联但从未入库(无位置映射)'          AS note
FROM tool_info t
JOIN task_type_tool tt ON tt.tool_id = t.tool_id
LEFT JOIN tool_position_mapping m ON m.tool_id = t.tool_id
WHERE m.mapping_id IS NULL
GROUP BY t.tool_id, t.tool_code, t.tool_name, t.spec, t.status, t.machine_group_id, tt.type_id
ORDER BY t.tool_id;

-- ② 全量清单：所有"从未入库"（无任何映射）的工具，不限是否有关联
SELECT
  t.tool_id,
  t.tool_code,
  t.tool_name,
  t.status,
  t.machine_group_id AS mg_id,
  COUNT(m.mapping_id) AS mapping_cnt
FROM tool_info t
LEFT JOIN tool_position_mapping m ON m.tool_id = t.tool_id
GROUP BY t.tool_id, t.tool_code, t.tool_name, t.status, t.machine_group_id
HAVING mapping_cnt = 0
ORDER BY t.tool_id;

-- ③ 参考：这些缺位工具所属机组的现有位置分布（帮您定位置时参考，避免机组串柜）
SELECT mg.group_id, mg.group_name,
       mpm.cabinet_id, COUNT(*) AS used_positions
FROM tool_info t
JOIN tool_position_mapping mpm ON mpm.tool_id = t.tool_id AND mpm.status = 'in_stock'
JOIN machine_group mg ON mg.group_id = t.machine_group_id
GROUP BY mg.group_id, mg.group_name, mpm.cabinet_id
ORDER BY mg.group_id, mpm.cabinet_id;
