/**
 * @file AuthController.h
 * @brief 认证控制层：口令加盐哈希、密码校验与人脸特征向量比对
 * @author 袁燕
 */
#pragma once
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
