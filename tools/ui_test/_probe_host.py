"""Probe: 启动上位机并 dump 可交互控件（写入 UTF-8 文件，避免 GBK 崩溃）。"""
import subprocess, time, os
from pywinauto import Application

HOST = r"d:\CFDZ\smartCabinet\trunk\SmartCabinetLite\build\Debug\QtSmartCabinet.exe"
OUT = r"d:\CFDZ\smartCabinet\trunk\SmartCabinetLite\tools\ui_test\probe_host.txt"

INTERACTIVE = {"Button", "Edit", "ComboBox", "TabItem", "CheckBox", "MenuItem", "Hyperlink"}

def san(s):
    if s is None:
        return ""
    return s.encode("ascii", "replace").decode("ascii") if False else s

def dump(ei, depth=0, maxdepth=9, out=None, path=""):
    if depth > maxdepth:
        return
    try:
        t = ei.control_type
        name = ei.name or ""
        auto = ei.automation_id or ""
        if t in INTERACTIVE:
            line = "  " * depth + f"[{t}] {name!r} id={auto!r}"
            print(line, file=out)
    except Exception as e:
        print("  " * depth + f"(err {e})", file=out)
        return
    try:
        for c in ei.children():
            dump(c, depth + 1, maxdepth, out)
    except Exception as e:
        print("  " * depth + f"(children err {e})", file=out)

def main():
    p = subprocess.Popen([HOST])
    print("launched host pid", p.pid)
    time.sleep(10)
    try:
        app = Application(backend="uia").connect(process=p.pid, timeout=10)
        w = app.window().wrapper_object()
        with open(OUT, "w", encoding="utf-8") as f:
            dump(w.element_info, out=f)
        print("OK wrote", OUT)
    except Exception as e:
        with open(OUT, "w", encoding="utf-8") as f:
            f.write("FAILED: " + repr(e) + "\n")
    finally:
        try:
            p.terminate()
        except Exception:
            pass

if __name__ == "__main__":
    main()
