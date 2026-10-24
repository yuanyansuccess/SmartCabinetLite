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
 * @brief 新增一条人脸识别日志
 * @param log 待写入的日志对象（含用户ID/相似度/结果等）
 * @return true=写入成功
 */
bool FaceRecogLogDAO::insertLog(const FaceRecogLog& log) {
    QSqlDatabase db = getDb();

    // 向前兼容：补齐第二名/质量回顾字段。列已存在时 ALTER 报错可忽略，
    // 旧库/旧备份导入的表首次写入会自动加列，不破坏既有数据。
    static bool schemaReady = false;
    if (!schemaReady) {
        QSqlQuery ensure(db);
        ensure.exec("ALTER TABLE face_recog_log ADD COLUMN second_sim double DEFAULT 0");
        ensure.exec("ALTER TABLE face_recog_log ADD COLUMN second_user varchar(64) DEFAULT ''");
        ensure.exec("ALTER TABLE face_recog_log ADD COLUMN quality double DEFAULT 0");
        schemaReady = true;
    }

    QSqlQuery query(db);
    query.prepare(
        "INSERT INTO face_recog_log (result, best_sim, best_dist, threshold, mode, "
        "candidate_cnt, elapsed_ms, matched_user, second_sim, second_user, quality) "
        "VALUES (:result, :sim, :dist, :th, :mode, :cnt, :ms, :user, :ssim, :suser, :q)"
    );
    query.bindValue(":result", log.result);
    query.bindValue(":sim", log.bestSim);
    query.bindValue(":dist", log.bestDist);
    query.bindValue(":th", log.threshold);
    query.bindValue(":mode", log.mode);
    query.bindValue(":cnt", log.candidateCnt);
    query.bindValue(":ms", log.elapsedMs);
    query.bindValue(":user", log.matchedUser);
    query.bindValue(":ssim", log.secondSim);
    query.bindValue(":suser", log.secondUser);
    query.bindValue(":q", log.quality);
    return safeExec(query);
}

} // namespace db
