/**
 * @file AlertDAO.h
 * @brief 告警数据访问对象，提供告警记录的查询、新增、类型查询与统计接口
 * @author 袁燕
 */
#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QDate>
#include "model/AlertLog.h"
#include "BaseDAO.h"

namespace db {

class AlertDAO : public BaseDAO {
public:
    AlertDAO() = default;

    // 分页查询告警列表（AlertLogsPage数据源，含tool_cabinet JOIN，显示所有状态含已忽略）
    // level 兼容显示值（严重/一般/提示）与库值；返回 {list, total}
    QJsonObject findAllPaged(const QString& type, const QString& level,
                             const QString& keyword, int page, int pageSize);
    // 获取所有启用的告警类型（用于前端筛选下拉）
    QJsonArray  getAlertTypes();
    // 新增告警记录实体
    int         insertAlert(const AlertLog& alert);
    // 按type_code查type_id，-1=未找到
    int         findTypeIdByCode(const QString& typeCode);
};

} // namespace db
