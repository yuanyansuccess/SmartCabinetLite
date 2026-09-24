// 作者：袁燕  智能柜Qt Widget 2.0  AuthController实现
// 日期：2026-06-21 登录流程：用户名密码→SHA256验证→生成Token
// 人脸登录：特征提取→余弦相似度→阈值判断
#include "AuthController.h"
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QUuid>
#include <QDebug>
#include <QtMath>

AuthController::AuthController(QObject* parent) : QObject(parent) {}

// ═══════════════════════════════════════
// 密码学工具
// ═══════════════════════════════════════
QString AuthController::generateSalt() {
    QByteArray salt;
    for (int i = 0; i < 16; ++i)
        salt.append(static_cast<char>(QRandomGenerator::global()->bounded(256)));
    return salt.toHex();
}

QString AuthController::hashPassword(const QString& password, const QString& salt) {
    QByteArray data = (salt + password).toUtf8();
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

bool AuthController::verifyPassword(const QString& password, const QString& storedHash, const QString& storedSalt) {
    QString computed = hashPassword(password, storedSalt);
    // 恒定时间比较
    if (computed.size() != storedHash.size()) return false;
    int result = 0;
    for (int i = 0; i < computed.size(); ++i)
        result |= computed[i].toLatin1() ^ storedHash[i].toLatin1();
    return result == 0;
}

QString AuthController::generateToken(int userId) {
    return QUuid::createUuid().toString(QUuid::WithoutBraces)
           + QCryptographicHash::hash(QByteArray::number(QRandomGenerator::global()->generate()),
                                       QCryptographicHash::Sha256).toHex().left(16);
}

// ═══════════════════════════════════════
// 登录流程
// ═══════════════════════════════════════
AuthController::LoginResult AuthController::login(const QString& username, const QString& password) {
    LoginResult result;
    if (username.isEmpty() || password.isEmpty()) {
        result.msg = "用户名和密码不能为空";
        emit loginFailed(username, result.msg);
        return result;
    }
    User user = m_userDao.findUserByUsername(username);
    if (user.userId == 0) {
        result.msg = "用户不存在";
        emit loginFailed(username, result.msg);
        return result;
    }
    if (user.status != "active") {
        result.msg = "账户已被禁用";
        emit loginFailed(username, result.msg);
        return result;
    }
    if (!verifyPassword(password, user.passwordHash, user.passwordSalt)) {
        result.msg = "密码错误";
        emit loginFailed(username, result.msg);
        return result;
    }

    result.ok    = true;
    result.msg   = "登录成功";
    result.user  = user;
    result.token = generateToken(user.userId);

    m_currentUser  = user;
    m_currentToken = result.token;
    m_userDao.updateLastLogin(user.userId);

    emit loginSuccess(user.userId, user.realName);
    return result;
}

// ═══════════════════════════════════════
// 人脸识别登录
// ═══════════════════════════════════════
AuthController::LoginResult AuthController::loginByFace(const QString& faceFeature) {
    LoginResult result;
    if (faceFeature.isEmpty()) {
        result.msg = "人脸特征数据为空";
        return result;
    }

    // [V1.00.8.4] 修复：使用UserDAO获取人脸列表，不再裸写SQL [V6.9] 方法名更新
    // 获取所有已录入人脸的用户
    QList<User> enrolledUsers = m_userDao.findAllUsers(1, 10000, "", "", "active", "");
    double bestSimilarity = 0;
    int    bestUserId = 0;

    for (const User& u : enrolledUsers) {
        if (u.faceFeature.isEmpty()) continue;
        double sim = faceSimilarity(faceFeature, u.faceFeature);
        if (sim > bestSimilarity) {
            bestSimilarity = sim;
            bestUserId = u.userId;
        }
    }

    // 陌生人判断
    // [V2.03b 2026-06-29] 陌生人阈值 0.70→0.60（收紧，防止长相相似误判）
    // [V2.03f 2026-06-29] 陌生人阈值 0.60→0.65（配合94%通过线，收紧陌生人判定）
    if (bestSimilarity < 0.65) {
        result.ok  = false;
        result.msg = "检测到陌生人，该人员不在库中";
        emit strangerDetected(faceFeature);
        return result;
    }

    // 置信度分级
    // [V2.03f] 要求94%以上才验证成功，杜绝偶发误判
    // 高置信度 0.90→0.94（直接通过线）
    // 中等置信度 0.85→0.90（需二次验证，简化为直接拒绝）
    // 低于0.94一律拒绝
    if (bestSimilarity >= 0.94) {
        // 高置信度，直接通过
    } else {
        result.msg = "人脸识别未通过（相似度未达94%），请重试";
        return result;
    }

    User user = m_userDao.findUserById(bestUserId);
    if (user.userId == 0) {
        result.msg = "用户数据异常";
        return result;
    }

    result.ok    = true;
    result.msg   = "人脸识别成功";
    result.user  = user;
    result.token = generateToken(user.userId);

    m_currentUser  = user;
    m_currentToken = result.token;
    m_userDao.updateLastLogin(user.userId);

    emit loginSuccess(user.userId, user.realName);
    return result;
}

void AuthController::logout(int userId) {
    Q_UNUSED(userId)
    m_currentUser = User();
    m_currentToken.clear();
}

// ═══════════════════════════════════════
// 人脸相似度计算
// ═══════════════════════════════════════
double AuthController::faceSimilarity(const QString& f1, const QString& f2) {
    // 将Base64/逗号分隔的特征转为double数组
    auto parseFeature = [](const QString& s) -> QVector<double> {
        QVector<double> vec;
        // 先尝试Base64解码
        QByteArray decoded = QByteArray::fromBase64(s.toUtf8());
        QString decodedStr = QString::fromUtf8(decoded);
        if (decodedStr.isEmpty()) decodedStr = s;

        QStringList parts = decodedStr.split(',', Qt::SkipEmptyParts);
        for (const QString& p : parts) {
            bool ok;
            double v = p.trimmed().toDouble(&ok);
            if (ok) vec.append(v);
        }
        return vec;
    };

    QVector<double> v1 = parseFeature(f1);
    QVector<double> v2 = parseFeature(f2);

    if (v1.isEmpty() || v2.isEmpty()) return 0;
    int minSize = qMin(v1.size(), v2.size());

    // 余弦相似度
    double dot = 0, norm1 = 0, norm2 = 0;
    for (int i = 0; i < minSize; ++i) {
        dot  += v1[i] * v2[i];
        norm1 += v1[i] * v1[i];
        norm2 += v2[i] * v2[i];
    }
    if (norm1 < 1e-10 || norm2 < 1e-10) return 0;
    return dot / (qSqrt(norm1) * qSqrt(norm2));
}

User AuthController::currentUser() const {
    return m_currentUser;
}
