/**
 * @file FaceRecogLogDAO.h
 * @brief 人脸识别识别统计日志数据访问对象 — db/目录统一namespace db
 * @author 袁燕
 * @修改说明 2026-08-24 新增：记录每次人脸识别尝试，用于统计误识率(FAR)/拒识率(FRR)/识别延迟，
 *          支撑专利交底书实测数据回填。数据库操作统一在db层（遵守袁总铁律）。
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

/// 识别统计汇总（用于专利FAR/FRR/延迟实测）
struct FaceRecogStats {
    int total = 0;         // 总识别次数
    int success = 0;       // 通过次数（合法用户）
    int stranger = 0;      // 判陌生人次数
    int rejected = 0;      // 双验证失败次数
    int error = 0;         // 异常次数
    double farRate = 0;    // 误识率：stranger中"本应通过"的占比（此处以stranger占总比近似，详见getStats注释）
    double frrRate = 0;    // 拒识率：合法用户被拒占比（近似 rejected/总合法尝试）
    double avgElapsedMs = 0; // 平均识别延迟
    double maxElapsedMs = 0; // 最大识别延迟
    double minElapsedMs = 0; // 最小识别延迟
};

class FaceRecogLogDAO : public BaseDAO {
public:
    FaceRecogLogDAO() = default;

    /// 写入一条识别日志
    bool insertLog(const FaceRecogLog& log);

    /// 统计汇总（默认统计全部历史；可传 days 限定近N天）
    FaceRecogStats getStats(int days = 0);

    /// 清空日志（仅测试/重置用，不影响业务表）
    bool clearAll();
};

} // namespace db
