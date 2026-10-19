-- ============================================================================
-- db_fill_missing_positions.sql — 为"从未入库"的缺位工具补灌位置
-- 目标：JZ01-SB(6)、JZ01-CRV(12)，均属机组3(液压系统机组)
-- 原则：先校验位置空闲 → 建 in_stock 映射 → 同步主档状态消除漂移 → 验证
-- 执行前请按实际物理位置填写下方参数（参考：机组3现位于柜1/柜3）
-- ============================================================================

-- ── 0) 填写目标位置参数（已为机组3所在柜3挑好真实空闲位，请按实际布局确认）──
-- 注意：用户变量需显式 COLLATE 与表列(utf8mb4_unicode_ci)一致，否则比较报 collation 冲突
SET @cab1 = 3,
    @lay1 = CAST('01' AS CHAR CHARACTER SET utf8mb4) COLLATE utf8mb4_unicode_ci,
    @pos1 = CAST('01' AS CHAR CHARACTER SET utf8mb4) COLLATE utf8mb4_unicode_ci;   -- JZ01-SB  → 柜3/层01/位01（空闲）
SET @cab2 = 3,
    @lay2 = CAST('01' AS CHAR CHARACTER SET utf8mb4) COLLATE utf8mb4_unicode_ci,
    @pos2 = CAST('03' AS CHAR CHARACTER SET utf8mb4) COLLATE utf8mb4_unicode_ci;   -- JZ01-CRV → 柜3/层01/位03（空闲）

-- ── 1) 校验：位置未被占用（应返回 0 行；若有行说明冲突，改上面的参数） ────────
SELECT cabinet_id, layer, position, status
FROM tool_position_mapping
WHERE (cabinet_id = @cab1 AND layer = @lay1 AND position = @pos1)
   OR (cabinet_id = @cab2 AND layer = @lay2 AND position = @pos2);

-- ── 2) 建立 in_stock 位置映射（位置必填，落进映射表） ─────────────────────────
INSERT INTO tool_position_mapping (tool_id, cabinet_id, layer, position, status)
VALUES (6,  @cab1, @lay1, @pos1, 'in_stock');
INSERT INTO tool_position_mapping (tool_id, cabinet_id, layer, position, status)
VALUES (12, @cab2, @lay2, @pos2, 'in_stock');

-- ── 3) 同步工具主档状态为 in_stock（消除 pending 状态漂移） ────────────────────
UPDATE tool_info SET status = 'in_stock' WHERE tool_id IN (6, 12);

-- ── 4) 验证：应各返回 1 行，map_status 为 in_stock ───────────────────────────
SELECT t.tool_id, t.tool_code, t.status AS tool_status,
       m.cabinet_id, m.layer, m.position, m.status AS map_status
FROM tool_info t
JOIN tool_position_mapping m ON m.tool_id = t.tool_id
WHERE t.tool_id IN (6, 12);
