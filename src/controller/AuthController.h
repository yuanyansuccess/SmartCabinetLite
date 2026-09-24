#pragma once
// 作者：袁燕  智能柜Qt Widget 2.0  认证业务控制层
// 日期：2026-06-21 功能：登录验证、密码哈希、Token管理、人脸识别匹配
// [V6.9 2026-06-24] 统一到db/目录namespace db
#include <QObject>
#include <QString>
#include <QDateTime>
#include "model/User.h"
#include "db/UserDAO.h"

class AuthController : public QObject {
    Q_OBJECT
public:
    explicit AuthController(QObject* parent = nullptr);

    // ── 登录 ──
    struct LoginResult { bool ok; QString msg; User user; QString token; };
    LoginResult login(const QString& username, const QString& password);
    LoginResult loginByFace(const QString& faceFeature);
    void        logout(int userId);

    // ── 密码 ──
    static QString hashPassword(const QString& password, const QString& salt);
    static QString generateSalt();
    static bool    verifyPassword(const QString& password, const QString& storedHash, const QString& storedSalt);

    // ── 人脸 ──

    User currentUser() const;

signals:
    void loginSuccess(int userId, const QString& realName);
    void loginFailed(const QString& username, const QString& reason);
    void strangerDetected(const QString& faceFeature);
    void tokenExpired(int userId);

private:
    db::UserDAO m_userDao;
    User    m_currentUser;
    QString m_currentToken;

    QString generateToken(int userId);                           // 生成登录Token
    double  faceSimilarity(const QString& feature1, const QString& feature2); // 余弦相似度
};
