/**
 * @file FaceRecognitionService.cpp
 * @brief 人脸识别服务实现 - 余弦相似度+欧氏距离双验证
 * @author 袁燕
 */
#include "FaceRecognitionService.h"
#include "db/UserDAO.h"
#include "db/FaceRecogLogDAO.h"
#include <QtMath>
#include <QDebug>
#include <QElapsedTimer>
#include <algorithm>

// [2026-06-21] 修复LNK2005：db/层已包裹namespace db
using db::UserDAO;
using db::FaceRecogLogDAO;
using db::FaceRecogLog;

FaceRecognitionService::FaceRecognitionService(QObject* parent) : QObject(parent) {}

QVector<double> FaceRecognitionService::parseDescriptor(const QString& str) {
    QVector<double> result;
    QStringList parts = str.split(",", Qt::SkipEmptyParts);
    for (const auto& p : parts) {
        bool ok = false;
        double v = p.trimmed().toDouble(&ok);
        if (ok) result.append(v);
    }
    return result;
}

void FaceRecognitionService::normalizeL2(QVector<double>& v) {
    double sum = 0;
    for (double x : v) sum += x * x;
    double len = qSqrt(sum);
    if (len > 1e-10) {
        for (auto& x : v) x /= len;
    }
}

double FaceRecognitionService::cosineSimilarity(const QVector<double>& a, const QVector<double>& b) {
    if (a.size() != b.size() || a.isEmpty()) return 0;
    QVector<double> an = a, bn = b;
    normalizeL2(an);
    normalizeL2(bn);
    double dot = 0;
    for (int i = 0; i < an.size(); ++i) dot += an[i] * bn[i];
    // [V2.02 2026-06-28] 移除(dot+1)/2映射 → 直接返回原始余弦相似度
    //   原映射把不同人sim从0.3抬高到0.65，超过rejectThreshold被纳入候选
    //   原始值：同一个人>0.9，不同人<0.5，辨识力强
    //   作者：袁燕 — 修复陌生人泛化误识P0致命Bug
    return dot;
}

double FaceRecognitionService::euclideanDistance(const QVector<double>& a, const QVector<double>& b) {
    if (a.size() != b.size()) return 1e10;
    QVector<double> an = a, bn = b;
    normalizeL2(an);
    normalizeL2(bn);
    double sum = 0;
    for (int i = 0; i < an.size(); ++i) {
        double d = an[i] - bn[i];
        sum += d * d;
    }
    return qSqrt(sum);
}

