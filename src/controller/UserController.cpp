/**
 * @file UserController.cpp
 * @brief 用户管理控制层实现：用户增删改、启用禁用与密码维护
 * @author 袁燕
 */
#include "UserController.h"
#include "controller/AuthController.h"
#include "db/DepartmentDAO.h"  // 从 dao/ 迁移到 db/
#include "common/Constants.h"

UserController::UserController(QObject* parent) : QObject(parent) {}

/**
 * @brief 分页查询用户列表
 * @param page 页码，从1开始
 * @param pageSize 每页条数
 * @param keyword 关键字，为空不过滤
 * @param dept 部门，为空不过滤
 * @param status 状态，为空不过滤
 * @param role 角色，为空不过滤
 * @return 含 list 数组与 total 总数的分页结果
 */
UserController::PageResult UserController::getUserList(int page, int pageSize,
    const QString& keyword, const QString& dept, const QString& status, const QString& role) {
    PageResult r;
    r.page = page; r.pageSize = pageSize;
    r.total = m_dao.countUsers(keyword, dept, status, role);
    r.list  = m_dao.findAllUsers(page, pageSize, keyword, dept, status, role);
    return r;
}

/**
 * @brief 按ID查询用户
 * @param userId 用户ID
 * @return 用户实体；不存在时返回空实体
 */
User UserController::getUserById(int userId) {
    return m_dao.findUserById(userId);
}

/**
 * @brief 新增用户
 * @param user 用户实体，username 与 workNo 会被校验
 * @param password 初始口令，内部加盐哈希后存储
 * @return 新用户ID；校验或写入失败返回 -1
 */
int UserController::createUser(const User& user, const QString& password) {
    // 增加诊断日志，定位批量导入失败的具体原因
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
    // 保留调用方传入的status，不强制覆盖（批量导入需支持"禁用"状态）
    if (u.status.isEmpty()) u.status = SC::USER_ACTIVE;

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

/**
 * @brief 更新用户信息
 * @param user 含用户ID与待更新字段的实体
 * @return true=更新成功；false=工号校验不通过或写入失败
 */
bool UserController::updateUser(const User& user) {
    if (!validateWorkNo(user.workNo, user.userId)) return false;
    bool ok = m_dao.updateUser(user);
    if (ok) emit userUpdated(user.userId);
    return ok;
}

/**
 * @brief 逻辑删除用户：状态置为 deleted
 * @param userId 用户ID
 * @return true=删除成功；false=目标为管理员账号，不允许删除
 */
bool UserController::deleteUser(int userId) {
    User u = m_dao.findUserById(userId);
    if (u.role == SC::ROLE_ADMIN) return false; // 不可删除admin
    bool ok = m_dao.deleteUserById(userId);
    if (ok) emit userDeleted(userId);
    return ok;
}

/**
 * @brief 切换用户启用/禁用状态
 * @param userId 用户ID
 * @param status 目标状态，取 SC::USER_* 常量
 * @return true=更新成功
 */
bool UserController::setUserStatus(int userId, const QString& status) {
    // 启用/禁用切换对管理员账号同样生效；是否禁止禁用管理员属产品需求，见 docs/ARCHITECTURE.md 遗留待办
    User u = m_dao.findUserById(userId);
    bool ok = m_dao.updateUserStatus(userId, status);
    if (ok) emit userStatusChanged(userId, u.status, status);
    return ok;
}

/**
 * @brief 用户自助修改口令
 * @param userId 用户ID
 * @param oldPwd 原口令
 * @param newPwd 新口令，需通过强度校验
 * @return true=修改成功；false=原口令错误或新口令不合法
 */
bool UserController::changePassword(int userId, const QString& oldPwd, const QString& newPwd) {
    User u = m_dao.findUserById(userId);
    if (u.userId == 0) return false;
    if (!AuthController::verifyPassword(oldPwd, u.passwordHash, u.passwordSalt)) return false;
    QString newSalt = AuthController::generateSalt();
    QString newHash = AuthController::hashPassword(newPwd, newSalt);
    return m_dao.updateUserPassword(userId, newHash, newSalt);
}

/**
 * @brief 管理员重置用户口令
 * @param userId 用户ID
 * @param newPwd 新口令，需通过强度校验
 * @return true=重置成功
 */
bool UserController::resetPassword(int userId, const QString& newPwd) {
    User u = m_dao.findUserById(userId);
    if (u.userId == 0) return false;
    QString newSalt = AuthController::generateSalt();
    QString newHash = AuthController::hashPassword(newPwd, newSalt);
    return m_dao.updateUserPassword(userId, newHash, newSalt);
}

/**
 * @brief 为用户录入人脸特征
 * @param userId 用户ID
 * @param faceFeature 128维特征串（逗号分隔）
 * @return true=录入成功
 */
bool UserController::enrollFace(int userId, const QString& faceFeature) {
    bool ok = m_dao.updateFaceFeature(userId, faceFeature);
    if (ok) emit faceEnrolled(userId);
    return ok;
}

/**
 * @brief 清除用户人脸特征
 * @param userId 用户ID
 * @return true=清除成功
 */
bool UserController::deleteFace(int userId) {
    return m_dao.updateFaceFeature(userId, "");
}

/**
 * @brief 判断用户是否已录入人脸
 * @param userId 用户ID
 * @return true=已录入
 */
bool UserController::hasFaceEnrolled(int userId) {
    return !m_dao.findUserById(userId).faceFeature.isEmpty();
}

/**
 * @brief 查询全部部门名称
 * @return 部门名称列表
 */
QStringList UserController::allDepartments() {
    return db::DepartmentDAO().allNames();
}

/**
 * @brief 查询全部可用角色
 * @return 角色标识列表（管理员、普通用户）
 */
QStringList UserController::allRoles() {
    return {SC::ROLE_ADMIN, SC::ROLE_USER};
}

/**
 * @brief 校验登录账号格式
 * @param username 登录账号
 * @return true=为1~32位纯数字
 */
bool UserController::validateUsername(const QString& username) {
    // 工号为纯数字（登录账号与数字键盘输入统一），1-32位
    static QRegularExpression re("^[0-9]{1,32}$");
    return re.match(username).hasMatch();
}

/**
 * @brief 校验工号是否合法且未被占用
 * @param workNo 工号
 * @param excludeUserId 编辑场景下排除自身，允许与原工号相同
 * @return true=合法且未重复
 */
bool UserController::validateWorkNo(const QString& workNo, int excludeUserId) {
    if (workNo.isEmpty()) return false;
    User existing = m_dao.findUserByWorkNo(workNo);
    if (existing.userId > 0 && existing.userId != excludeUserId) return false;
    return true;
}

/**
 * @brief 校验口令强度
 * @param password 待校验口令
 * @param errorMsg 校验失败原因，校验通过时不修改
 * @return true=符合强度要求
 */
bool UserController::validatePassword(const QString& password, QString& errorMsg) {
    if (password.length() < 6) { errorMsg = "密码长度至少6位"; return false; }
    if (password.length() > 64) { errorMsg = "密码长度不能超过64位"; return false; }
    return true;
}
