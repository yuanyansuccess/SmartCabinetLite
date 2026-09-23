// 作者：袁燕  智能柜Qt Widget 2.0  UserController实现
// 日期：2026-06-21
// [V6.9 2026-06-24] 统一到db/目录namespace db，方法名更新为合并后的新API
#include "UserController.h"
#include "controller/AuthController.h"
#include "db/DepartmentDAO.h"  // [2026-06-23] 从 dao/ 迁移到 db/

UserController::UserController(QObject* parent) : QObject(parent) {}

UserController::PageResult UserController::getUserList(int page, int pageSize,
    const QString& keyword, const QString& dept, const QString& status, const QString& role) {
    PageResult r;
    r.page = page; r.pageSize = pageSize;
    r.total = m_dao.countUsers(keyword, dept, status, role);
    r.list  = m_dao.findAllUsers(page, pageSize, keyword, dept, status, role);
    return r;
}

User UserController::getUserById(int userId) {
    return m_dao.findUserById(userId);
}

int UserController::createUser(const User& user, const QString& password) {
    // [2026-06-27] 增加诊断日志，定位批量导入失败的具体原因
    if (!validateUsername(user.username)) {
        qWarning() << "[createUser] 工号格式不合法:" << user.username
                   << "(规则: 1-32位纯数字)";
        return -1;
    }
    if (!validateWorkNo(user.workNo)) {
        qWarning() << "[createUser] 工号已存在:" << user.workNo;
        return -1;
    }

    QString errorMsg;
    if (!validatePassword(password, errorMsg)) {
        qWarning() << "[createUser] 密码不合法:" << errorMsg;
        return -1;
    }

    User u = user;
    u.passwordSalt = AuthController::generateSalt();
    u.passwordHash = AuthController::hashPassword(password, u.passwordSalt);
    // [2026-06-27] 保留调用方传入的status，不再强制覆盖（批量导入需支持"禁用"状态）
    if (u.status.isEmpty()) u.status = "active";

    int id = m_dao.insertUser(u);
    if (id > 0) {
        emit userCreated(id);
    } else {
        qWarning() << "[createUser] 数据库插入失败 | username:" << u.username
                   << "workNo:" << u.workNo << "deptId:" << u.deptId
                   << "department:" << u.department << "role:" << u.role;
    }
    return id;
}

bool UserController::updateUser(const User& user) {
    if (!validateWorkNo(user.workNo, user.userId)) return false;
    bool ok = m_dao.updateUser(user);
    if (ok) emit userUpdated(user.userId);
    return ok;
}

bool UserController::deleteUser(int userId) {
    User u = m_dao.findUserById(userId);
    if (u.role == "admin") return false; // 不可删除admin
    bool ok = m_dao.deleteUserById(userId);
    if (ok) emit userDeleted(userId);
    return ok;
}

bool UserController::setUserStatus(int userId, const QString& status) {
    User u = m_dao.findUserById(userId);
    //if (u.role == "admin" && status != "active") return false;
    //TODO 20260625马慧芳说为啥 管理员可以不能禁用 ，这块需求可以和用户沟通下
    bool ok = m_dao.updateUserStatus(userId, status);
    if (ok) emit userStatusChanged(userId, u.status, status);
    return ok;
}

bool UserController::changePassword(int userId, const QString& oldPwd, const QString& newPwd) {
    User u = m_dao.findUserById(userId);
    if (u.userId == 0) return false;
    if (!AuthController::verifyPassword(oldPwd, u.passwordHash, u.passwordSalt)) return false;
    QString newSalt = AuthController::generateSalt();
    QString newHash = AuthController::hashPassword(newPwd, newSalt);
    return m_dao.updateUserPassword(userId, newHash, newSalt);
}

bool UserController::resetPassword(int userId, const QString& newPwd) {
    User u = m_dao.findUserById(userId);
    if (u.userId == 0) return false;
    QString newSalt = AuthController::generateSalt();
    QString newHash = AuthController::hashPassword(newPwd, newSalt);
    return m_dao.updateUserPassword(userId, newHash, newSalt);
}

bool UserController::enrollFace(int userId, const QString& faceFeature) {
    bool ok = m_dao.updateFaceFeature(userId, faceFeature);
    if (ok) emit faceEnrolled(userId);
    return ok;
}

bool UserController::deleteFace(int userId) {
    return m_dao.updateFaceFeature(userId, "");
}

bool UserController::hasFaceEnrolled(int userId) {
    return !m_dao.findUserById(userId).faceFeature.isEmpty();
}

QStringList UserController::allDepartments() {
    return db::DepartmentDAO().allNames();
}

QStringList UserController::allRoles() {
    return {"admin", "user"};
}

bool UserController::validateUsername(const QString& username) {
    // [2026-09-23] 工号改为纯数字（登录账号与数字键盘输入统一），1-32位
    static QRegularExpression re("^[0-9]{1,32}$");
    return re.match(username).hasMatch();
}

bool UserController::validateWorkNo(const QString& workNo, int excludeUserId) {
    if (workNo.isEmpty()) return false;
    User existing = m_dao.findUserByWorkNo(workNo);
    if (existing.userId > 0 && existing.userId != excludeUserId) return false;
    return true;
}

bool UserController::validatePassword(const QString& password, QString& errorMsg) {
    if (password.length() < 6) { errorMsg = "密码长度至少6位"; return false; }
    if (password.length() > 64) { errorMsg = "密码长度不能超过64位"; return false; }
    return true;
}
