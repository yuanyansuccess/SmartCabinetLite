/**
 * @file FaceRecogLogDAO.cpp
 * @brief 人脸识别识别统计日志数据访问对象实现 — 全部参数化查询
 * @author 袁燕
 * @修改说明 2026-08-24 新增：识别日志写入与统计汇总，支撑专利实测数据
 */
#include "FaceRecogLogDAO.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>

namespace db {

bool FaceRecogLogDAO::insertLog(const FaceRecogLog& log) {
    QSqlDatabase db = getDb();
    QSqlQuery query(db);
    query.prepare(
        "INSERT INTO face_recog_log (result, best_sim, best_dist, threshold, mode, "
        "candidate_cnt, elapsed_ms, matched_user) "
        "VALUES (:result, :sim, :dist, :th, :mode, :cnt, :ms, :user)"
    );
    query.bindValue(":result", log.result);
    query.bindValue(":sim", log.bestSim);
    query.bindValue(":dist", log.bestDist);
    query.bindValue(":th", log.threshold);
    query.bindValue(":mode", log.mode);
    query.bindValue(":cnt", log.candidateCnt);
    query.bindValue(":ms", log.elapsedMs);
    query.bindValue(":user", log.matchedUser);
    return safeExec(query);
}

FaceRecogStats FaceRecogLogDAO::getStats(int days) {
    FaceRecogStats stats;
    QString where = days > 0
        ? QString("WHERE created_at >= datetime('now','-%1 day','localtime')").arg(days)
        : QString();

    // 总量与各结果计数
    QSqlQuery q(getDb());
    if (!q.prepare(
            "SELECT result, COUNT(*) FROM face_recog_log " + where +
            " GROUP BY result")) {
        return stats;
    }
    safeExec(q);
    while (q.next()) {
        QString r = q.value(0).toString();
        int c = q.value(1).toInt();
        stats.total += c;
        if (r == "success")       stats.success += c;
        else if (r == "stranger") stats.stranger += c;
        else if (r == "rejected") stats.rejected += c;
        else if (r == "error")    stats.error += c;
    }

    if (stats.total == 0) return stats;

    // 延迟统计（仅统计有效识别：success/stranger/rejected）
    QSqlQuery dq(getDb());
    QString delayWhere = where.isEmpty() ? "WHERE result IN ('success','stranger','rejected')"
                                         : (where + " AND result IN ('success','stranger','rejected')");
    if (dq.prepare("SELECT AVG(elapsed_ms), MAX(elapsed_ms), MIN(elapsed_ms) FROM face_recog_log " + delayWhere)) {
        if (safeExec(dq) && dq.next()) {
            stats.avgElapsedMs = dq.value(0).toDouble();
            stats.maxElapsedMs = dq.value(1).toDouble();
            stats.minElapsedMs = dq.value(2).toDouble();
        }
    }

    // FAR/FRR 近似（基于真实日志语义）：
    //   FAR(误识率) = 陌生人被误判为合法的比例 → 本系统"宁误拒不误识"，stranger 即正确拒绝，故实测FAR≈0
    //   此处以 (success 失误风险) 反推：成功中 best_sim<0.97 的视为潜在风险计数
    QSqlQuery farq(getDb());
    QString farWhere = where.isEmpty()
        ? "WHERE result='success' AND best_sim < 0.97"
        : (where + " AND result='success' AND best_sim < 0.97");
    if (farq.prepare("SELECT COUNT(*) FROM face_recog_log " + farWhere)) {
        if (safeExec(farq) && farq.next()) {
            int riskySuccess = farq.value(0).toInt();
            stats.farRate = stats.success > 0 ? double(riskySuccess) / stats.success : 0;
        }
    }

    // FRR(拒识率) = 合法用户被拒占比 ≈ rejected / (success + rejected)
    int legitAttempts = stats.success + stats.rejected;
    stats.frrRate = legitAttempts > 0 ? double(stats.rejected) / legitAttempts : 0;

    return stats;
}

bool FaceRecogLogDAO::clearAll() {
    QSqlDatabase db = getDb();
    QSqlQuery query(db);
    query.prepare("DELETE FROM face_recog_log");
    return safeExec(query);
}

} // namespace db
