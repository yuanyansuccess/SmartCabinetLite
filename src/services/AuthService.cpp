/**
 * @file AuthService.cpp
 * @brief 认证服务实现 - 支持密码+人脸双因子登录
 * @author 袁燕
 *
 * 统一使用驼峰命名字段，与Qt Widget前端保持一致
 * loginByFace：人脸特征向量无法用字符串相等比较，委托FaceRecognitionService::matchFace做余弦相似度比对
 */
#include "AuthService.h"
#include "UserDAO.h"
#include "FaceRecognitionService.h"
#include "db/AlertDAO.h"
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QElapsedTimer>
#include <QDebug>
#include "common/Constants.h"

// 修复LNK2005：db/层已包裹namespace db
using db::UserDAO;

AuthService::AuthService(QObject* parent) : QObject(parent) {}

// ═══════════════════════════════════════════════════════════
// 密码登录防爆破锁定（整机级）
// 状态为本进程静态数据：观察窗口内的失败时刻列表 + 锁定截止时刻。
// 时刻一律取 QElapsedTimer 单调时钟——用户改系统时间无法绕过锁定。
// ═══════════════════════════════════════════════════════════
QVector<qint64> AuthService::s_pwdFailTicks;
qint64 AuthService::s_pwdLockUntilTick = 0;

namespace {
/**
 * @brief 进程级单调时钟（首次使用时启动）
 * @return 全局唯一的 QElapsedTimer 引用
 */
QElapsedTimer& authClock() {
    static QElapsedTimer clock;
    if (!clock.isValid()) clock.start();
    return clock;
}
}

/**
 * @brief 取防爆破模块的单调时钟当前毫秒数
 * @return 距时钟启动的毫秒数（不受系统时间修改影响）
 */
qint64 AuthService::guardNow() {
    return authClock().elapsed();
}

/**
 * @brief 判定指定时刻密码登录是否处于锁定中
 * @param nowTick 单调时钟毫秒数
 * @param remainMs 输出锁定剩余毫秒数，可为 nullptr
 * @return true=锁定中；false=未锁定或已到期
 * @note 锁定到期时顺便清掉截止时刻（自动恢复，无需额外定时器）
 */
bool AuthService::isPasswordLoginLockedAt(qint64 nowTick, qint64* remainMs) {
    if (s_pwdLockUntilTick > 0 && nowTick < s_pwdLockUntilTick) {
        if (remainMs) *remainMs = s_pwdLockUntilTick - nowTick;
        return true;
    }
    if (s_pwdLockUntilTick > 0) s_pwdLockUntilTick = 0;  // 已到期，恢复密码登录
    if (remainMs) *remainMs = 0;
    return false;
}

/**
 * @brief 在指定时刻记录一次密码登录失败
 * @param nowTick 单调时钟毫秒数
 * @return true=本次失败触发了锁定（调用方据此写告警日志）
 * @note 滑动窗口：先剔除滑出观察窗口的旧失败，窗口内累计达到阈值即锁定；
 *       已在锁定中时不再累计（锁定期内页面不发起校验，此处为兜底）
 */
bool AuthService::recordPasswordFailureAt(qint64 nowTick) {
    if (isPasswordLoginLockedAt(nowTick)) return false;
    while (!s_pwdFailTicks.isEmpty()
           && nowTick - s_pwdFailTicks.first() > SC::PWD_FAIL_WINDOW_MS) {
        s_pwdFailTicks.removeFirst();
    }
    s_pwdFailTicks.append(nowTick);
    if (s_pwdFailTicks.size() >= SC::PWD_FAIL_LOCK_TIMES) {
        s_pwdLockUntilTick = nowTick + SC::PWD_LOCK_MS;
        s_pwdFailTicks.clear();  // 锁定中无需继续计数，解锁后重新累计
        return true;
    }
    return false;
}

/**
 * @brief 密码登录是否处于锁定中（按当前时刻判定）
 * @param remainMs 输出锁定剩余毫秒数，可为 nullptr
 * @return true=锁定中
 */
bool AuthService::isPasswordLoginLocked(qint64* remainMs) {
    return isPasswordLoginLockedAt(guardNow(), remainMs);
}

/**
 * @brief 记录一次密码登录失败（按当前时刻）
 * @return true=本次失败触发了锁定
 */
bool AuthService::recordPasswordFailure() {
    return recordPasswordFailureAt(guardNow());
}

/**
 * @brief 清空失败计数
 * @note 登录成功时调用，保证只对"连续"失败计数
 */
void AuthService::clearPasswordFailures() {
    s_pwdFailTicks.clear();
}

/**
 * @brief 清空全部锁定状态（锁定截止时刻+失败计数）
 * @note 供单元测试在用例间隔离防爆破状态
 */
