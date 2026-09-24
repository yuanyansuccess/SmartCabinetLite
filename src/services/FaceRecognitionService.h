/**
 * @file FaceRecognitionService.h
 * @brief 人脸识别服务 - 特征向量比对 (复刻后端AuthService的余弦相似度算法)
 * @author 袁燕
 *
 * 后端算法说明：
 * - 余弦相似度 + 欧氏距离双验证
 * - 高置信度(>=0.78)直接通过
 * - 中等置信度(>=0.65)检查与第2名差距>0.08
 * - 低于0.50直接拒绝
 * - 无候选→陌生人
 */
#pragma once
#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>

class FaceRecognitionService : public QObject {
    Q_OBJECT
public:
    explicit FaceRecognitionService(QObject* parent = nullptr);

    struct FaceMatchResult {
        bool success;
        bool isStranger;
        int userId;
        QString username;
        QString realName;
        QString workNo;
        QString department;
        QString role;         // [V6.3] 从DB读取真实角色，防止硬编码"user"导致权限错误
        double similarity;
        QString message;
    };

    // 解析描述符字符串 "0.1,0.2,0.3,..." → QVector<double>
    static QVector<double> parseDescriptor(const QString& descriptorStr);

    // 计算余弦相似度 (L2归一化+点积)
    static double cosineSimilarity(const QVector<double>& a, const QVector<double>& b);

    // 计算欧氏距离
    static double euclideanDistance(const QVector<double>& a, const QVector<double>& b);

    // 从数据库获取所有已录入人脸并进行匹配
    // [V2.17 2026-07-06 袁总指令] 陌生人误识根除：阈值95%以上才通过
    // 核心原则：只有1个人脸时更严格（95%），多人脸时margin验证可降至94%
    // 设计理念：宁误拒不误识——陌生人绝对不能登录系统
    // 1. threshold 0.94→0.94（多人脸最低相似度=94%，有第二名差距保护）
    // 2. highConfidence 0.94→0.95（单人脸必须95%以上，无第二名差距保护）
    // 3. singleFaceThreshold=0.95（系统仅1人脸时的最低门槛，杜绝陌生人误识）
    // 4. maxEuclideanDist 0.80（欧氏距离收紧不变）
    //   作者：袁燕
    FaceMatchResult matchFace(const QString& faceDescriptor,
                               double threshold = 0.97,
                               double highConfidence = 0.97,
                               double rejectThreshold = 0.35,
                               double marginThreshold = 0.15,
                               double maxEuclideanDist = 0.80);

    // 录入人脸特征
    bool enrollFace(int userId, const QString& faceDescriptor);

    // 删除人脸特征
    bool deleteFace(int userId);

    // 检查是否已录入
    bool hasFaceEnrolled(int userId);

    // 获取所有已录入人脸
    QJsonArray getAllEnrolledFaces();

private:
    // L2归一化
    static void normalizeL2(QVector<double>& v);
};
