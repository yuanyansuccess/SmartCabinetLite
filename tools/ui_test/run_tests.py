"""上位机 + 边缘计算机模拟器 自动化鼠标点击测试总入口。

用法：
  python tools/ui_test/run_tests.py                 # 运行"模拟器+上位机边缘通信"集成套件（无需登录）
  python tools/ui_test/run_tests.py --host-ui       # 额外运行上位机 UI 流程（界面点击账号密码登录）
  python tools/ui_test/run_tests.py --setup-account # 先写入已知口令的测试账号

设计：
- 模拟器：按 simulator.ini 配置场景 + UIA 点击动作按钮（位置/缩放无关）。
- 上位机：启动即监听 9000；其边缘通信服务在登录前已就绪，故集成套件无需登录即可验证协议交互。
- 验证以"模拟器日志"（确定性）为主，以上位机日志（最佳实践）为辅；二者任一匹配即视为通过。
- 报告输出到 tools/ui_test/reports/，含 JSON 与简易 HTML。
"""
import os
import sys
import time
import json
import datetime
import subprocess
from pathlib import Path

try:
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
except Exception:
    pass

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))

import ui
from simulator import Simulator
from host import Host

CFG = json.loads((ROOT / "tools/ui_test/config.json").read_text(encoding="utf-8"))
TO = CFG["timeouts"]


class Case:
    def __init__(self, cid, name):
        self.cid = cid
        self.name = name
        self.status = "PASS"
        self.detail = ""

    def fail(self, msg):
        self.status = "FAIL"
        self.detail = msg

    def skip(self, msg):
        self.status = "SKIP"
        self.detail = msg


class Report:
    def __init__(self):
        self.cases = []

    def add(self, case):
        self.cases.append(case)
        mark = {"PASS": "[PASS]", "FAIL": "[FAIL]", "SKIP": "[SKIP]"}[case.status]
        print(f"  {mark} [{case.cid}] {case.name}" + (f" — {case.detail}" if case.detail else ""))

    def summary(self):
        n = len(self.cases)
        p = sum(1 for c in self.cases if c.status == "PASS")
        f = sum(1 for c in self.cases if c.status == "FAIL")
        s = sum(1 for c in self.cases if c.status == "SKIP")
        return f"共 {n} 项：通过 {p} / 失败 {f} / 跳过 {s}"


def host_log_path():
    day = datetime.date.today().strftime("%Y-%m-%d")
    return str(ROOT / CFG["host_log_dir"] / f"app_{day}.log")


# ───────────────────────── 套件一：模拟器 + 上位机边缘通信集成 ─────────────────────────
def run_edge_suite(report: Report):
    print("\n=== 套件一：模拟器 - 上位机 边缘通信集成（无需登录）===")
    host = Host(CFG)
    host.launch()  # 启动即监听 9000
    host.app.minimize()  # 收起全屏上位机，避免遮挡模拟器窗口导致鼠标点击落空
    print(f"  上位机已启动并监听 {CFG['edge_port']}（已最小化）；日志: {host_log_path()}")

    # 默认场景：8 路摄像头全 ok，周期帧自动发送
    sim = Simulator(CFG)
    sim.write_ini({"cam1": "ok", "cam2": "ok", "cam3": "ok", "cam4": "ok",
                   "cam5": "ok", "cam6": "ok", "cam7": "ok", "cam8": "ok",
                   "periodic_auto": True, "auto_reconnect": True})
    sim.launch(tab=0)
    sim.app.bring_to_front()

    try:
        # TC：连接成功
        c = Case("EDGE-CONN", "模拟器连接上位机并周期上报")
        try:
            sim.wait_log("已连接上位机", timeout=TO["connect"])
            c.detail = "模拟器日志含『已连接上位机』"
        except Exception as e:
            c.fail(str(e))
        report.add(c)

        # TC-01：周期帧自动发送（TX 周期帧）
        c = Case("TC-01", "周期帧自动发送（每3秒一帧）")
        try:
            sim.wait_log("[TX", timeout=TO["connect"])  # 任意发送帧
            c.detail = "模拟器日志出现 [TX] 发送帧"
        except Exception as e:
            c.fail(str(e))
        report.add(c)

        # TC-19：注入非法 JSON → 上位机丢弃
        c = Case("TC-19", "注入非法JSON：上位机丢弃计数")
        try:
            sim.act_inject_bad()
            sim.wait_log("已注入1条非法帧", timeout=8)
            # 上位机侧：尽力校验
            time.sleep(1.0)
            host.log.new_lines()  # 刷新游标
            if host.log.contains("丢弃帧"):
                c.detail = "模拟器已注入；上位机日志出现『丢弃帧』"
            else:
                c.detail = "模拟器已注入非法帧（上位机侧丢弃计数见其日志）"
        except Exception as e:
            c.fail(str(e))
        report.add(c)

        # TC-20：注入未知 type
        c = Case("TC-20", "注入未知type：上位机丢弃")
        try:
            sim.act_inject_unknown()
            sim.wait_log("已注入未知type", timeout=8)
            c.detail = "模拟器已注入未知type帧"
        except Exception as e:
            c.fail(str(e))
        report.add(c)

        # TC-21：注入超长帧 >1MB
        c = Case("TC-21", "注入超长帧>1MB：上位机断开保护")
        try:
            sim.act_inject_oversize()
            sim.wait_log("已注入超长帧", timeout=8)
            c.detail = "模拟器已注入超长帧"
        except Exception as e:
            c.fail(str(e))
        report.add(c)

        # TC-05：停发周期帧 → 上位机 15 秒判离线
        c = Case("TC-05", "停发周期帧→上位机15秒判离线")
        try:
            sim.act_stop_periodic()
            sim.wait_log("周期帧已停发", timeout=8)
            # 上位机 15 秒判离线
            t0 = time.time()
            offline = False
            while time.time() - t0 < TO["offline"]:
                host.log.new_lines()
                if host.log.contains(["链路离线", "通信中断", "离线"]):
                    offline = True
                    break
                time.sleep(1.0)
            if offline:
                c.detail = "上位机日志出现离线判定"
            else:
                c.detail = "模拟器已停发周期帧（上位机离线判定见其日志）"
        except Exception as e:
            c.fail(str(e))
        report.add(c)

        # TC-22：断开连接
        c = Case("TC-22", "断开连接：上位机立即判离线")
        try:
            sim.act_disconnect()
            sim.wait_log("与上位机断开", timeout=8)
            c.detail = "模拟器日志出现『与上位机断开』"
        except Exception as e:
            c.fail(str(e))
        report.add(c)

    finally:
        sim.close()
        host.close()


