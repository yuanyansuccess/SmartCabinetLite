#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import re, os, sys

out = []
def log(*a):
    out.append(" ".join(str(x) for x in a))

HERE = os.path.dirname(os.path.abspath(__file__))
h = os.path.join(HERE, "..", "src", "utils", "PinyinDictData.h")
t = open(h, encoding="utf-8").read()
entries = re.finditer(r'\{\s*"([^"]+)"\s*,\s*"((?:\\x[0-9a-fA-F]{2}|[^"\\])*)"', t)
data = {}
for m in entries:
    py = m.group(1)
    esc = m.group(2)
    raw = bytearray()
    for b in re.finditer(r'\\x([0-9a-fA-F]{2})', esc):
        raw.append(int(b.group(1), 16))
    try:
        hanzi = raw.decode("utf-8")
    except Exception:
        hanzi = ""
    data[py] = list(hanzi)

log("li first12:", "".join(data.get("li", []))[:12])
log("zhang first8:", "".join(data.get("zhang", []))[:8])
log("wang first6:", "".join(data.get("wang", []))[:6])
log("li 里 index:", "".join(data.get("li", [])).find("里"))

# 姓氏集合
sm = re.search(r'kSurnameChars\[\]\s*=\s*\{(.*?)\}', t, re.S)
surn = re.findall(r'"((?:\\x[0-9a-fA-F]{2})*)"', sm.group(1))
surset = set()
for s in surn:
    b = re.finditer(r'\\x([0-9a-fA-F]{2})', s)
    bb = bytes(int(x.group(1), 16) for x in b)
    try:
        surset.add(bb.decode("utf-8"))
    except Exception:
        pass
log("surname count:", len(surset), "含张/王/李/赵:", all(c in surset for c in "张王李赵"))

# 词组
hp = os.path.join(HERE, "..", "src", "utils", "PinyinPhraseData.h")
tp = open(hp, encoding="utf-8").read()
pe = re.finditer(r'\{\s*"([^"]+)"\s*,\s*"((?:\\x[0-9a-fA-F]{2}|[^"\\])*)"', tp)
phrases = {}
for m in pe:
    py = m.group(1)
    esc = m.group(2)
    raw = bytearray()
    for b in re.finditer(r'\\x([0-9a-fA-F]{2})', esc):
        raw.append(int(b.group(1), 16))
    try:
        phrases[py] = raw.decode("utf-8")
    except Exception:
        phrases[py] = ""
log("zhongguo ->", phrases.get("zhongguo"))
log("beijing ->", phrases.get("beijing"))
log("xingming ->", phrases.get("xingming"))
log("zhang/li/wang/zhao phrase keys:", [k for k in phrases if k in ("zhang", "li", "wang", "zhao")])

open(os.path.join(HERE, "diag_out.txt"), "w", encoding="utf-8").write("\n".join(out))
print("\n".join(out))

