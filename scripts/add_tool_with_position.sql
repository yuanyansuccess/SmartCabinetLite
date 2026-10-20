-- ============================================================
-- @file add_tool_with_position.sql
-- @brief 升级/部署时向数据库添加新工具并配置货位关系（内置全部前置校验）
-- @author 袁燕
--
-- 使用场景：系统升级需要补充新工具时，直接连 MySQL 执行本脚本，
--           不用在软件界面里逐条手工录入。
--
-- 执行前必须先确认三个ID（单独执行下面三条查询）：
--   SELECT group_id, group_name FROM machine_group;        -- 本机机组ID（填错则"一键导入入库"列表查不到该工具）
--   SELECT cabinet_id, cabinet_name FROM tool_cabinet;     -- 柜体ID
--   SELECT category_id, category_name FROM tool_category;  -- 工具分类ID（填0表示不分类）
--   货位是否被占用由脚本自动判断，无需手工查。
--
-- 执行方式（PowerShell，必须按此方式执行，保证中文不乱码、不报排序规则错误）：
--   第一步：设置管道输出编码
--     $OutputEncoding = [System.Text.Encoding]::UTF8
--   第二步：创建存储过程（在 SmartCabinetLite 目录下）
--     Get-Content -Raw -Encoding UTF8 "scripts/add_tool_with_position.sql" | & "C:\Program Files\MySQL\MySQL Server 5.7\bin\mysql.exe" -uroot -proot smart_cabinet --default-character-set=utf8mb4
--   第三步：添加工具（管道传中文参数）
--     "CALL sp_add_tool_with_position('JZ01-MIC50','千分尺50mm','25-50mm',3,1,1,'01','07');" | & "C:\Program Files\MySQL\MySQL Server 5.7\bin\mysql.exe" -uroot -proot smart_cabinet --default-character-set=utf8mb4
--
-- 为什么必须这样执行：Windows PowerShell 默认按 GBK 传递参数，与数据库 utf8mb4
-- 混合会报 ERROR 1270（排序规则冲突）且中文可能乱码；上述命令让脚本文件与参数
-- 都以 UTF-8 字节进入 mysql 客户端，并指定客户端字符集 utf8mb4，全链路一致。
--
-- 注意：不要用 -e "source 脚本" 方式（-e 参数按 GBK 传中文会触发上述问题）；
--       不要用 -p root 中间带空格的写法（必须 -proot 连写）。
--
-- 幂等说明：工具编号已存在时自动跳过并提示，不会重复插入，可反复执行；
--           货位被其他工具占用、机组/柜体/分类不存在、层号位号格式错误时报错中止，不会写坏数据。
--
-- 添加多个工具：在文件末尾"使用示例"区写多条 CALL，每条一个工具。
-- ============================================================

USE smart_cabinet;

DROP PROCEDURE IF EXISTS sp_add_tool_with_position;
DELIMITER $$

