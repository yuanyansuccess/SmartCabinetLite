#pragma once
// 作者：袁燕  智能柜Qt Widget 2.0  借用记录实体
// 日期：2026-06-21 [V1.00.8.4] 修复：映射表tool_borrow_record，字段对齐数据库schema
#include <QString>
#include <QDateTime>

struct BorrowRecord {
    int     borrowId     = 0;  // 对应tool_borrow_record.record_id
    QString flowNo;            // 流水号
    int     userId       = 0;
    int     toolId       = 0;
    int     quantity     = 1;  // 对应tool_borrow_record.borrow_qty
    QDateTime borrowTime;
    QDateTime expectReturnTime;  // 对应tool_borrow_record.expected_return_time
    QDateTime actualReturnTime;
    QString status;            // borrowing/returned/overdue
    QString borrowReason;
    QString remark;
    QDateTime createdAt;

    // 关联展示字段
    QString userName;
    QString realName;
    QString workNo;
    QString department;
    QString toolCode;
    QString toolName;
    QString toolSpec;
    QString cabinetName;
};
