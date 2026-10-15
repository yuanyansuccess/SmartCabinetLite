/**
 * @file AuthService.h
 * @brief 认证服务 - 支持密码+人脸双因子登录
 * @author 袁燕
 * 添加人脸登录功能
 */
#pragma once
#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QVector>

class AuthService : public QObject {
    Q_OBJECT
public:
    explicit AuthService(QObject* parent = nullptr);
    
    struct LoginResult { 
        bool success; 
        QString message; 
        QJsonObject user; 
    };
    
    // 密码登录
    LoginResult login(const QString& username, const QString& password);
    
    // 人脸登录
    LoginResult loginByFace(const QString& faceFeature);
    
    // 权限检查
    bool isAdmin(const QJsonObject& user) const;
    bool isActive(const QJsonObject& user) const;
    
    // 密码工具方法
    static QString hashPassword(const QString& password, const QString& salt);
    static QString generateSalt();
    static bool verifyPassword(const QString& password, const QString& salt, const QString& storedHash);

    // ── 密码登录防爆破锁定（整机级：任意工号连续失败累计到同一计数） ──
    /// 密码登录是否处于锁定中，remainMs 返回剩余毫秒（可传 nullptr）
    static bool isPasswordLoginLocked(qint64* remainMs = nullptr);
    /// 记录一次密码失败，观察窗口内达到阈值时返回 true=本次触发锁定
    static bool recordPasswordFailure();
    /// 登录成功后清空失败计数（密码/刷脸成功都调用，保证只对连续失败计数）
    static void clearPasswordFailures();
    /// 锁定剩余毫秒转"X分X秒"文案（拦截提示与页面倒计时共用同一格式）
    static QString lockRemainText(qint64 remainMs);

    // 以下三个为单元测试注入时间推进专用（生产代码只用上面三个无参版本）
    /// 在指定时刻记录一次密码失败，返回是否触发锁定
    static bool recordPasswordFailureAt(qint64 nowTick);
    /// 判定指定时刻是否处于锁定中
    static bool isPasswordLoginLockedAt(qint64 nowTick, qint64* remainMs = nullptr);
    /// 清空全部锁定状态（含锁定截止时刻），用于测试用例间隔离
    static void resetPasswordGuard();

private:
    // 防爆破锁定状态（进程内唯一，仅主线程访问；时刻取单调时钟，防改系统时间绕过锁定）
    static QVector<qint64> s_pwdFailTicks;   // 观察窗口内的失败时刻列表
    static qint64 s_pwdLockUntilTick;        // 锁定截止时刻，<=0 表示未锁定
    static qint64 guardNow();                // 当前单调时钟毫秒数
    /// 触发锁定时写一条"登录失败"告警日志（写库失败仅记警告，不影响锁定）
    static void recordLockAlert();
};
