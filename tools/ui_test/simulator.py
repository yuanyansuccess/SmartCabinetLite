"""边缘计算机模拟器控制器：用 simulator.ini 配置场景 + UIA 点击动作按钮。

模拟器在启动时会从 exe 同目录 simulator.ini 恢复上次配置（摄像头/应答场景/开箱策略/版本/连接）。
因此绝大多数"场景设置"不需要点击下拉框，直接写 ini 即可；需要一次性动作时才点击按钮。
"""
import os
from pathlib import Path
import configparser

from ui import WinApp, LogWatcher, rel


class Simulator:
    def __init__(self, cfg):
        self.cfg = cfg
        self.ini_path = Path(rel(cfg["sim_workdir"])) / "simulator.ini"
        self.log_path = rel(cfg["sim_log"])
        self.app = None

    # ---------- 配置（写 ini，重启生效）----------
    def write_ini(self, scen: dict):
        cp = configparser.ConfigParser()
        cp["camera"] = {f"cam{i}": scen.get(f"cam{i}", "ok") for i in range(1, 9)}
        cp["reply"] = {
            "sceneIndex": str(scen.get("reply_scene", 0)),
            "delayMs": str(scen.get("delay_ms", 200)),
        }
        cp["open"] = {
            "policyIndex": str(scen.get("open_policy", 0)),
            "failMsg": scen.get("open_fail", "开箱失败（模拟）"),
        }
        cp["version"] = {"value": scen.get("version", "1.2.0")}
        cp["conn"] = {
            "addr": scen.get("addr", "127.0.0.1"),
            "port": str(scen.get("port", self.cfg["edge_port"])),
            "autoReconnect": "true" if scen.get("auto_reconnect", True) else "false",
        }
        cp["periodic"] = {
            "auto": "true" if scen.get("periodic_auto", True) else "false",
            "intervalMs": str(scen.get("periodic_ms", 3000)),
            "uptime": str(scen.get("uptime", 123)),
        }
        # 清空旧序列，避免历史序列影响单次场景
        cp["reply"]["seqEnabled"] = "false"
        with open(self.ini_path, "w", encoding="utf-8") as f:
            cp.write(f)

    # ---------- 生命周期 ----------
    def launch(self, tab=0):
        # 模拟器自身以 Truncate 方式打开日志，覆盖旧内容；这里仅尽力清理（忽略安全删除拦截）
        try:
            if os.path.exists(self.log_path):
                os.remove(self.log_path)
        except OSError:
            pass
        self.app = WinApp(
            self.cfg["sim_exe"],
            cwd=self.cfg["sim_workdir"],
            args=["--auto-connect", "--tab", str(tab)],
            title_re="边缘计算机模拟器",
            timeout=self.cfg["timeouts"]["app_start"],
        )
        self.log = LogWatcher(self.log_path)
        return self

    def click(self, text, substring=True, timeout=10):
        return self.app.click_button(text, substring=substring, timeout=timeout)

    def select_tab(self, text):
        return self.app.select_tab(text)

    def set_combo(self, auto_id, value):
        return self.app.set_combo(auto_id, value)

    def screenshot(self, path):
        return self.app.screenshot(path)

    def close(self):
        if self.app:
            self.app.close()

    # 便捷动作（按按钮文字点击）
    def act_send_periodic_now(self):
        self.click("立即发一帧周期帧")

    def act_stop_periodic(self):
        # 取消"自动发送"勾选 = 停发周期帧
        self.app.toggle_checkbox("自动发送")

    def act_dup_reply(self):
        self.click("重复回应")

    def act_stick_frames(self):
        self.click("两帧拼一行")

    def act_silent_drop(self):
        self.click("停发周期帧")

    def act_inject_bad(self):
        self.click("注入非法JSON")

    def act_inject_unknown(self):
        self.click("注入未知type")

    def act_inject_oversize(self):
        self.click("注入超长帧")

    def act_bad_request_id(self):
        self.click("回应乱序request_id")

    def act_disconnect(self):
        self.click("断开连接")

    def act_version_now(self):
        self.click("立即回 version_ack")

    # ---------- 日志断言 ----------
    def log_tail(self, n=2000):
        return self.log.tail(n)

    def wait_log(self, patterns, timeout=15):
        return self.log.wait_for(patterns, timeout=timeout)

    def log_has(self, patterns, lookback=3000):
        return self.log.contains(patterns, lookback=lookback)
