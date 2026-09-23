#pragma once
// 作者：袁燕  智能柜Qt Widget 2.0  归还记录实体
// 日期：2026-06-21  [V1.00.8.4] 修复：映射自tool_borrow_record中status='returned'的记录
#include <QString>
#include <QDateTime>

struct ReturnRecord {
    int     returnId     = 0;  // 复用tool_borrow_record.record_id
    int     borrowId     = 0;  // 同record_id
    int     userId       = 0;
    int     toolId       = 0;
    int     quantity     = 1;  // 对应borrow_qty
    QDateTime returnTime;      // 对应actual_return_time
    QString condition;         // good/damaged/lost（从remark解析）
    QString remark;
    QDateTime createdAt;

    QString flowNo;
    QString userName;
    QString realName;
    QString toolCode;
    QString toolName;
    QString borrowTime;       // 关联借出时间(展示用)
};
