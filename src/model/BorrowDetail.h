#pragma once
// 作者：袁燕  智能柜Qt Widget 2.0  借用明细实体(含工具)
// 日期：2026-06-21  用于借用列表详细展示
#include <QString>
#include <QDateTime>

struct BorrowDetail {
    int     borrowId     = 0;
    int     detailId     = 0;
    int     toolId       = 0;
    int     quantity     = 1;
    QString remark;

    // 关联
    QString toolCode;
    QString toolName;
    QString toolSpec;
    QString categoryName;
    QString cabinetName;
    QString layer;
    QString position;
};
