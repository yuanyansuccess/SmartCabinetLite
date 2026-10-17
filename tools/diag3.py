#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import re, os
HERE = os.path.dirname(os.path.abspath(__file__))
h = os.path.join(HERE, "..", "src", "utils", "PinyinDictData.h")
b = open(h, "rb").read()
idx = b.find(b'"li"')
seg = b[idx:idx+70]
print("RAW BYTES (from file):")
print(" ".join("%02x" % x for x in seg))
# 看这串字节作为 UTF-8 解码是什么
print("AS UTF-8:", seg.decode("utf-8", "replace")[:20])
# 查找 li 条目在文件里到底是转义还是原始中文：看是否含反斜杠字节 0x5c
print("contains backslash 0x5c in segment:", b'\x5c' in seg)
