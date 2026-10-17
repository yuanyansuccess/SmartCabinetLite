#!/usr/bin/env python3
# -*- coding: utf-8 -*-
from pypinyin import pinyin, Style

def norm_py(p):
    return p.lower().replace("\u00fc", "v")

for ph in ["中国", "北京", "姓名", "公司", "张李", "王赵", "工具", "柜子"]:
    try:
        res = pinyin(ph, style=Style.NORMAL, heteronym=False)
        parts = [norm_py(r[0]) for r in res if r]
        print(repr(ph), res, "->", "".join(parts))
    except Exception as e:
        print(repr(ph), "ERR", repr(e))

# 单字多音
print("行", pinyin("行", style=Style.NORMAL, heteronym=True))
print("里", pinyin("里", style=Style.NORMAL, heteronym=False))
