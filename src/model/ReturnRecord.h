/**
 * @file ReturnRecord.h
 * @brief 归还记录实体，取自 tool_borrow_record 中已归还的记录
 * @author 袁燕
 */
#pragma once
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