void AuthService::resetPasswordGuard() {
    s_pwdFailTicks.clear();
    s_pwdLockUntilTick = 0;
}

/**
 * @brief 锁定剩余毫秒数转为人读时间文案
 * @param remainMs 剩余毫秒数
 * @return "X分X秒"格式（秒数向上取整，避免倒计时长时间停留在0秒）
 */
QString AuthService::lockRemainText(qint64 remainMs) {
    if (remainMs < 0) remainMs = 0;
    const qint64 totalSec = (remainMs + 999) / 1000;
    return QStringLiteral("%1分%2秒").arg(totalSec / 60).arg(totalSec % 60);
}

/**
 * @brief 触发整机锁定时写一条"登录失败"告警（管理员在告警日志页可查）
 * @note 告警写库失败只记警告日志，不影响锁定本身；
 *       告警类型按编码查 sys_alert_type，查不到时 DAO 内自动用系统类型兜底
 */
void AuthService::recordLockAlert() {
    db::AlertDAO alertDao;
    AlertLog alert;
    alert.typeId = alertDao.findTypeIdByCode(SC::ALERT_TYPE_LOGIN_FAIL);
    alert.message = QStringLiteral("密码连续输错%1次，密码登录已锁定%2分钟")
                        .arg(SC::PWD_FAIL_LOCK_TIMES)
                        .arg(SC::PWD_LOCK_MS / (60 * 1000));
    alert.status = SC::ALERT_UNHANDLED;
    alertDao.insertAlert(alert);
}

/**
 * @brief 账号口令登录
 * @param username 登录账号
 * @param password 口令明文
 * @return success=是否登录成功，message=失败原因，user=用户信息
 * @note 口令以加盐哈希比对，库中不存明文
 */
AuthService::LoginResult AuthService::login(const QString& username, const QString& password) {
    LoginResult r; r.success = false;
    // 防爆破锁定优先于一切校验：锁定期内拒绝密码登录（刷脸登录不受影响）
    qint64 remainMs = 0;
    if (isPasswordLoginLocked(&remainMs)) {
        r.message = QStringLiteral("密码登录已锁定，请%1后重试，或刷脸登录")
                        .arg(lockRemainText(remainMs));
        return r;
    }
    if (username.isEmpty() || password.isEmpty()) { r.message = "请输入工号和密码"; return r; }
    UserDAO dao;
    QJsonObject user = dao.findByUsername(username);
    // 工号不存在与口令错误给同一句提示：区分二者等于告诉攻击者哪些工号有效
    if (user.isEmpty()) {
        // 工号不存在同样计入防爆破窗口（防"枚举工号+逐个试密码"）
        if (recordPasswordFailure()) {
            recordLockAlert();
            // 触发锁定的这一次失败必须明确告知锁定事实，
            // 仍返回"工号或密码错误"会让操作员在锁定期内继续盲试
            r.locked = true;
            r.message = QStringLiteral("密码连续输错%1次，密码登录已锁定%2分钟，请改用刷脸登录")
                            .arg(SC::PWD_FAIL_LOCK_TIMES)
                            .arg(SC::PWD_LOCK_MS / (60 * 1000));
            return r;
        }
        r.message = "工号或密码错误"; return r;
    }
    if (user["status"].toString() != SC::USER_ACTIVE) { r.message = "账户已禁用"; return r; }
    if (!verifyPassword(password, user["passwordSalt"].toString(), user["passwordHash"].toString())) {
        // 密码校验失败计入防爆破窗口，达到阈值自动进入整机锁定
        if (recordPasswordFailure()) {
            recordLockAlert();
            r.locked = true;
            r.message = QStringLiteral("密码连续输错%1次，密码登录已锁定%2分钟，请改用刷脸登录")
                            .arg(SC::PWD_FAIL_LOCK_TIMES)
                            .arg(SC::PWD_LOCK_MS / (60 * 1000));
            return r;
        }
        r.message = "工号或密码错误"; return r;
    }
    // 登录成功：清空失败计数（只对"连续"失败计数）
    clearPasswordFailures();
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
    // 阈值必须与登录页识别一致：跨人相似度实测可达 0.9551，
    // 低于 0.96 的门槛会把长相相近的男生误判为同一人登录
    auto matchResult = faceSvc.matchFace(faceFeature, 0.96);

    if (matchResult.success) {
        UserDAO dao;
        QJsonObject userInfo = dao.findById(matchResult.userId);
        if (!userInfo.isEmpty() && userInfo["status"].toString() == SC::USER_ACTIVE) {
            // 刷脸登录成功同样清空密码失败计数，避免历史误输残留影响后续判定
            clearPasswordFailures();
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