FaceRecognitionService::FaceMatchResult FaceRecognitionService::matchFace(
    const QString& faceDescriptor, double threshold, double highConfidence,
    double rejectThreshold, double marginThreshold, double maxEuclideanDist) {

    FaceMatchResult result;
    result.success = false;
    result.isStranger = false;
    result.userId = 0;
    result.similarity = 0;

    // [2026-08-24 袁燕] 识别计时 + 统计日志埋点（支撑专利实测数据）
    QElapsedTimer recogTimer;
    recogTimer.start();
    QJsonArray enrolledFaces = getAllEnrolledFaces();
    const int candidateCnt = enrolledFaces.size();
    const QString mode = "multi"; // 后续按candidates数量判定单/多人脸

    QVector<double> inputVec = parseDescriptor(faceDescriptor);
    if (inputVec.size() < 64) {
        result.message = QStringLiteral("人脸特征数据异常(维度不足64)");
        FaceRecogLogDAO().insertLog({ "error", 0, 1, threshold, mode, candidateCnt,
                                      int(recogTimer.elapsed()), "" });
        return result;
    }

    // 获取所有已录入人脸
    QJsonArray enrolled = enrolledFaces;
    if (enrolled.isEmpty()) {
        result.message = QStringLiteral("系统中没有人脸数据，请先录入人脸");
        FaceRecogLogDAO().insertLog({ "error", 0, 1, threshold, mode, candidateCnt,
                                      int(recogTimer.elapsed()), "" });
        return result;
    }

    // [V2.02 2026-06-28] 遍历所有已录入人脸，计算余弦相似度+欧氏距离
    //   移除提前退出逻辑：陌生人sim碰巧高时直接return success=true是致命Bug
    //   所有候选必须遍历完再综合判断
    //   作者：袁燕 — 修复陌生人泛化误识P0致命Bug
    struct Candidate {
        int userId;
        QString username;
        QString realName;
        QString workNo;
        QString department;
        double similarity;       // 余弦相似度
        double euclideanDist;    // 欧氏距离（越小越相似）
    };
    QVector<Candidate> candidates;
    candidates.reserve(enrolled.size());

    // [2026-08-24 袁燕] 统计埋点：追踪全局最佳候选（即便低于rejectThreshold），供stranger日志记录最高相似度
    double globalBestSim = 0;
    double globalBestDist = 1;

    for (const auto& e : enrolled) {
        QJsonObject obj = e.toObject();
        QVector<double> dbVec = parseDescriptor(obj["faceFeature"].toString());
        if (dbVec.size() < 64) continue;
        double sim = cosineSimilarity(inputVec, dbVec);
        double dist = euclideanDistance(inputVec, dbVec);
        // [V2.02] 只记录超过rejectThreshold的候选（原始余弦相似度，不同人<0.5）
        if (sim > rejectThreshold) {
            candidates.append({
                obj["userId"].toInt(),
                obj["username"].toString(),
                obj["realName"].toString(),
                obj["workNo"].toString(),
                obj["department"].toString(),
                sim,
                dist
            });
        }
        if (sim > globalBestSim) { globalBestSim = sim; globalBestDist = dist; }
    }

    if (candidates.isEmpty()) {
        result.isStranger = true;
        result.message = QStringLiteral("检测到陌生人，未在系统中注册");
        FaceRecogLogDAO().insertLog({ "stranger", globalBestSim, globalBestDist, threshold,
                                      "multi", candidateCnt, int(recogTimer.elapsed()), "" });
        return result;
    }

    // 按相似度降序排序
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.similarity > b.similarity; });

    auto& best = candidates[0];
    double secondSim = candidates.size() > 1 ? candidates[1].similarity : 0;

    // [V2.17 2026-07-06 袁总指令] 单人脸严格策略：系统仅1人脸时必须95%以上
    //   根因：只有1个候选时无法做第二名差距检查(marginThreshold)，陌生人碰巧0.94+就可能通过
    //   修复：单人脸模式下使用singleFaceThreshold=0.95，多人脸模式仍可用margin降至0.94
    //   设计理念：宁误拒不误识——陌生人绝对不能登录系统
    //   作者：袁燕
    const double singleFaceThreshold = 0.97;  // 单人脸最低门槛（袁总确认95%以上才通过）
    bool cosOk = false;

    if (candidates.size() == 1) {
        // [V2.17] 单人脸模式：没有第二名候选，必须达到singleFaceThreshold(0.95)才通过
        //   杜绝陌生人误识：只有1个人脸时，任何低于95%的匹配一律拒绝
        cosOk = (best.similarity >= singleFaceThreshold);
        qDebug() << "[FaceRecog] 单人脸模式: best.sim=" << best.similarity
                 << "threshold=" << singleFaceThreshold
                 << "cosOk=" << cosOk;
    } else if (best.similarity >= highConfidence) {
        // 多人脸高置信度直接通过（>=0.95）
        cosOk = true;
    } else if (candidates.size() >= 2 &&
               best.similarity >= threshold &&
               (best.similarity - secondSim) > marginThreshold) {
        // 多人脸中置信度：第二名差距足够大才通过
        cosOk = true;
    }
    // 欧氏距离验证（第二道防线）
    bool distOk = (best.euclideanDist <= maxEuclideanDist);

    // [2026-08-24 袁燕] 统计埋点：记录本次生效的判定模式（单/多人脸）与最终阈值
    const QString effMode = (candidates.size() == 1) ? "single" : "multi";
    const double effThreshold = (candidates.size() == 1) ? singleFaceThreshold : threshold;

    if (cosOk && distOk) {
        result.success = true;
        FaceRecogLogDAO().insertLog({ "success", best.similarity, best.euclideanDist,
                                      effThreshold, effMode, candidateCnt,
                                      int(recogTimer.elapsed()), best.workNo });
    } else {
        // [V2.02] 未通过双验证 → 一律判定为陌生人（宁误拒不误识）
        result.isStranger = true;
        result.message = QStringLiteral("人脸验证失败，相似度: %.1f%% 距离: %.3f (未通过双验证)")
                          .arg(best.similarity * 100).arg(best.euclideanDist);
        FaceRecogLogDAO().insertLog({ "rejected", best.similarity, best.euclideanDist,
                                      effThreshold, effMode, candidateCnt,
                                      int(recogTimer.elapsed()), "" });
        return result;
    }

    result.userId = best.userId;
    result.username = best.username;
    result.realName = best.realName;
    result.workNo = best.workNo;
    result.department = best.department;
    result.similarity = best.similarity;
    result.message = QStringLiteral("识别成功，相似度: %.1f%% 距离: %.3f").arg(best.similarity * 100).arg(best.euclideanDist);

    // 检查用户状态
    UserDAO dao;
    QJsonObject user = dao.findById(best.userId);
    if (user["status"].toString() != "active") {
        result.success = false;
        result.message = QStringLiteral("用户已被禁用或锁定");
    }
    // [V6.3] 从数据库读取真实角色，防止doLocalFaceVerify硬编码"user"导致管理员降级
    result.role = user["role"].toString();
    // [V6.3] 安全回填：getAllFaceFeatures可能缺少workNo/department，从findById补全
    if (result.workNo.isEmpty() && !user["workNo"].toString().isEmpty())
        result.workNo = user["workNo"].toString();
    if (result.department.isEmpty() && !user["department"].toString().isEmpty())
        result.department = user["department"].toString();

    return result;
}

bool FaceRecognitionService::enrollFace(int userId, const QString& faceDescriptor) {
    UserDAO dao;
    return dao.updateFace(userId, faceDescriptor);
}

bool FaceRecognitionService::deleteFace(int userId) {
    UserDAO dao;
    return dao.deleteFace(userId);
}

bool FaceRecognitionService::hasFaceEnrolled(int userId) {
    UserDAO dao;
    QJsonObject u = dao.findById(userId);
    return !u["faceFeature"].toString().isEmpty();
}

QJsonArray FaceRecognitionService::getAllEnrolledFaces() {
    UserDAO dao;
    return dao.getAllFaceFeatures();
}
