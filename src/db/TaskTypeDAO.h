/**
 * @file TaskTypeDAO.h
 * @brief 任务类型数据访问对象 — db/目录统一namespace db，合并实体类API
 * @author 袁燕
 * @修改说明 V6.9 2026-06-24 合并dao/TaskTypeDAO的实体类API到此文件，统一namespace db管理
 */
#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QList>
#include <QSqlDatabase>
#include <QSqlQuery>
#include "model/TaskType.h"
#include "BaseDAO.h"

namespace db {

class TaskTypeDAO : public BaseDAO {
public:
    TaskTypeDAO() = default;

    // QJsonObject API（Service层使用）
    QJsonArray findAllActive();
    QJsonArray findRecommendedToolsByTypes(const QList<int>& typeIds, int machineGroupId = 0);
    // [2026-09-23] 按任务类型查询本机组在库工具（位置维度，每个在库位置一行，开柜页只读展示用）
    // 返回字段：toolName/toolCode/cabinetName/layer/position
    QJsonArray findInStockToolsByType(int typeId, int machineGroupId = 0);

    // 实体类API（Controller层使用）— 从dao/TaskTypeDAO合并
    QList<TaskType> findAll();

    static TaskType fromQuery(const QSqlQuery& q);
};

} // namespace db
