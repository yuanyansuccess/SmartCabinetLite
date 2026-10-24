"""Probe3: 用 ElementInfo 遍历 EdgeSimulator 的 UIA 控件树。"""
import subprocess, time
from pywinauto import Application

SIM = r"d:\CFDZ\smartCabinet\trunk\SmartCabinetLite\tools\EdgeSimulator\release\EdgeSimulator.exe"

def dump(ei, depth=0, maxdepth=8):
    if depth > maxdepth:
        return
    try:
        t = ei.control_type
        name = ei.name
        auto = ei.automation_id
        print("  " * depth + f"[{t}] name={name!r} id={auto!r}")
    except Exception as e:
        print("  " * depth + f"(err {e})")
        return
    try:
        for c in ei.children():
            dump(c, depth + 1, maxdepth)
    except Exception as e:
        print("  " * depth + f"(children err {e})")

def main():
    p = subprocess.Popen([SIM, "--auto-connect"])
    time.sleep(3)
    try:
        app = Application(backend="uia").connect(process=p.pid)
        w = app.window().wrapper_object()
        dump(w.element_info)
    except Exception as e:
        print("FAILED:", repr(e))
    finally:
        try:
            p.terminate()
        except Exception:
            pass

if __name__ == "__main__":
    main()
