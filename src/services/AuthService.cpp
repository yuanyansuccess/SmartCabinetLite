/**
 * @file AuthService.cpp
 * @brief 认证服务实现 - 支持密码+人脸双因子登录
 * @author 袁燕
 *
 * [V1.00.8.3] 统一使用驼峰命名字段，与Qt Widget前端保持一致
 * [2026-06-21v4] 修复loginByFace致命Bug：字符串严格相等(==)永远无法匹配人脸特征向量
 *             改为委托FaceRecognitionService::matchFace做余弦相似度比对
 */
#include "AuthService.h"
#include "UserDAO.h"
#include "FaceRecognitionService.h"
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QDebug>

// [2026-06-21] 修复LNK2005：db/层已包裹namespace db
using db::UserDAO;

AuthService::AuthService(QObject* parent) : QObject(parent) {}

AuthService::LoginResult AuthService::login(const QString& username, const QString& password) {
    LoginResult r; r.success = false;
    if (username.isEmpty() || password.isEmpty()) { r.message = "用户名和密码不能为空"; return r; }
    UserDAO dao;
    QJsonObject user = dao.findByUsername(username);
    if (user.isEmpty()) { r.message = "用户名或密码错误"; return r; }
    if (user["status"].toString() != "active") { r.message = "账户已被禁用"; return r; }
    if (!verifyPassword(password, user["passwordSalt"].toString(), user["passwordHash"].toString())) {
        r.message = "用户名或密码错误"; return r;
    }
    dao.recordLoginSuccess(user["userId"].toInt());
    user.remove("passwordHash"); user.remove("passwordSalt");
    r.success = true; r.message = "登录成功"; r.user = user;
    return r;
}

AuthService::LoginResult AuthService::loginByFace(const QString& faceFeature) {
    // [2026-06-21v4] 修复致命Bug：之前用storedFeature==faceFeature严格字符串相等比对
    // 两个人脸捕获的特征向量不可能完全相等，必须用余弦相似度等数值比对
    // 委托给FaceRecognitionService::matchFace做专业比对（余弦相似度+欧氏距离双验证）
    LoginResult r; r.success = false;
    if (faceFeature.isEmpty()) { r.message = "人脸特征为空"; return r; }

    FaceRecognitionService faceSvc;
    auto matchResult = faceSvc.matchFace(faceFeature, 0.65);

    if (matchResult.success) {
        UserDAO dao;
        QJsonObject userInfo = dao.findById(matchResult.userId);
        if (!userInfo.isEmpty() && userInfo["status"].toString() == "active") {
            dao.recordLoginSuccess(matchResult.userId);
            userInfo.remove("passwordHash"); userInfo.remove("passwordSalt");
            r.success = true;
            r.message = QStringLiteral("人脸登录成功 (相似度: %1%)")
                .arg(QString::number(matchResult.similarity * 100.0, 'f', 1));
            r.user = userInfo;
            return r;
        }
        r.message = "用户已被禁用或不存在";
        return r;
    }

    r.message = matchResult.message.isEmpty()
        ? QStringLiteral("人脸匹配失败") : matchResult.message;
    return r;
}

bool AuthService::isAdmin(const QJsonObject& user) const { 
    return user["role"].toString() == "admin"; 
}

bool AuthService::isActive(const QJsonObject& user) const { 
    return user["status"].toString() == "active"; 
}

QString AuthService::hashPassword(const QString& password, const QString& salt) {
    return QString(QCryptographicHash::hash((salt + password).toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString AuthService::generateSalt() {
    QByteArray b(16, 0);
    for (int i = 0; i < 16; ++i) b[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    return QString(b.toHex());
}

bool AuthService::verifyPassword(const QString& pw, const QString& salt, const QString& hash) {
    // 作者：袁燕，代码审查修复 — 使用恒定时间比较，防止时序侧信道攻击
    QString computed = hashPassword(pw, salt);
    if (computed.size() != hash.size()) return false;
    int result = 0;
    for (int i = 0; i < computed.size(); ++i)
        result |= computed[i].toLatin1() ^ hash[i].toLatin1();
    return result == 0;
}


