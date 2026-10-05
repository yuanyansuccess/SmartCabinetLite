/**
 * @file ToolCategory.h
 * @brief 工具分类实体，字段对齐 tool_category
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
