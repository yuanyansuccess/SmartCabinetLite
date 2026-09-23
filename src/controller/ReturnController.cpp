// 作者：袁燕  智能柜Qt Widget 2.0  ReturnController实现
// 日期：2026-06-21
#include "ReturnController.h"

ReturnController::ReturnController(QObject* parent) : QObject(parent) {}

ReturnController::PageResult ReturnController::getReturnList(
    int page, int pageSize, const QString& keyword,
    int userId, const QDate& startDate, const QDate& endDate) {
    PageResult r;
    r.page = page; r.pageSize = pageSize;
    r.list = m_dao.findReturns(page, pageSize, keyword, userId, startDate, endDate);
    return r;
}
