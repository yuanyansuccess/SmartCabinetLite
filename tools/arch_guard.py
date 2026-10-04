# -*- coding: utf-8 -*-
"""arch_guard.py - 架构守护脚本（规则代码化，防止无意破坏分层）
用法: python tools/arch_guard.py    退出码 0=通过 1=有违规
作者：袁燕"""
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "src"))
PAGES = os.path.join(ROOT, "pages")
COMPONENTS = os.path.join(ROOT, "components")
SERVICES = os.path.join(ROOT, "services")
violations = []
# 规则2 例外：数据库备份属基础设施操作，需连接信息判断DB类型，由页面层调度
PAGE_DB_MANAGER_ALLOW = {"SystemSettingsPage.cpp"}


def scan(folder, exts=(".cpp", ".h")):
    for dirpath, _d, files in os.walk(folder):
        for fn in files:
            if fn.endswith(exts):
                yield os.path.join(dirpath, fn), fn


def read(p):
    with open(p, encoding="utf-8") as f:
        return f.read()


# 规则1：pages 不得写 SQL
for path, fn in scan(PAGES):
    content = read(path)
    for pat in (r"QSqlQuery", r"\.prepare\(", r"lastInsertId"):
        if re.search(pat, content):
            violations.append("[SQL] pages 禁止直接操作数据库: %s (%s)" % (fn, pat))

# 规则2：pages 不得直呼 DatabaseManager
for path, fn in scan(PAGES):
    for line in read(path).splitlines():
        if fn in PAGE_DB_MANAGER_ALLOW:
            continue
        if "DatabaseManager" in line and ("instance()" in line or "DatabaseManager::" in line):
            violations.append("[分层] pages 禁止直呼 DatabaseManager: %s" % fn)
            break

# 规则3：禁止 Qt4 信号槽宏（应统一函数指针/lambda）
for folder in (PAGES, COMPONENTS, SERVICES, os.path.join(ROOT, "common")):
    for path, fn in scan(folder):
        if re.search(r"SIGNAL\(|SLOT\(", read(path)):
            violations.append("[信号槽] 禁止 SIGNAL/SLOT 宏: %s" % fn)

# 规则4：components 不得读 AppConfig（配置属基础设施，应由页面注入）
for path, fn in scan(COMPONENTS):
    if "AppConfig::instance()" in read(path):
        violations.append("[分层] components 禁止读 AppConfig（应由页面注入）: %s" % fn)

# 规则5：services 不得直接取配置单例（应构造注入）
for path, fn in scan(SERVICES):
    if "AppConfig::instance()" in read(path):
        violations.append("[注入] services 禁止直接取配置单例: %s" % fn)

# 规则6：状态值不得裸字符串比较（应走 SC:: 常量）
STATE_RE = r'==\s*"(scanning|capturing|success|fail|stranger|no-camera|borrowing|returned|overdue|in_stock|pending|checked_out)"'
for path, fn in scan(PAGES):
    for m in re.finditer(STATE_RE, read(path)):
        violations.append("[常量] 状态值禁止裸字符串（用 SC:: 常量）: %s" % fn)

print("=" * 56)
print("架构守护报告 arch_guard")
print("=" * 56)
if violations:
    print("发现 %d 处违规：" % len(violations))
    for v in violations:
        print("  X", v)
    sys.exit(1)
print("全部架构规则通过")
sys.exit(0)