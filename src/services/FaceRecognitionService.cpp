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
#include "common/Constants.h"

// 修复LNK2005：db/层已包裹namespace db
using db::UserDAO;
using db::FaceRecogLogDAO;
using db::FaceRecogLog;

FaceRecognitionService::FaceRecognitionService(QObject* parent) : QObject(parent) {}

/**
 * @brief 解析人脸特征串
 * @param str 逗号分隔的特征值文本
 * @return 特征向量；解析失败时返回空向量
 */
QVector<double> FaceRecognitionService::parseDescriptor(const QString& str) {
    QVector<double> result;
    QStringList parts = str.split(",", Qt::SkipEmptyParts);
    for (const auto& p : parts) {
        bool ok = false;
        double dimValue = p.trimmed().toDouble(&ok);
        if (ok) result.append(dimValue);
    }
    return result;
}

/**
 * @brief 对特征向量做L2归一化
 * @param v 待归一化的向量（原地修改）
 */
void FaceRecognitionService::normalizeL2(QVector<double>& v) {
    double sum = 0;
    for (double x : v) sum += x * x;
    double len = qSqrt(sum);
    if (len > 1e-10) {
        for (auto& x : v) x /= len;
    }
}

/**
 * @brief 计算两个特征向量的余弦相似度
 * @param a 特征向量A
 * @param b 特征向量B
 * @return 相似度，范围0~1；维度不一致或为空时返回0
 */
double FaceRecognitionService::cosineSimilarity(const QVector<double>& a, const QVector<double>& b) {
    if (a.size() != b.size() || a.isEmpty()) return 0;
    QVector<double> an = a, bn = b;
    normalizeL2(an);
    normalizeL2(bn);
    double dot = 0;
    for (int i = 0; i < an.size(); ++i) dot += an[i] * bn[i];
    // 移除(dot+1)/2映射 → 直接返回原始余弦相似度
    // 原映射把不同人sim从0.3抬高到0.65，超过rejectThreshold被纳入候选
    // 原始值：同一个人>0.9，不同人<0.5，辨识力强
    //   修复陌生人泛化误识P0致命Bug
    return dot;
}

/**
 * @brief 计算两个特征向量的欧氏距离
 * @param a 特征向量A
 * @param b 特征向量B
 * @return 距离值，越小越相似；维度不一致时返回极大值
 */
double FaceRecognitionService::euclideanDistance(const QVector<double>& a, const QVector<double>& b) {
    if (a.size() != b.size()) return 1e10;
    QVector<double> an = a, bn = b;
    normalizeL2(an);
    normalizeL2(bn);
    double sum = 0;
    for (int i = 0; i < an.size(); ++i) {
        double diff = an[i] - bn[i];
        sum += diff * diff;
    }
    return qSqrt(sum);
}

/**
 * @brief 将采集特征与已录入人脸逐一比对
 * @param faceDescriptor 采集到的特征串
 * @param threshold 判定为同一人的相似度下限
 * @param highConfidence 高置信度帧的相似度下限，用于优先采用最佳帧
 * @return 匹配结果，含是否匹配、最佳相似度、命中用户与候选明细
 * @note 入库人脸特征已归一化，比对前同样归一化；低于阈值一律判为陌生人
 */
