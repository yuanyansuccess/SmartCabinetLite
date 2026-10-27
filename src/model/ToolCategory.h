/**
 * @file ToolCategory.h
 * @brief 工具分类实体，字段与 tool_category 表对应
 * @author 袁燕
 */
#pragma once
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
