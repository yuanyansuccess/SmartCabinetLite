#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
verify_tool_insert.py — 灌库前校验脚本（袁总命名"circle脚本"）

【用途】
升级上位机软件、用 SQL 灌入新工具前，先跑本脚本核对 4 项约束，避免脏数据：
  1) category_id / cabinet_id / machine_group_id 必须已存在（外键）
  2) machine_group_id 不能空（机组隔离铁律，空了工具对谁都不可见/借不到）
  3) 位置不冲突：tool_position_mapping 唯一约束 (cabinet_id, layer, position)
  4) tool_code 唯一（uk_tool_code）

【输入】
  --tool-name   工具名称（必填，袁总说的"输入工具名称"）
  --tool-code   工具编号（必填，uk_tool_code 校验用）
  --category    分类名称（按名解析 category_id）
  --cabinet     柜体名称（按名解析 cabinet_id）
  --layer       层号，如 01
  --position    位号，如 01
  --machine-group 机组名称（按名解析 group_id，铁律非空）

【输出】规范化逐项 [PASS]/[FAIL]，末尾 [SUMMARY] ALL_PASS / HAS_FAIL
  --json 输出机读 JSON（便于接入升级流水线）

【连接】通过参数或环境变量传入，不硬编码密码
  pip install pymysql
  python verify_tool_insert.py --tool-name "内六角扳手" --tool-code JZ01-WLS001 \
      --category "扳手类" --cabinet "A柜" --layer 01 --position 01 --machine-group "机组1" \
      --host 127.0.0.1 --port 3306 --user root --password xxx --db smart_cabinet

依赖：pymysql
"""
import argparse
import json
import os
import sys

import pymysql


def get_conn(args):
    return pymysql.connect(
        host=args.host,
        port=args.port,
        user=args.user,
        password=args.password or os.environ.get("DB_PASS", ""),
        database=args.db,
        charset="utf8mb4",
        connect_timeout=5,
    )


def q1(conn, sql, params):
    with conn.cursor() as cur:
        cur.execute(sql, params)
        return cur.fetchone()


def resolve_id(conn, table, id_col, name_col, name):
    row = q1(conn, f"SELECT {id_col} FROM {table} WHERE {name_col}=%s LIMIT 1", (name,))
    return row[0] if row else None


def main():
    p = argparse.ArgumentParser(description="灌库前校验脚本")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=3306)
    p.add_argument("--user", default="root")
    p.add_argument("--password", default="")
    p.add_argument("--db", default="smart_cabinet")
    p.add_argument("--tool-name", required=True)
    p.add_argument("--tool-code", required=True)
    p.add_argument("--category", required=True, help="分类名称")
    p.add_argument("--cabinet", required=True, help="柜体名称")
    p.add_argument("--layer", required=True)
    p.add_argument("--position", required=True)
    p.add_argument("--machine-group", required=True, dest="machine_group", help="机组名称")
    p.add_argument("--json", action="store_true", help="输出机读 JSON")
    args = p.parse_args()

    # (检查项, 是否通过, 详情, 是否阻断)
    checks = []

    try:
        conn = get_conn(args)
    except Exception as e:  # 连不上直接失败退出
        print(f"[FAIL] 数据库连接失败: {e}")
        sys.exit(2)

    try:
        # 1) 外键存在性
        cat_id = resolve_id(conn, "tool_category", "category_id", "category_name", args.category)
        cab_id = resolve_id(conn, "tool_cabinet", "cabinet_id", "cabinet_name", args.cabinet)
        mg_id = resolve_id(conn, "machine_group", "group_id", "group_name", args.machine_group)

        checks.append(("分类存在", cat_id is not None,
                       f"category_id={cat_id} (名称'{args.category}')", True))
        checks.append(("柜体存在", cab_id is not None,
                       f"cabinet_id={cab_id} (名称'{args.cabinet}')", True))

        # 2) 机组非空 + 存在
        if mg_id is None:
            checks.append(("机组存在且非空", False,
                           f"未找到机组'{args.machine_group}'，机组隔离铁律不允许为空", True))
        else:
            checks.append(("机组存在且非空", True, f"group_id={mg_id}", True))

        # 3) 位置冲突（唯一约束 cabinet_id, layer, position）
        if cab_id is None:
            checks.append(("位置不冲突", False, "柜体未知，无法校验位置唯一约束", True))
        else:
            dup = q1(conn,
                     "SELECT mapping_id FROM tool_position_mapping "
                     "WHERE cabinet_id=%s AND layer=%s AND position=%s LIMIT 1",
                     (cab_id, args.layer, args.position))
            if dup:
                checks.append(("位置不冲突", False,
                               f"位置已占用 mapping_id={dup[0]} "
                               f"(cabinet={cab_id}, layer={args.layer}, pos={args.position})", True))
            else:
                checks.append(("位置不冲突", True,
                               f"位置空闲 (cabinet={cab_id}, layer={args.layer}, pos={args.position})", True))

        # 4) tool_code 唯一（uk_tool_code）
        code_dup = q1(conn, "SELECT tool_id FROM tool_info WHERE tool_code=%s LIMIT 1",
                      (args.tool_code,))
        if code_dup:
            checks.append(("tool_code唯一", False,
                           f"tool_code '{args.tool_code}' 已存在 tool_id={code_dup[0]}", True))
        else:
            checks.append(("tool_code唯一", True,
                           f"tool_code '{args.tool_code}' 未占用", True))

        # 友好提示：同名工具（不阻断）
        name_dup = q1(conn, "SELECT tool_id FROM tool_info WHERE tool_name=%s LIMIT 1",
                      (args.tool_name,))
        if name_dup:
            checks.append(("工具名提示(不阻断)", False,
                           f"同名工具已存在 tool_id={name_dup[0]}，请确认是否重复录入", False))
        else:
            checks.append(("工具名提示(不阻断)", True, "无同名工具", False))
    finally:
        conn.close()

    blocking = [(n, ok, d) for (n, ok, d, b) in checks if b]
    all_pass = all(ok for (_, ok, _) in blocking)

    if args.json:
        out = [{"check": n, "pass": ok, "detail": d, "blocking": b}
               for (n, ok, d, b) in checks]
        print(json.dumps({"checks": out, "all_pass": all_pass},
                         ensure_ascii=False, indent=2))
    else:
        for n, ok, d, _ in checks:
            print(f"[{'PASS' if ok else 'FAIL'}] {n}: {d}")
        print(f"[SUMMARY] {'ALL_PASS' if all_pass else 'HAS_FAIL'}")

    sys.exit(0 if all_pass else 1)


if __name__ == "__main__":
    main()