FaceRecognitionService::FaceMatchResult FaceRecognitionService::matchFace(
    const QString& faceDescriptor, double threshold, double highConfidence,
    double rejectThreshold, double marginThreshold, double maxEuclideanDist) {

    FaceMatchResult result;
    result.success = false;
    result.isStranger = false;
    result.userId = 0;
    result.similarity = 0;

    // 识别计时 + 统计日志埋点（支撑专利实测数据）
    QElapsedTimer recogTimer;
    recogTimer.start();
    QJsonArray enrolledFaces = getAllEnrolledFaces();
    const int candidateCnt = enrolledFaces.size();
    const QString mode = "multi"; // 后续按candidates数量判定单/多人脸

    QVector<double> inputVec = parseDescriptor(faceDescriptor);
    if (inputVec.size() < 64) {
        result.message = QStringLiteral("人脸特征数据异常(维度不足64)");
        FaceRecogLogDAO().insertLog({ SC::FACE_RESULT_ERROR, 0, 1, threshold, mode, candidateCnt,
                                      int(recogTimer.elapsed()), "" });
        return result;
    }

    // 获取所有已录入人脸
    QJsonArray enrolled = enrolledFaces;
    if (enrolled.isEmpty()) {
        result.message = QStringLiteral("系统中没有人脸数据，请先录入人脸");
        FaceRecogLogDAO().insertLog({ SC::FACE_RESULT_ERROR, 0, 1, threshold, mode, candidateCnt,
                                      int(recogTimer.elapsed()), "" });
        return result;
    }

    // 遍历所有已录入人脸，计算余弦相似度+欧氏距离
    // 移除提前退出逻辑：陌生人sim碰巧高时直接return success=true是致命Bug
    // 所有候选必须遍历完再综合判断
    //   修复陌生人泛化误识P0致命Bug
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

    // 统计埋点：追踪全局最佳候选（即便低于rejectThreshold），供stranger日志记录最高相似度
    double globalBestSim = 0;
    double globalBestDist = 1;

    for (const auto& e : enrolled) {
        QJsonObject obj = e.toObject();
        QVector<double> dbVec = parseDescriptor(obj["faceFeature"].toString());
        if (dbVec.size() < 64) continue;
        double sim = cosineSimilarity(inputVec, dbVec);
        double dist = euclideanDistance(inputVec, dbVec);
        // 只记录超过rejectThreshold的候选（原始余弦相似度，不同人<0.5）
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
        FaceRecogLogDAO().insertLog({ SC::FACE_RESULT_STRANGER, globalBestSim, globalBestDist, threshold,
                                      "multi", candidateCnt, int(recogTimer.elapsed()), "" });
        return result;
    }

    // 按相似度降序排序
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.similarity > b.similarity; });

    // ⚠ 无候选时必须先返回：candidates 为空时下面 candidates[0] 会越界访问
    //   这种情况说明无人脸与库内任何特征达到 rejectThreshold，属"识别失败"
    //   而非陌生人，不应让上层显示陌生人警报。
    if (candidates.isEmpty()) {
        // 质量不足：本次未产生有效候选，交给上层继续采样重试
        result.success = false;
        result.isStranger = false;
        result.message = QStringLiteral("未匹配，请重试");
        FaceRecogLogDAO().insertLog({ "no_candidate", globalBestSim, globalBestDist,
                                      rejectThreshold, "none",
                                      candidateCnt, int(recogTimer.elapsed()), "" });
        return result;
    }

    auto& best = candidates[0];
    double secondSim = candidates.size() > 1 ? candidates[1].similarity : 0;

  // 单人脸严格策略：系统仅1人脸时必须95%以上
    // 根因：只有1个候选时无法做第二名差距检查(marginThreshold)，陌生人碰巧0.94+就可能通过
    // 注意：单人脸模式下使用singleFaceThreshold=0.95，多人脸模式仍可用margin降至0.94
    // 设计理念：宁误拒不误识——陌生人绝对不能登录系统

    const double singleFaceThreshold = 0.97;  // 单人脸相似度门槛，低于此值判为非本人
    bool cosOk = false;

    if (candidates.size() == 1) {
        // 单人脸模式：没有第二名候选，必须达到singleFaceThreshold(0.95)才通过
        // 杜绝陌生人误识：只有1个人脸时，任何低于95%的匹配一律拒绝
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

    // 统计埋点：记录本次生效的判定模式（单/多人脸）与最终阈值
    const QString effMode = (candidates.size() == 1) ? "single" : "multi";
    const double effThreshold = (candidates.size() == 1) ? singleFaceThreshold : threshold;

    if (cosOk && distOk) {
        result.success = true;
        FaceRecogLogDAO().insertLog({ SC::FACE_RESULT_SUCCESS, best.similarity, best.euclideanDist,
                                      effThreshold, effMode, candidateCnt,
                                      int(recogTimer.elapsed()), best.workNo });
    } else {
        // 区分"陌生人"与"质量不足"：
        //   欧氏距离是第二道防线，特征偏斜/姿态不佳时余弦够但距离略超，
        //   这种属"差一点"，应让用户重试而不是直接判定陌生人。
        //   只有余弦明显低于阈值（真不像）才认定陌生人。
        const double strangerLine = rejectThreshold - 0.10;   // 明显不像陌生人
        const bool likelyStranger = (best.similarity < strangerLine);

        result.isStranger = likelyStranger;
        if (likelyStranger) {
            // 注意：QString::arg 使用 %1/%2 占位符并单独指定小数位，
            // 不支持 printf 风格的 %.1f（会导致 "Argument missing" 且显示错乱）
            result.message = QStringLiteral("人脸验证失败，相似度: %1%% 距离: %2 (未通过双验证)")
                              .arg(best.similarity * 100, 0, 'f', 1)
                              .arg(best.euclideanDist, 0, 'f', 3);
        } else {
            // 疑似同一人但本次质量不足：不算陌生人，交给上层继续采样
            result.message = QStringLiteral("未匹配，请重试");
        }
        FaceRecogLogDAO().insertLog({ SC::FACE_RESULT_REJECTED, best.similarity, best.euclideanDist,
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
    result.message = QStringLiteral("识别成功，相似度: %1%% 距离: %2")
                      .arg(best.similarity * 100, 0, 'f', 1)
                      .arg(best.euclideanDist, 0, 'f', 3);

    // 检查用户状态
    UserDAO dao;
    QJsonObject user = dao.findById(best.userId);
    if (user["status"].toString() != SC::USER_ACTIVE) {
        result.success = false;
        result.message = QStringLiteral("用户已被禁用或锁定");
    }
    // 从数据库读取真实角色，防止doLocalFaceVerify硬编码"user"导致管理员降级
    result.role = user["role"].toString();
    // 安全回填：getAllFaceFeatures可能缺少workNo/department，从findById补全
    if (result.workNo.isEmpty() && !user["workNo"].toString().isEmpty())
        result.workNo = user["workNo"].toString();
    if (result.department.isEmpty() && !user["department"].toString().isEmpty())
        result.department = user["department"].toString();

    return result;
}

/**
 * @brief 为用户录入人脸特征
 * @param userId 用户ID
 * @param faceDescriptor 128维特征串
 * @return true=录入成功
 */
bool FaceRecognitionService::enrollFace(int userId, const QString& faceDescriptor) {
    UserDAO dao;
    return dao.updateFace(userId, faceDescriptor);
}

/**
 * @brief 删除用户人脸特征
 * @param userId 用户ID
 * @return true=删除成功
 */
bool FaceRecognitionService::deleteFace(int userId) {
    UserDAO dao;
    return dao.deleteFace(userId);
}

/**
 * @brief 判断用户是否已录入人脸
 * @param userId 用户ID
 * @return true=已录入
 */
bool FaceRecognitionService::hasFaceEnrolled(int userId) {
    UserDAO dao;
    QJsonObject u = dao.findById(userId);
    return !u["faceFeature"].toString().isEmpty();
}

/**
 * @brief 查询全部已录入人脸的用户
 * @return 元素含 userId、realName、faceFeature 的数组
 */
QJsonArray FaceRecognitionService::getAllEnrolledFaces() {
    UserDAO dao;
    return dao.getAllFaceFeatures();
}
