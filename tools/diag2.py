#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import re, os

HERE = os.path.dirname(os.path.abspath(__file__))
h = os.path.join(HERE, "..", "src", "utils", "PinyinDictData.h")
raw_bytes = open(h, "rb").read()
text = raw_bytes.decode("utf-8")
# 找 li 条目
m = re.search(r'"li",\s*"((?:\\x[0-9a-fA-F]{2}|[^"\\])*)"', text)
esc = m.group(1)
hexstr = " ".join("%02x" % b for b in raw_bytes[
    text.index(esc):text.index(esc)+60])
# 实际字节重建
bb = bytearray()
for x in re.finditer(r'\\x([0-9a-fA-F]{2})', esc):
    bb.append(int(x.group(1), 16))
decoded = bb.decode("utf-8")
out = []
out.append("first 20 hex of li hanzi bytes: " + " ".join("%02x" % b for b in bb[:24]))
out.append("decoded li head: " + decoded[:12])
out.append("里 codepoint U+%04X" % ord("里"))
out.append("鍔 codepoint U+%04X" % ord("鍔"))
# 检查 FREQ_SEED 是否含 里：从 gen 脚本读
gs = open(os.path.join(HERE, "gen_pinyin_dict.py"), encoding="utf-8").read()
out.append("gen脚本含'里': " + str("里" in gs))
out.append("gen脚本含'鍔': " + str("鍔" in gs))
open(os.path.join(HERE, "diag2_out.txt"), "w", encoding="utf-8").write("\n".join(out))
print("\n".join(out))
