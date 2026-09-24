#pragma once
// 作者：袁燕  智能柜Qt Widget 2.0  借用管理业务控制层
// 日期：2026-06-21 功能：借用记录查询、统计
// [V6.9 2026-06-24] 统一到db/目录namespace db
#include <QObject>
#include <QDate>
#include "model/BorrowRecord.h"
#include "db/RecordDAO.h"

class BorrowController : public QObject {
    Q_OBJECT
public:
    explicit BorrowController(QObject* parent = nullptr);

    struct PageResult { QList<BorrowRecord> list; int total = 0; int page = 1; int pageSize = 20; };

    PageResult   getBorrowList(int page, int pageSize, const QString& keyword = "",
                               const QString& status = "", int userId = 0,
                               const QDate& startDate = {}, const QDate& endDate = {});
    BorrowRecord getBorrowById(int borrowId);
    QList<BorrowRecord> getUserRecords(int userId, int limit = 50);
    int          activeBorrowCount();
    int          overdueCount();
    bool         hasOverdue(int userId);

private:
    db::RecordDAO m_dao;
};
