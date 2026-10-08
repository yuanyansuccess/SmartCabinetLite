/**
 * @file ConfigDAO.h
 * @brief 系统配置数据访问对象（system_config 表）— db/目录唯一读写配置表的入口
 * @author 袁燕
 */
#pragma once
#include <QJsonObject>
#include <QString>
#include "BaseDAO.h"

namespace db {

class ConfigDAO : public BaseDAO {
public:
    ConfigDAO() = default;

    /// 加载全部配置（key→value 对象）；连接无效或查询失败返回空对象
    QJsonObject loadAll();
    /// 批量保存配置（事务内逐条 REPLACE，任一条失败整体回滚）
    bool        saveBatch(const QJsonObject& config);
    /// 读取单个配置项，不存在时返回 defaultValue
    QString     value(const QString& key, const QString& defaultValue = QString());
};

} // namespace db
