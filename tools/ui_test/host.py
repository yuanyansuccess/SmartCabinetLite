"""上位机（QtSmartCabinet）控制器：启动、登录、导航到出入库流程。

登录方式：按真人操作路径在界面上点击"使用账号密码登录"，再用软键盘输入工号与口令，
          全程不依赖生产程序里的任何测试开关（生产程序不提供测试入口）。

注意：上位机为全屏触屏程序，所有点击均基于控件名/坐标，与窗口位置无关。
"""
import os
import time
import datetime
from pathlib import Path

import pyautogui

from ui import WinApp, LogWatcher, rel, wait_port_open


class Host:
    def __init__(self, cfg):
        self.cfg = cfg
        self.app = None
        self.log_path = None

    def launch(self):
        self.app = WinApp(
            self.cfg["host_exe"],
            cwd=self.cfg["host_workdir"],
            title_re="智能工具柜管理系统",
            timeout=self.cfg["timeouts"]["app_start"],
        )
        # 等待监听 9000（边缘机通信服务随主程序启动）
        wait_port_open("127.0.0.1", self.cfg["edge_port"],
                       timeout=self.cfg["timeouts"]["app_start"])
        # 日志按天滚动：logs/app_YYYY-MM-DD.log
        day = datetime.date.today().strftime("%Y-%m-%d")
        self.log_path = rel(self.cfg["host_log_dir"]) + f"/app_{day}.log"
        self.log = LogWatcher(self.log_path)
        return self

    # ---------- 登录 ----------
    def login_keypad(self, work_no, password):
        """在界面上用软键盘输入工号与口令登录（与人工操作路径一致）。"""
        self.app.click_button("使用账号密码登录", substring=True)
        time.sleep(1.0)
        # 点击用户名输入框（首个可见 Edit），弹出软键盘
        self._type_keypad(work_no)
        # 切到密码后再次弹键盘
        self._type_keypad(password, expect_switch=True)
        return self.wait_home(timeout=10)

    def _type_keypad(self, text, expect_switch=False):
        # 取上位机窗口矩形作为"屏幕"近似（全屏）
        r = self.app._spec().rectangle()
        W = r.width() if hasattr(r, "width") else (r.right - r.left)
        H = r.height() if hasattr(r, "height") else (r.bottom - r.top)
        panelW = min(460, max(320, int(W * 0.90)))
        panelH = 412
        panelX = r.left + W // 2 - panelW // 2
        panelY = r.top + H - panelH - 8
        padL = 16
        avail = panelW - 32
        colW = (avail - 16) / 3.0
        lastW = (avail - 8) / 2.0
        # 数字键中心映射（未打乱位）
        digit_pos = {
            "7": (0, 0), "8": (0, 1), "9": (0, 2),
            "4": (1, 0), "5": (1, 1), "6": (1, 2),
            "1": (2, 0), "2": (2, 1), "3": (2, 2),
            "0": (3, 0), "⌫": (3, 1),
        }
        # 先点一下用户名/密码框以唤起键盘（兜底：点窗口中部偏上）
        def key_center(ch):
            row, col = digit_pos[ch]
            if ch == "0":
                x = panelX + padL + lastW / 2
            elif ch == "⌫":
                x = panelX + padL + lastW + 8 + lastW / 2
            else:
                x = panelX + padL + col * (colW + 8) + colW / 2
            y = panelY + 96 + row * 64 + 28
            return int(x), int(y)

        for ch in text:
            if ch not in digit_pos:
                continue
            x, y = key_center(ch)
            pyautogui.click(x, y)
            time.sleep(0.15)
        # 点确认（footer 中心）
        cx = panelX + panelW // 2
        cy = panelY + 368
        pyautogui.click(cx, cy)
        time.sleep(0.6)

    # ---------- 导航 ----------
    def wait_home(self, timeout=15):
        # 主页出现"工具出入库"等卡片/菜单即视为登录成功
        try:
            self.app.wait_window(".*出入库.*", timeout=timeout)
            return True
        except Exception:
            # 退一步：等待"退出"按钮出现（TopBar）
            try:
                self.app.click_button("退出", substring=True, timeout=timeout)
                # 刚点错地方会弹退出确认，撤销
                self.app.click_button("取消", substring=True, timeout=3)
            except Exception:
                pass
            return False

    def navigate(self, name):
        self.app.click_button(name, substring=True)

    def screenshot(self, path):
        return self.app.screenshot(path)

    def close(self):
        if self.app:
            self.app.close()
