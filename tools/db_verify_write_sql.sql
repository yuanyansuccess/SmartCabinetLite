-- 验证写入侧去位置列后的 SQL 合法性（事务内执行，最终 ROLLBACK，零脏数据）
START TRANSACTION;

-- 模拟 insertTool 新列集合（无 cabinet_id/layer/position）
INSERT INTO tool_info (tool_code,tool_name,spec,category_id,machine_group_id,
                      total_qty,current_qty,vision_tag,status,is_recommended,recognition_method,document_path)
VALUES ('TESTSQL_CHK','__verify__','',1,1,1,1,'','in_stock',0,'visual','');

-- 模拟 updateTool 新列集合（无 cabinet_id/layer/position）
UPDATE tool_info
SET tool_name=tool_name, spec=spec, category_id=category_id, machine_group_id=machine_group_id,
    total_qty=total_qty, vision_tag=vision_tag, status=status, is_recommended=is_recommended,
    recognition_method=recognition_method, document_path=document_path
WHERE tool_id = (SELECT LAST_INSERT_ID());

-- 验证 ToolDAO::update(QJsonObject) 的 dbCols 也合法：只更新非位置列
UPDATE tool_info SET total_qty=total_qty WHERE tool_id=1;

SELECT 'WRITE_SQL_OK' AS result;

ROLLBACK;  -- 撤销上述所有写操作，确保不污染生产数据
