#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import re, os
HERE = os.path.dirname(os.path.abspath(__file__))
h = os.path.join(HERE, "..", "src", "utils", "PinyinDictData.h")
b = open(h, "rb").read()
# 真实数据条目以 {"li" 开头
i = b.find(b'{"li"')
seg = b[i:i+90]
print("RAW:", " ".join("%02x" % x for x in seg))
# 用正则抓 py 与 hanzi 转义段（仅匹配数据条目格式 "py", "..."）
text = b.decode("utf-8")
m = re.search(r'\{\s*"li"\s*,\s*"((?:\\x[0-9a-fA-F]{2}|[^"\\])*)"', text)
esc = m.group(1)
if "\\x" in esc:
    bb = bytearray()
    for x in re.finditer(r'\\x([0-9a-fA-F]{2})', esc):
        bb.append(int(x.group(1), 16))
    dec = bb.decode("utf-8")
else:
    dec = esc
print("DECODED li head:", dec[:12])
print("里 at:", dec.find("里"))
print("力 at:", dec.find("力"))
