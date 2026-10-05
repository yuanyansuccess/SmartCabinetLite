/**
 * @file AuthService.cpp
 * @brief 认证服务实现 - 支持密码+人脸双因子登录
 * @author 袁燕
 *
 * 统一使用驼峰命名字段，与Qt Widget前端保持一致
 * 修复loginByFace致命Bug：字符串严格相等(==)永远无法匹配人脸特征向量
 *             改为委托FaceRecognitionService::matchFace做余弦相似度比对
 */
#include "AuthService.h"
#include "UserDAO.h"
#include "FaceRecognitionService.h"
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QDebug>
#include "common/Constants.h"

// 修复LNK2005：db/层已包裹namespace db
using db::UserDAO;

AuthService::AuthService(QObject* parent) : QObject(parent) {}

/**
 * @brief 账号口令登录
 * @param username 登录账号
 * @param password 口令明文
 * @return success=是否登录成功，message=失败原因，user=用户信息
 * @note 口令以加盐哈希比对，库中不存明文
 */
AuthService::LoginResult AuthService::login(const QString& username, const QString& password) {
    LoginResult r; r.success = false;
    if (username.isEmpty() || password.isEmpty()) { r.message = "用户名和密码不能为空"; return r; }
    UserDAO dao;
    QJsonObject user = dao.findByUsername(username);
    if (user.isEmpty()) { r.message = "用户名或密码错误"; return r; }
    if (user["status"].toString() != SC::USER_ACTIVE) { r.message = "账户已被禁用"; return r; }
    if (!verifyPassword(password, user["passwordSalt"].toString(), user["passwordHash"].toString())) {
        r.message = "用户名或密码错误"; return r;
    }
    dao.recordLoginSuccess(user["userId"].toInt());
    user.remove("passwordHash"); user.remove("passwordSalt");
    r.success = true; r.message = "登录成功"; r.user = user;
    return r;
}

/**
 * @brief 人脸特征登录
 * @param faceFeature 采集到的128维特征串
 * @return success=是否匹配到已录入人脸，message=失败原因，user=用户信息
 * @note 按余弦相似度择优匹配，相似度低于阈值视为陌生人
 */
AuthService::LoginResult AuthService::loginByFace(const QString& faceFeature) {
    // 人脸特征按余弦相似度比对：同一人在不同次采集下特征值并不完全相同，
    // 因此不能用字符串相等判断
    // 两个人脸捕获的特征向量不可能完全相等，必须用余弦相似度等数值比对
    // 委托给FaceRecognitionService::matchFace做专业比对（余弦相似度+欧氏距离双验证）
    LoginResult r; r.success = false;
    if (faceFeature.isEmpty()) { r.message = "人脸特征为空"; return r; }

    FaceRecognitionService faceSvc;
    auto matchResult = faceSvc.matchFace(faceFeature, 0.65);

    if (matchResult.success) {
        UserDAO dao;
        QJsonObject userInfo = dao.findById(matchResult.userId);
        if (!userInfo.isEmpty() && userInfo["status"].toString() == SC::USER_ACTIVE) {
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

/**
 * @brief 判断用户是否为管理员
 * @param user 用户信息对象
 * @return true=管理员
 */
bool AuthService::isAdmin(const QJsonObject& user) const { 
    return user["role"].toString() == SC::ROLE_ADMIN; 
}

/**
 * @brief 判断用户是否为启用状态
 * @param user 用户信息对象
 * @return true=已启用
 */
bool AuthService::isActive(const QJsonObject& user) const { 
    return user["status"].toString() == SC::USER_ACTIVE; 
}

/**
 * @brief 计算口令哈希
 * @param password 口令明文
 * @param salt 口令盐值
 * @return 十六进制哈希串
 */
QString AuthService::hashPassword(const QString& password, const QString& salt) {
    return QString(QCryptographicHash::hash((salt + password).toUtf8(), QCryptographicHash::Sha256).toHex());
}

/**
 * @brief 生成随机盐值
 * @return 16字节随机盐的十六进制串
 */
QString AuthService::generateSalt() {
    QByteArray b(16, 0);
    for (int i = 0; i < 16; ++i) b[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    return QString(b.toHex());
}

/**
 * @brief 校验口令
 * @param pw 口令明文
 * @param salt 盐值
 * @param hash 库中存储的哈希
 * @return true=校验通过
 * @note 采用恒定时间比较，避免时序侧信道泄露信息
 */
bool AuthService::verifyPassword(const QString& pw, const QString& salt, const QString& hash) {
    // 恒定时间比较，避免时序侧信道泄露信息
    QString computed = hashPassword(pw, salt);
    if (computed.size() != hash.size()) return false;
    int result = 0;
    for (int i = 0; i < computed.size(); ++i)
        result |= computed[i].toLatin1() ^ hash[i].toLatin1();
    return result == 0;
}
