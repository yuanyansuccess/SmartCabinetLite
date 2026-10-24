"""为自动化测试写入一个已知口令的测试账号（幂等）。

算法严格对齐 AuthService::hashPassword：SHA256(salt + password) 的十六进制串。
默认复用已存在的 admin 工号 001，避免污染真实业务数据；如需独立账号，改 TEST_USER 即可。
仅用于本机自测，生产环境请勿保留。
"""
import hashlib
import pymysql
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CFG = json.loads((ROOT / "tools/ui_test/config.json").read_text(encoding="utf-8"))

TEST_USER = CFG["test_user"]
WORK_NO = TEST_USER["work_no"]
PASSWORD = TEST_USER["password"]
SALT = "a1b2c3d4e5f60718"  # 固定盐，便于复现


def sha256_salt_pwd(salt: str, pwd: str) -> str:
    return hashlib.sha256((salt + pwd).encode("utf-8")).hexdigest()


def main():
    db = CFG["host_db"] if "host_db" in CFG else None
    conn = pymysql.connect(
        host="127.0.0.1", port=3306, user="root", password="root",
        db="smart_cabinet", charset="utf8mb4",
    )
    try:
        cur = conn.cursor()
        h = sha256_salt_pwd(SALT, PASSWORD)
        # 若工号不存在则插入；存在则更新口令，使其变为已知值
        cur.execute(
            "SELECT user_id FROM sys_user WHERE work_no=%s", (WORK_NO,)
        )
        row = cur.fetchone()
        if row is None:
            cur.execute(
                "INSERT INTO sys_user (username,password_hash,password_salt,real_name,work_no,dept_id,role,status,must_change_pwd,created_at,updated_at) "
                "VALUES (%s,%s,%s,'自动测试员',%s,1,'admin','active',0,NOW(),NOW())",
                ("test" + WORK_NO, h, SALT, WORK_NO),
            )
            print(f"已新建测试账号 work_no={WORK_NO}")
        else:
            cur.execute(
                "UPDATE sys_user SET password_hash=%s, password_salt=%s, status='active', must_change_pwd=0 WHERE work_no=%s",
                (h, SALT, WORK_NO),
            )
            print(f"已更新测试账号 work_no={WORK_NO} 的口令为配置值")
        conn.commit()
        print("OK: 测试账号口令已就绪，可用于自动化登录。")
    finally:
        conn.close()


if __name__ == "__main__":
    main()
