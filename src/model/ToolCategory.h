#pragma once
// 作者：袁燕  智能柜Qt Widget 2.0  工具分类实体
// 日期：2026-06-21 映射表：tool_category
#include <QString>
#include <QDateTime>

struct ToolCategory {
    int     categoryId   = 0;
    QString categoryName;
    int     parentId     = 0;
    int     sortOrder    = 0;
    QString icon;
    QDateTime createdAt;
};
