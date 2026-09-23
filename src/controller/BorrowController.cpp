// 作者：袁燕  智能柜Qt Widget 2.0  BorrowController实现
// 日期：2026-06-21
#include "BorrowController.h"

BorrowController::BorrowController(QObject* parent) : QObject(parent) {}

BorrowController::PageResult BorrowController::getBorrowList(
    int page, int pageSize, const QString& keyword, const QString& status,
    int userId, const QDate& startDate, const QDate& endDate) {
    PageResult r;
    r.page = page; r.pageSize = pageSize;
    r.total = m_dao.borrowCount(keyword, status, userId, startDate, endDate);
    r.list  = m_dao.findBorrows(page, pageSize, keyword, status, userId, startDate, endDate);
    return r;
}

BorrowRecord BorrowController::getBorrowById(int borrowId) { return m_dao.findBorrowById(borrowId); }
QList<BorrowRecord> BorrowController::getUserRecords(int userId, int limit) { return m_dao.findBorrowsByUser(userId, limit); }
int BorrowController::activeBorrowCount() { return m_dao.activeBorrowCount(); }
int BorrowController::overdueCount() { return m_dao.overdueCount(); }
bool BorrowController::hasOverdue(int userId) { return m_dao.hasOverdue(userId); }