CREATE PROCEDURE sp_add_tool_with_position(
    IN p_tool_code   VARCHAR(32),
    IN p_tool_name   VARCHAR(128),
    IN p_spec        VARCHAR(64),
    IN p_category_id INT,
    IN p_group_id    INT,
    IN p_cabinet_id  INT,
    IN p_layer       VARCHAR(16),
    IN p_position    VARCHAR(16)
)
BEGIN
    DECLARE v_tool_id  INT DEFAULT 0;
    DECLARE v_occ_code VARCHAR(32)  DEFAULT NULL;
    DECLARE v_occ_name VARCHAR(128) DEFAULT NULL;
    DECLARE v_cnt      INT DEFAULT 0;

    /* 校验1：编号与名称不能为空 */
    IF p_tool_code = '' OR p_tool_name = '' THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = '参数错误：工具编号和工具名称不能为空';
    END IF;

    /* 校验2：层号位号格式（两位数字，与界面 A-01-02 位置格式一致） */
    IF p_layer NOT REGEXP '^[0-9]{2}$' THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = '层号格式错误：必须是两位数字，例如 01、07';
    END IF;
    IF p_position NOT REGEXP '^[0-9]{2}$' THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = '位号格式错误：必须是两位数字，例如 01、12';
    END IF;

    /* 校验3：机组必须存在（待入库查询按 machine_group_id 过滤，填错列表查不到） */
    SELECT COUNT(*) INTO v_cnt FROM machine_group WHERE group_id = p_group_id;
    IF v_cnt = 0 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = '机组不存在：请执行 SELECT group_id,group_name FROM machine_group 确认本机机组ID';
    END IF;

    /* 校验4：柜体必须存在 */
    SELECT COUNT(*) INTO v_cnt FROM tool_cabinet WHERE cabinet_id = p_cabinet_id;
    IF v_cnt = 0 THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = '柜体不存在：请执行 SELECT cabinet_id,cabinet_name FROM tool_cabinet 确认柜体ID';
    END IF;

    /* 校验5：分类必须存在（填0表示不分类） */
    IF p_category_id > 0 THEN
        SELECT COUNT(*) INTO v_cnt FROM tool_category WHERE category_id = p_category_id;
        IF v_cnt = 0 THEN
            SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = '工具分类不存在：请执行 SELECT category_id,category_name FROM tool_category 确认分类ID（填0表示不分类）';
        END IF;
    END IF;

    /* 校验6：工具编号唯一，已存在则跳过（保证脚本可反复执行不重复插） */
    SELECT COUNT(*) INTO v_cnt FROM tool_info WHERE tool_code = p_tool_code;
    IF v_cnt > 0 THEN
        SELECT p_tool_code AS 工具编号, '已存在，本次跳过不重复添加' AS 结果;
    ELSE
        /* 校验7：货位未被占用（tool_position_mapping 唯一键 = 柜体+层+位） */
        SELECT t.tool_code, t.tool_name INTO v_occ_code, v_occ_name
          FROM tool_position_mapping m
          JOIN tool_info t ON m.tool_id = t.tool_id
         WHERE m.cabinet_id = p_cabinet_id AND m.layer = p_layer AND m.position = p_position
         LIMIT 1;
        IF v_occ_code IS NOT NULL THEN
            SELECT p_cabinet_id AS 柜号, p_layer AS 层号, p_position AS 位号,
                   v_occ_code AS 占用工具编号, v_occ_name AS 占用工具名称;
            SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = '货位已被其他工具占用，请更换层号或位号（占用详情见上方结果行）';
        END IF;

        /* 全部通过：事务内写入工具档案与货位关系（字段与 DAO insertTool/insertPositionMapping 一致） */
        START TRANSACTION;
        INSERT INTO tool_info (tool_code, tool_name, spec, category_id, machine_group_id,
                               total_qty, current_qty, unit, status)
        VALUES (p_tool_code, p_tool_name, p_spec, NULLIF(p_category_id, 0),
                p_group_id, 1, 0, '件', 'pending');
        SET v_tool_id = LAST_INSERT_ID();
        INSERT INTO tool_position_mapping (tool_id, cabinet_id, layer, position, status)
        VALUES (v_tool_id, p_cabinet_id, p_layer, p_position, 'pending');
        COMMIT;

        SELECT p_tool_code AS 工具编号, p_tool_name AS 工具名称, p_spec AS 规格,
               p_cabinet_id AS 柜号, p_layer AS 层号, p_position AS 位号,
               '待入库' AS 状态, '添加成功，可在界面一键导入入库中入库' AS 结果;
    END IF;
END$$

DELIMITER ;

-- ============================================================
-- 使用示例：升级时把参数改成实际值（可写多条，改完取消注释执行）
-- 参数顺序：工具编号, 工具名称, 规格, 分类ID, 机组ID, 柜体ID, 层号, 位号
-- 机组ID=1 发动机维护机组；分类 3=测量工具；柜体 1=A柜
-- ============================================================
-- CALL sp_add_tool_with_position('JZ01-MIC50', '千分尺50mm', '25-50mm', 3, 1, 1, '01', '05');
