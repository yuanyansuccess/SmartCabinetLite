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
# 注：备份已统一走 mysqldump 外部命令，不再需要直呼 DatabaseManager
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
# 覆盖全部层（pages/services/db/controller/model/common/components），并同时匹配 == 与 !=
STATE_RE = (r'[=!]=\s*"(scanning|capturing|success|fail|stranger|no-camera|borrowing|returned|'
            r'overdue|in_stock|pending|checked_out|maintenance|unhandled|handled|ignored)"')
for folder in (PAGES, SERVICES, COMPONENTS,
               os.path.join(ROOT, "db"), os.path.join(ROOT, "controller"),
               os.path.join(ROOT, "model"), os.path.join(ROOT, "common")):
    for path, fn in scan(folder):
        # Constants.h 是常量定义处，DatabaseManager.cpp 为建表/播种 SQL，均不适用本规则
        if fn in ("Constants.h", "DatabaseManager.cpp"):
            continue
        for m in re.finditer(STATE_RE, read(path)):
            violations.append("[常量] 状态值禁止裸字符串（用 SC:: 常量）: %s -> %s" % (fn, m.group(0)))

# 规则7：字号与触屏控件高度不得裸写数字（必须走 StyleHelper::Token）
# 只约束两类真正有触屏标准的取值：
#   a) 字号 setPointSize/setPixelSize —— 对应 Token::Font* 系列
#   b) 按钮/输入框等交互控件的高度 —— 对应 Token::ControlHeight* 系列
# 弹窗、卡片、容器、滑条的宽高属布局尺寸，各页差异大且无统一标准，
# 若强行塞进 Token 只会制造无语义的"伪令牌"，故不纳入本规则。
# 判定口径：只有「裸数字恰好等于某个 Token 取值」才算违规——即本可以走 Token 却裸写。
# 72/52/46 这类页面特定尺寸是真实设计差异，强行替换会改变视觉，不纳入约束。
# Token 取值直接从 StyleHelper.h 解析，避免脚本与头文件两处维护产生漂移。
STYLE_FILE = os.path.join(ROOT, "utils", "StyleHelper.h")
TOKEN_FONT, TOKEN_HEIGHT = set(), set()
for name, val in re.findall(r"static constexpr int (\w+)\s*=\s*(\d+);", read(STYLE_FILE)):
    if name.startswith("Font"):
        TOKEN_FONT.add(int(val))
    elif name.startswith("ControlHeight") or name == "RowHeight":
        TOKEN_HEIGHT.add(int(val))

FONT_SIZE_RE = r"set(PointSize|PixelSize)\(\s*(\d+)\s*\)"
TOUCH_HEIGHT_RE = (r"[A-Za-z0-9_]*(?:btn|Btn|Button|button|edit|Edit|input|Input|"
                   r"combo|Combo|line|Line|spin|Spin)[A-Za-z0-9_]*\s*->\s*"
                   r"set(Minimum|Fixed)Height\(\s*(\d+)\s*\)")
for folder in (PAGES, COMPONENTS):
    for path, fn in scan(folder):
        for line in read(path).splitlines():
            if "Token" in line or line.strip().startswith("//"):
                continue
            hit = False
            for m in re.finditer(FONT_SIZE_RE, line):
                if int(m.group(2)) in TOKEN_FONT:
                    hit = True
            for m in re.finditer(TOUCH_HEIGHT_RE, line):
                if int(m.group(2)) in TOKEN_HEIGHT:
                    hit = True
            if hit:
                violations.append("[令牌] 该取值已有 StyleHelper::Token，禁止裸写数字: %s -> %s"
                                  % (fn, line.strip()[:80]))
                break

# 统计项（不计违规）：QSS 文本串中的裸 font-size，供后续 Token 化治理参考
FONT_PX_TOTAL = 0
for folder in (PAGES, COMPONENTS):
    for path, fn in scan(folder):
        FONT_PX_TOTAL += len(re.findall(r"font-size:\s*\d+px", read(path)))

# ═══════════════════════════════════════════════════════════════
# 规则8：禁止遗留待办标记（TODO/FIXME/XXX/HACK）
# 待办必须记入 docs 手册的遗留待办表，而不是留在代码里腐烂。
# 需求类问题另见"历史修改记录注释"提示项。
# ═══════════════════════════════════════════════════════════════
TODO_MARK_RE = r"\b(TODO|FIXME|XXX|HACK)\b"
for folder in (PAGES, COMPONENTS, SERVICES,
               os.path.join(ROOT, "db"), os.path.join(ROOT, "controller"),
               os.path.join(ROOT, "model"), os.path.join(ROOT, "common")):
    for path, fn in scan(folder):
        for i, line in enumerate(read(path).splitlines(), 1):
            if re.search(TODO_MARK_RE, line):
                violations.append("[待办] 禁止遗留 TODO/FIXME（请记入手册遗留待办表）: %s:%d"
                                  % (fn, i))
                break

# ═══════════════════════════════════════════════════════════════
# 技术债提示项：只统计不阻断。
# 这些是存量问题，治理需要逐个人工确认（改命名、删历史注释、迁 Token），
# 一次性报成违规会让守护脚本长期红灯，反而失去约束力。
# 待存量清零后，可逐条升级为硬规则。
# ═══════════════════════════════════════════════════════════════
HIST_COMMENT_RE = (r"\[V\d+(\.\d+)+\]"
                   r"|(修复|重构|新增|变更|修改|调整|改为|删除|移除)\s*[:：]\s*\S"
                   r"|已删除|已修复|历史遗留|原来是|之前是|改成了")
SINGLE_VAR_RE = r"\b(?:int|qint64|double|float|bool|auto|QString|QStringList)\s+([a-hj-z])\s*[=;]"
SINGLE_VAR_ALLOW = set("ijktxyzhgwrb")  # 循环索引/坐标/宽高/颜色等通行惯例

debt_hist = []
debt_single = []
debt_author = 0
for folder in (PAGES, COMPONENTS, SERVICES,
               os.path.join(ROOT, "db"), os.path.join(ROOT, "controller"),
               os.path.join(ROOT, "model"), os.path.join(ROOT, "common"),
               os.path.join(ROOT, "utils")):
    for path, fn in scan(folder):
        text = read(path)
        for line in text.splitlines():
            s = line.strip()
            if s.startswith("//") and re.search(HIST_COMMENT_RE, s):
                debt_hist.append(fn)
                break
        for line in text.splitlines():
            if line.strip().startswith("//"):
                continue
            for m in re.finditer(SINGLE_VAR_RE, line):
                if m.group(1) not in SINGLE_VAR_ALLOW:
                    debt_single.append(fn)
                    break
        head = "\n".join(text.splitlines()[:20])
        if "袁燕" not in head:
            debt_author += 1

print("=" * 56)
print("架构守护报告 arch_guard")
print("=" * 56)
if violations:
    print("发现 %d 处违规：" % len(violations))
    for v in violations:
        print("  X", v)
    sys.exit(1)
print("全部架构规则通过")
print("─" * 56)
print("技术债提示（存量，不阻断；清零后可升级为硬规则）：")
print("  · 历史修改记录注释（应改功能说明）：%d 处" % len(debt_hist))
print("  · 无意义单字母变量名              ：%d 处" % len(debt_single))
print("  · 文件头缺「作者：袁燕」          ：%d 处" % debt_author)
print("  · QSS 裸 font-size                ：%d 处" % FONT_PX_TOTAL)
sys.exit(0)