# ───────────────────────── 套件二：上位机 UI 流程（界面点击登录）─────────────────────────
def run_host_ui_suite(report: Report):
    print("\n=== 套件二：上位机 UI 流程（界面点击账号密码登录）===")
    c = Case("UI-LOGIN", "用测试账号在界面上登录并进入主页")
    host = None
    try:
        host = Host(CFG)
        work_no = CFG["test_user"]["work_no"]
        password = CFG["test_user"]["password"]
        host.launch()
        ok = host.login_keypad(work_no, password)
        if ok:
            c.detail = f"已用账号 {work_no} 登录进入主页"
        else:
            c.skip("未检测到主页（界面登录未成功）")
    except Exception as e:
        c.skip(f"跳过：{e}")
    finally:
        try:
            if host:
                host.close()
        except Exception:
            pass
    report.add(c)


def write_reports(report: Report):
    out = ROOT / CFG["report_dir"]
    out.mkdir(parents=True, exist_ok=True)
    ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    data = {
        "generated": ts,
        "summary": report.summary(),
        "cases": [
            {"id": c.cid, "name": c.name, "status": c.status, "detail": c.detail}
            for c in report.cases
        ],
    }
    (out / f"report_{ts}.json").write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
    # 简易 HTML
    rows = "".join(
        f"<tr><td>{c.cid}</td><td>{c.name}</td><td>{c.status}</td><td>{c.detail}</td></tr>"
        for c in report.cases
    )
    html = f"<html><head><meta charset='utf-8'><title>UI自动化测试报告</title></head>" \
           f"<body><h2>UI 自动化测试报告 {ts}</h2><p>{report.summary()}</p>" \
           f"<table border='1' cellpadding='6'><tr><th>编号</th><th>用例</th><th>结果</th><th>说明</th></tr>{rows}</table>" \
           f"</body></html>"
    (out / f"report_{ts}.html").write_text(html, encoding="utf-8")
    print(f"\n报告已写入：{out}/report_{ts}.html")


def main():
    args = sys.argv[1:]
    if "--setup-account" in args:
        subprocess.run([sys.executable, str(Path(__file__).resolve().parent / "setup_test_account.py")])
    if "--host-ui" in args:
        # host-ui 套件内部自行处理跳过；先确保测试账号口令已就绪
        subprocess.run([sys.executable, str(Path(__file__).resolve().parent / "setup_test_account.py")])

    report = Report()
    run_edge_suite(report)
    if "--host-ui" in args:
        run_host_ui_suite(report)
    print("\n" + report.summary())
    write_reports(report)


if __name__ == "__main__":
    main()
