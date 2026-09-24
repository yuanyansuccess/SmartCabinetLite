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
#include "model/ReturnRecord.h"
#include "BaseDAO.h"

namespace db {

class RecordDAO : public BaseDAO {
public:
    RecordDAO() = default;

    // ═══════════════════════════════════════════════
    // QJsonObject API（Service层使用）
    // ═══════════════════════════════════════════════
    int         insert(const QJsonObject& record);
    QJsonObject findById(int recordId);
    QJsonObject findAll(int userId, int toolId, const QString& status,
                        const QString& startDate, const QString& endDate,
                        int page, int pageSize);
    bool        completeReturn(int recordId, const QString& returnTime,
                               const QString& condition, int operatorId, const QString& remark);
    // 查询某工具的最近N条借用记录（用于工具详情对话框展示）
    QJsonArray  findByToolId(int toolId, int limit = 5);
    // 按位置映射ID查询借用记录（工具详情按位置过滤）
    QJsonArray  findByMappingId(int mappingId, int limit = 20);
    // 查询操作日志（sys_operation_log）：入库/出库记录
    QJsonObject findLastOperationLog(const QString& toolCode, const QString& operationType);
    QJsonArray  findOperationLogs(const QString& toolCode, const QString& operationType,
                                  const QString& positionKeyword, int limit = 10);
    // 通过借用记录ID查询关联工具信息（tool_id和tool_code）
    QJsonObject findToolInfoByRecordId(int recordId);
    // 操作日志通用方法（sys_operation_log）
    // 写入一条操作日志，返回log_id（失败返回-1）
    int         insertOperationLog(int userId, const QString& operationType,
                                  const QString& targetType, const QString& targetId,
                                  const QString& content, const QString& ipAddress = "127.0.0.1");
    // 分页查询指定类型的操作日志（含操作人信息），返回 {list, total}
    QJsonObject findOperationLogsByType(const QString& operationType, int page, int pageSize);
    // 统计指定类型的操作日志数量
    int         countOperationLogsByType(const QString& operationType);
    // 入库记录查询（含位置JOIN信息）— 入库记录Tab专用
    // 返回：{list: [{createdAt,content,targetId,realName,workNo,
    // tiCabId,tiCabName,tiLayer,tiPos,mpmCabId,mpmCabName,mpmLayer,mpmPos}], total}
    QJsonObject findCheckinLogs(int page, int pageSize);
    // 统计某机组下未归还(borrowing/overdue)的借用记录数
    // 入参：machineGroupId 机组ID
    // 返回：该机组下所有未归还的借用记录数（关联tool_info.machine_group_id）
    int         countActiveByMachineGroup(int machineGroupId);
    // 用户首页统计：今日借用/待归还/本月借用
    QJsonObject getUserStats(int userId);
    // 最近借用活动（Dashboard动态日志）
    QJsonArray  findRecentActivity(int limit);
    // 按用户ID查询借用记录
    QJsonArray  findByUserId(int userId, int limit);

    // ═══════════════════════════════════════════════
    // 实体类API（Controller层使用）— 从dao/RecordDAO合并
    // ═══════════════════════════════════════════════
    int                insertBorrow(const BorrowRecord& r);
    bool               completeBorrowReturn(int borrowId, const QDateTime& returnTime,
                                            const QString& condition, const QString& remark);
    QList<BorrowRecord> findBorrows(int page = 1, int pageSize = 20,
                                    const QString& keyword = "", const QString& status = "",
                                    int userId = 0, const QDate& startDate = {}, const QDate& endDate = {});
    int                borrowCount(const QString& keyword = "", const QString& status = "", int userId = 0,
                                   const QDate& startDate = {}, const QDate& endDate = {});
    BorrowRecord       findBorrowById(int borrowId);
    QList<BorrowRecord> findBorrowsByUser(int userId, int limit = 50);
    bool               hasOverdue(int userId);

    // 归还记录
    QList<ReturnRecord> findReturns(int page = 1, int pageSize = 20,
                                    const QString& keyword = "", int userId = 0,
                                    const QDate& startDate = {}, const QDate& endDate = {});

    // 统计
    int activeBorrowCount();
    int overdueCount();

    static BorrowRecord borrowFromQuery(const QSqlQuery& q);
    static ReturnRecord returnFromQuery(const QSqlQuery& q);
};

} // namespace db
