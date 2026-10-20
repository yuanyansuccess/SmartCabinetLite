/**
 * @file FaceRecogLogDAO.h
 * @brief 人脸识别识别统计日志数据访问对象 — db/目录统一namespace db
 * @author 袁燕
 *          数据库操作统一在db层（遵守分层规范）。
 */
#pragma once
#include <QJsonObject>
#include <QString>
#include "BaseDAO.h"

namespace db {

/// 单次人脸识别日志入参
struct FaceRecogLog {
    QString result;        // success / stranger / rejected / error
    double  bestSim = 0;   // 最佳候选余弦相似度
    double  bestDist = 1;  // 最佳候选欧氏距离
    double  threshold = 0; // 本次生效判定阈值
    QString mode;          // single / multi
    int     candidateCnt = 0;
    int     elapsedMs = 0;
    QString matchedUser;   // 命中工号（stranger/error为空）
};

class FaceRecogLogDAO : public BaseDAO {
public:
    FaceRecogLogDAO() = default;

    /// 写入一条识别日志
    bool insertLog(const FaceRecogLog& log);
};

} // namespace db
