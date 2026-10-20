add_tool_with_position.sql 使用说明
====================================

一、用途
    系统升级/部署时向数据库添加新工具并配置货位关系，内置全部前置校验，
    不用在软件界面里逐条手工录入。添加后工具状态为"待入库"，
    会出现在"工具出入库管理 - 一键导入入库行"列表中。

二、执行前先确认三个ID（单独执行查询）
    SELECT group_id, group_name FROM machine_group;        本机机组ID
    SELECT cabinet_id, cabinet_name FROM tool_cabinet;     柜体ID
    SELECT category_id, category_name FROM tool_category;  工具分类ID
    本机机组ID必须填对：待入库查询按 machine_group_id 过滤，填错则界面上查不到该工具。
    货位是否被占用由脚本自动判断，无需手工查。

三、执行方式（PowerShell，必须按此方式，保证中文不乱码）
    第一步（每个新窗口执行一次）：
        $OutputEncoding = [System.Text.Encoding]::UTF8
    第二步：创建存储过程（在 SmartCabinetLite 目录下）
        Get-Content -Raw -Encoding UTF8 "scripts/add_tool_with_position.sql" | & "C:\Program Files\MySQL\MySQL Server 5.7\bin\mysql.exe" -uroot -proot smart_cabinet --default-character-set=utf8mb4
    第三步：添加工具（管道传中文参数，可执行多条）
        "CALL sp_add_tool_with_position('工具编号','工具名称','规格',分类ID,机组ID,柜体ID,'层号','位号');" | & "C:\Program Files\MySQL\MySQL Server 5.7\bin\mysql.exe" -uroot -proot smart_cabinet --default-character-set=utf8mb4
    示例：
        "CALL sp_add_tool_with_position('JZ01-MIC50','千分尺50mm','25-50mm',3,1,1,'01','07');" | & "C:\Program Files\MySQL\MySQL Server 5.7\bin\mysql.exe" -uroot -proot smart_cabinet --default-character-set=utf8mb4

四、参数说明（8个，顺序固定）
    1. 工具编号  唯一，重复则跳过不插
    2. 工具名称
    3. 规格
    4. 分类ID    填0表示不分类
    5. 机组ID    本机机组
    6. 柜体ID
    7. 层号      两位数字，如 01
    8. 位号      两位数字，如 07

五、内置校验（不满足即报错中止，不会写坏数据）
    1. 编号/名称为空           报错
    2. 层号位号不是两位数字     报错
    3. 机组不存在              报错
    4. 柜体不存在              报错
    5. 分类不存在（填了非0ID）  报错
    6. 货位已被其他工具占用     报错并显示占用工具
    7. 工具编号已存在           自动跳过（可反复执行不重复插）

六、注意事项
    1. 密码必须 -proot 连写，-p root 中间带空格会打印Usage无法执行；
    2. 不要用 -e "source 脚本" 方式传中文（会报 ERROR 1270 排序规则冲突，
       也可能中文乱码），必须用上面第三步的管道方式；
    3. 写入字段与软件DAO层（insertTool/insertPositionMapping）完全一致；
    4. 升级补充多个工具时，第三步写多条 CALL 逐条执行即可；
    5. 本脚本只写 tool_info 与 tool_position_mapping 两张表，不动其他数据。
