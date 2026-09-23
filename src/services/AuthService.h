/**
 * @file AuthService.h
 * @brief 认证服务 - 支持密码+人脸双因子登录
 * @author 袁燕
 * [V1.00.8] 添加人脸登录功能
 */
#pragma once
#include <QObject>
#include <QString>
#include <QJsonObject>

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
};
