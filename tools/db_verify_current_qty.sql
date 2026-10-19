-- current_qty 同步验证（方案B：触发器自动维护）
USE smart_cabinet;

SELECT '=== A) 全库 current_qty 漂移检查 (期望 0 行) ===' AS step;
SELECT ti.tool_id, ti.tool_code, ti.current_qty AS col_val,
       (SELECT COUNT(*) FROM tool_position_mapping m WHERE m.tool_id=ti.tool_id AND m.status='in_stock') AS calc_val
FROM tool_info ti
WHERE ti.current_qty <> (SELECT COUNT(*) FROM tool_position_mapping m WHERE m.tool_id=ti.tool_id AND m.status='in_stock');

SELECT '=== B) 触发器自动生效实测：翻转一个在库位置 ===' AS step;
-- 取一个在库位置做探针（最终会翻回，数据不变）
SELECT @mid:=m.mapping_id, @tid:=m.tool_id, @before:=ti.current_qty
FROM tool_position_mapping m JOIN tool_info ti ON ti.tool_id=m.tool_id
WHERE m.status='in_stock' LIMIT 1;
SELECT @tid AS test_tool_id, @before AS current_qty_before;

-- 翻成 borrowed：应触发 AFTER UPDATE 触发器，current_qty 自动 -1
UPDATE tool_position_mapping SET status='borrowed' WHERE mapping_id=@mid;
SELECT @tid AS test_tool_id,
       (SELECT current_qty FROM tool_info WHERE tool_id=@tid) AS current_qty_after_borrow,
       (SELECT COUNT(*) FROM tool_position_mapping WHERE tool_id=@tid AND status='in_stock') AS calc_after_borrow;

-- 翻回 in_stock：应触发触发器，current_qty 自动恢复
UPDATE tool_position_mapping SET status='in_stock' WHERE mapping_id=@mid;
SELECT @tid AS test_tool_id,
       (SELECT current_qty FROM tool_info WHERE tool_id=@tid) AS current_qty_after_return,
       (SELECT COUNT(*) FROM tool_position_mapping WHERE tool_id=@tid AND status='in_stock') AS calc_after_return;

SELECT '若 after_borrow = before-1 且 after_return = before，则触发器自动同步生效 ✅' AS conclusion;
