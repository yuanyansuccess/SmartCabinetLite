"""UI 自动化底层：基于 pywinauto(uia) 的"按控件名点击"，位置/缩放无关，新人可直接复用。

设计要点：
- 按进程 PID 连接窗口，避开 UIA 同名多窗口歧义；
- 按钮按文字（name）定位，下拉框按 automation_id 定位（模拟器摄像头用 cam1..cam8）；
- 日志按"文件大小游标"增量读取，避免重复扫全文件；
- 截图用 PNG 落盘，供失败复盘与手册配图。
"""
import os
import re
import time
import socket
import subprocess
from pathlib import Path

from pywinauto import Application

ROOT = Path(__file__).resolve().parents[2]  # tools/ui_test -> SmartCabinetLite


class UIError(Exception):
    pass


def rel(p: str) -> str:
    return str((ROOT / p).resolve()) if not os.path.isabs(p) else p


class WinApp:
    """封装一个 GUI 程序的启动、连接与控件操作。"""

    def __init__(self, exe, cwd=None, args=None, title_re=None, timeout=25):
        self.exe = rel(exe)
        self.cwd = rel(cwd) if cwd else None
        self.title_re = title_re
        self.proc = subprocess.Popen([self.exe] + (args or []), cwd=self.cwd)
        self.app = Application(backend="uia").connect(process=self.proc.pid, timeout=timeout)

    # ---- 控件定位 ----
    def _spec(self):
        return self.app.window()

    def click_button(self, text, substring=False, timeout=10):
        spec = self._spec()
        if substring:
            el = spec.child_window(title_re=".*%s.*" % re.escape(text), control_type="Button")
        else:
            el = spec.child_window(title=text, control_type="Button")
        el.wait("exists visible enabled", timeout=timeout)
        # 优先真实鼠标点击；若窗口被遮挡则回退到 UIA InvokePattern（程序化触发，无视层级）
        try:
            el.click()
        except Exception:
            try:
                el.invoke()
            except Exception:
                el.click_input()

    def toggle_checkbox(self, text, substring=False, timeout=10):
        spec = self._spec()
        if substring:
            el = spec.child_window(title_re=".*%s.*" % re.escape(text), control_type="CheckBox")
        else:
            el = spec.child_window(title=text, control_type="CheckBox")
        el.wait("exists visible enabled", timeout=timeout)
        try:
            el.toggle()
        except Exception:
            el.click()
        return el

    def select_tab(self, text, substring=False, timeout=10):
        spec = self._spec()
        if substring:
            el = spec.child_window(title_re=".*%s.*" % re.escape(text), control_type="TabItem")
        else:
            el = spec.child_window(title=text, control_type="TabItem")
        el.wait("exists visible", timeout=timeout)
        try:
            el.select() if hasattr(el, "select") else el.click()
        except Exception:
            el.click()

    def set_combo(self, auto_id, value, timeout=10):
        el = self._spec().child_window(auto_id=auto_id, control_type="ComboBox")
        el.wait("exists", timeout=timeout)
        try:
            el.select(value)
        except Exception:
            # Qt ComboBox 在 UIA 下兜底：展开后点选项
            el.expand()
            el.child_window(title=value, control_type="ListItem").click()

    def set_edit(self, name, text, timeout=10):
        el = self._spec().child_window(title=name, control_type="Edit")
        el.wait("exists", timeout=timeout)
        el.set_edit_text(text)

    def window_text(self):
        return self.app.top_window().window_text()

    def minimize(self):
        self.app.top_window().minimize()

    def bring_to_front(self):
        try:
            self.app.top_window().bring_to_front()
        except Exception:
            self.app.top_window().set_focus()

    def wait_window(self, title_re, timeout=10):
        return self.app.window(title_re=title_re).wait("exists", timeout=timeout)

    def screenshot(self, path):
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        self._spec().capture_as_image().save(str(path))

    def close(self):
        try:
            self.proc.terminate()
        except Exception:
            pass


class LogWatcher:
    """按文件大小游标增量读取日志，支持"等待出现某关键字"。"""

    def __init__(self, path):
        self.path = path
        self._pos = 0
        if os.path.exists(path):
            self._pos = os.path.getsize(path)

    def new_lines(self, limit=5000):
        if not os.path.exists(self.path):
            return []
        with open(self.path, encoding="utf-8", errors="ignore") as f:
            f.seek(self._pos)
            data = f.read()
            self._pos = f.tell()
        return data.splitlines()[-limit:]

    def wait_for(self, patterns, timeout=15, poll=0.3):
        """patterns: 字符串或字符串列表；任一出现即返回匹配行。超时抛 TimeoutError。"""
        if isinstance(patterns, str):
            patterns = [patterns]
        deadline = time.time() + timeout
        buf = []
        while time.time() < deadline:
            buf.extend(self.new_lines())
            for line in buf:
                for p in patterns:
                    if p in line:
                        return line
            time.sleep(poll)
        raise TimeoutError("日志在 %ss 内未出现: %s" % (timeout, patterns))

    def contains(self, patterns, lookback=2000):
        lines = self.new_lines(limit=lookback) or self.tail(lookback)
        pats = [patterns] if isinstance(patterns, str) else patterns
        return any(any(p in ln for p in pats) for ln in lines)

    def tail(self, n=2000):
        if not os.path.exists(self.path):
            return []
        with open(self.path, encoding="utf-8", errors="ignore") as f:
            return f.readlines()[-n:]


def wait_port_open(host, port, timeout=25, poll=0.5):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection((host, port), timeout=1):
                return True
        except OSError:
            time.sleep(poll)
    return False
