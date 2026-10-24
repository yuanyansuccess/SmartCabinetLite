/**
 * @file FaceRecognitionService.h
 * @brief 人脸识别服务 - 特征向量比对 (复刻后端AuthService的余弦相似度算法)
 * @author 袁燕
 *
 * 后端算法说明：
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
        // 识别结果直接决定能否登录，基础类型必须给确定初值，不能靠调用方记得赋值
        bool success = false;
        bool isStranger = false;
        int userId = 0;
        QString username;
        QString realName;
        QString workNo;
        QString department;
        QString role;  // 从DB读取真实角色，防止硬编码"user"导致权限错误
        int mustChangePwd = 0;  // 1=该用户仍使用初始口令，登录后须提示修改
        double similarity = 0.0;
        QString message;
    };

    // 解析描述符字符串 "0.1,0.2,0.3,..." → QVector<double>
    static QVector<double> parseDescriptor(const QString& descriptorStr);

    // 计算余弦相似度 (L2归一化+点积)
    static double cosineSimilarity(const QVector<double>& a, const QVector<double>& b);

    // 计算欧氏距离
    static double euclideanDistance(const QVector<double>& a, const QVector<double>& b);

    // 从数据库获取所有已录入人脸并进行匹配
    // 阈值依据实测分布标定（跨人上限以实发误判事件为准）：
    //   同人不同帧 0.9403~0.9967（低质量帧下限），跨人（男生间相像）实测上限 0.9551。
    //   0.94~0.9551 为跨人误判高危区，实发两例：刷脸者被识别成另一男生登录成功；
    //   同一未录入者登录页最优帧 0.93x 被拒、忘记密码单帧 0.94x 却匹配到他人账号——
    //   判定线落在跨人波动带中间时，两边结论随机，必须整体抬到跨人上限之上。
    // 1. threshold 0.96（多人脸第一名通过下限，兼任第二名拒绝线 secondRejectLine）：
    //    必须高于跨人上限 0.9551，第一名才可能是"本人明确胜出"而非"跨人撞线"
    // 2. singleFaceThreshold 0.96（库内仅 1 人脸时的最低门槛，陌生人撞线实测可达 0.955）
    // 3. maxEuclideanDist 0.80（欧氏距离第二道防线）
    // 本人低质量帧（相似度 0.9403~0.96）会被 0.96 门槛拒掉，靠多帧取最优与重试补回。
    // 第二名按相似度绝对值判定，不按与第一名差多少来判：长相相近的两人与本人帧差距仅约 0.012，
    // 按差距判会把真人误拒。
    // 设计理念：宁误拒不误识——陌生人绝对不能登录系统

    FaceMatchResult matchFace(const QString& faceDescriptor,
                               double threshold = 0.965,
                               double highConfidence = 0.965,
                               double rejectThreshold = 0.35,
                               double marginThreshold = 0.15,
                               double maxEuclideanDist = 0.80,
                               double quality = 0);

    /// 人脸查重命中结果：命中"同一张脸已挂在其他账户"时携带归属用户信息
    struct FaceDuplicateInfo {
        bool    duplicated = false;  ///< true=库内已有同一张脸且不属于本次录入用户
        double  similarity  = 0;     ///< 与命中者的余弦相似度
        int     userId      = 0;     ///< 命中者的用户ID
        QString username;            ///< 命中者的登录账号
        QString realName;            ///< 命中者的姓名
        QString workNo;              ///< 命中者的工号
        QString department;          ///< 命中者的部门

        /// 命中者的可读标识，形如「张三（工号 CF001）」，缺姓名时退回登录账号
        QString ownerText() const;
    };

    /// 人脸录入结果：区分写入成功、查重拦截与写入失败三种情况
    struct EnrollFaceResult {
        bool             success    = false;  ///< true=特征已写入数据库
        bool             duplicated = false;  ///< true=被查重拦截，本次未写入
        QString          message;              ///< 面向操作员的提示文案
        FaceDuplicateInfo duplicate;           ///< duplicated 为 true 时有效
    };

    /// 录入人脸特征（含查重）：命中其他账户的同一张脸时拒绝写入
    EnrollFaceResult enrollFaceChecked(int userId, const QString& faceDescriptor);

    /// 查询采集特征是否与库内某个其他账户的人脸重复（自身人脸不参与比对）
    /// @note 查重范围为全部未注销账户：账户被禁用不等于其人脸可释放，
    ///       否则账户重新启用后会出现同一张脸挂在两个账户上
    FaceDuplicateInfo findDuplicateFace(const QString& faceDescriptor, int excludeUserId);

    /// 在给定人脸列表中找出与采集特征最相似的用户（纯算法，不访问数据库，便于单元测试）
    static FaceDuplicateInfo findBestMatch(const QString& faceDescriptor,
                                           const QJsonArray& enrolledFaces,
                                           int excludeUserId,
                                           double similarityThreshold);

    // 删除人脸特征
    bool deleteFace(int userId);

    // 获取所有已录入人脸
    /// 获取所有已录入人脸
    /// @param activeOnly true=仅启用账户（登录匹配用）；false=全部未注销账户（录入查重用）
    QJsonArray getAllEnrolledFaces(bool activeOnly = true);

private:
    // L2归一化
    static void normalizeL2(QVector<double>& v);
};
