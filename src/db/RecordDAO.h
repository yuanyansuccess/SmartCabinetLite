/**
 * @file RecordDAO.h
 * @brief 借用记录数据访问对象 — QJsonObject API + 实体类API + 操作日志查询
 * @author 袁燕
 */
#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QDateTime>
#include <QDate>
#include "model/BorrowRecord.h"
#include "BaseDAO.h"

namespace db {

class RecordDAO : public BaseDAO {
public:
    RecordDAO() = default;

    // ═══════════════════════════════════════════════
    // QJsonObject API（Service层使用）
    // ═══════════════════════════════════════════════
    // 写入一条借用记录，返回 record_id（失败返回-1）
    int         insert(const QJsonObject& record);
    // 按记录ID查询借用详情
    QJsonObject findById(int recordId);
    // 多条件分页查询借用记录
    QJsonObject findAll(int userId, int toolId, const QString& status,
                        const QString& startDate, const QString& endDate,
                        int page, int pageSize);
    // 归还确认：写入归还时间/状态/操作人，返回是否成功
    bool        completeReturn(int recordId, const QString& returnTime,
                               const QString& condition, int operatorId, const QString& remark);
    // 查询某工具的最近N条借用记录（用于工具详情对话框展示）
    QJsonArray  findByToolId(int toolId, int limit = 5);
    // 按位置映射ID查询借用记录（工具详情按位置过滤）
    QJsonArray  findByMappingId(int mappingId, int limit = 20);
    // 查询操作日志（sys_operation_log）：入库/出库记录
    QJsonObject findLastOperationLog(const QString& toolCode, const QString& operationType);
    // 查询指定工具的操作日志清单
    QJsonArray  findOperationLogs(const QString& toolCode, const QString& operationType,
                                  const QString& positionKeyword, int limit = 10);
    // 按用户查询借用记录（普通用户借用明细页/功能主页用）
    // 入参：userId用户ID, limit返回条数上限
    // 返回：QList<BorrowRecord> 该用户的借用流水（按借用时间倒序）
    QList<BorrowRecord> findBorrowsByUser(int userId, int limit);
    // 通过借用记录ID查询关联工具信息（tool_id和tool_code）
    QJsonObject findToolInfoByRecordId(int recordId);
    // 写入一条操作日志，返回log_id（失败返回-1）
    int         insertOperationLog(int userId, const QString& operationType,
                                  const QString& targetType, const QString& targetId,
                                  const QString& content, const QString& ipAddress = "127.0.0.1");
    // 分页查询指定类型的操作日志（含操作人信息），返回 {list, total}
    QJsonObject findOperationLogsByType(const QString& operationType, int page, int pageSize);
    // 分页查询全部类型的操作日志（含操作人信息），返回 {list, total}
    QJsonObject findOperationLogsAll(int page, int pageSize);
    // 分页查询入库类操作日志（operation_type='checkin'），返回 {list, total}
    QJsonObject findCheckinLogs(int page, int pageSize);
    // 台账统计（LedgerStatsPage数据源）
    // 返回：QJsonObject含totalBorrows/totalReturns/currentBorrowed/overdueCount/activeUsers
    //      + categoryStats（按品类件数）+ departmentStats（按部门）
    QJsonObject getLedgerStats();

    static BorrowRecord borrowFromQuery(const QSqlQuery& q);
};

} // namespace db
