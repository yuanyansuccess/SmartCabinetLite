/**
 * @file FaceRecogLogDAO.cpp
 * @brief 人脸识别识别统计日志数据访问对象实现 — 全部参数化查询
 * @author 袁燕
 */
#include "FaceRecogLogDAO.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include "common/Constants.h"  // 人脸识别结果常量 SC::FACE_RESULT_*

namespace db {

/**
 * @brief 新增日志
 */
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

} // namespace db
