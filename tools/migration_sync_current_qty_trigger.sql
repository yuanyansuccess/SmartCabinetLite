-- ============================================================================
-- migration: 用 DB 触发器让 current_qty 成为 tool_position_mapping 的派生值
-- 单一真相源 = tool_position_mapping.status；current_qty = COUNT(status='in_stock')
-- 目的：消除"借/还/出/入"四条路径对 current_qty 列的手写同步漂移
--        （现状：borrow/return 路径翻转 mapping 却忘了更新数量列）
-- 可回滚：DROP TRIGGER trg_mpm_ai_current_qty; ... 三句即可撤销
-- ============================================================================
USE smart_cabinet;
DELIMITER $$

-- INSERT 映射行 → 重算该 tool 的 current_qty
DROP TRIGGER IF EXISTS trg_mpm_ai_current_qty$$
CREATE TRIGGER trg_mpm_ai_current_qty
AFTER INSERT ON tool_position_mapping
FOR EACH ROW
BEGIN
    UPDATE tool_info ti
       SET ti.current_qty = (SELECT COUNT(*) FROM tool_position_mapping m
                              WHERE m.tool_id = NEW.tool_id AND m.status = 'in_stock')
     WHERE ti.tool_id = NEW.tool_id;
END$$

-- UPDATE 映射行（status / tool_id 变化才需重算）→ 重算 OLD 与 NEW 两个 tool
DROP TRIGGER IF EXISTS trg_mpm_au_current_qty$$
CREATE TRIGGER trg_mpm_au_current_qty
AFTER UPDATE ON tool_position_mapping
FOR EACH ROW
BEGIN
    IF (OLD.status <> NEW.status) OR (OLD.tool_id <> NEW.tool_id) OR (OLD.tool_id IS NULL) OR (NEW.tool_id IS NULL) THEN
        IF OLD.tool_id IS NOT NULL THEN
            UPDATE tool_info ti
               SET ti.current_qty = (SELECT COUNT(*) FROM tool_position_mapping m
                                      WHERE m.tool_id = OLD.tool_id AND m.status = 'in_stock')
             WHERE ti.tool_id = OLD.tool_id;
        END IF;
        IF NEW.tool_id IS NOT NULL AND (NEW.tool_id <> OLD.tool_id OR OLD.tool_id IS NULL) THEN
            UPDATE tool_info ti
               SET ti.current_qty = (SELECT COUNT(*) FROM tool_position_mapping m
                                      WHERE m.tool_id = NEW.tool_id AND m.status = 'in_stock')
             WHERE ti.tool_id = NEW.tool_id;
        END IF;
    END IF;
END$$

-- DELETE 映射行 → 重算该 tool 的 current_qty
DROP TRIGGER IF EXISTS trg_mpm_ad_current_qty$$
CREATE TRIGGER trg_mpm_ad_current_qty
AFTER DELETE ON tool_position_mapping
FOR EACH ROW
BEGIN
    IF OLD.tool_id IS NOT NULL THEN
        UPDATE tool_info ti
           SET ti.current_qty = (SELECT COUNT(*) FROM tool_position_mapping m
                                  WHERE m.tool_id = OLD.tool_id AND m.status = 'in_stock')
         WHERE ti.tool_id = OLD.tool_id;
    END IF;
END$$

DELIMITER ;

-- ── 一次性回填：让 current_qty 立即等于真实在库位置数（消除历史漂移）──
UPDATE tool_info ti
   SET ti.current_qty = (SELECT COUNT(*) FROM tool_position_mapping m
                         WHERE m.tool_id = ti.tool_id AND m.status = 'in_stock');

SELECT 'migration applied: triggers created + current_qty backfilled' AS result;
