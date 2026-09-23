#pragma once
// 作者：袁燕  智能柜Qt Widget 2.0  归还管理业务控制层
// 日期：2026-06-21  功能：归还记录查询
// [V6.9 2026-06-24] 统一到db/目录namespace db
#include <QObject>
#include <QDate>
#include "model/ReturnRecord.h"
#include "db/RecordDAO.h"

class ReturnController : public QObject {
    Q_OBJECT
public:
    explicit ReturnController(QObject* parent = nullptr);
    struct PageResult { QList<ReturnRecord> list; int total = 0; int page = 1; int pageSize = 20; };

    PageResult getReturnList(int page, int pageSize, const QString& keyword = "",
                             int userId = 0, const QDate& startDate = {}, const QDate& endDate = {});

private:
    db::RecordDAO m_dao;
};
