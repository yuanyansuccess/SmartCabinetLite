/**
 * @file UserController.cpp
 * @brief 用户管理控制层实现：用户增删改、启用禁用与密码维护
 * @author 袁燕
 */
#include "UserController.h"
#include "services/AuthService.h"  // 口令加盐哈希工具（与登录链路共用同一实现）
#include "db/DepartmentDAO.h"  // 从 dao/ 迁移到 db/
#include "services/FaceRecognitionService.h"  // 人脸录入统一入口（内含查重）
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
                   << "(规则: 6-12位纯数字)";
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
    u.passwordSalt = AuthService::generateSalt();
    u.passwordHash = AuthService::hashPassword(password, u.passwordSalt);
    // 保留调用方传入的status，不强制覆盖（批量导入需支持"禁用"状态）
    if (u.status.isEmpty()) u.status = SC::USER_ACTIVE;

    int id = m_dao.insertUser(u);
    if (id > 0) {
        // 新建用户一律标记"下次登录强制修改初始密码"，防止默认口令长期沿用
        m_dao.setMustChangePwd(id, 1);
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
 * @param oldPwd 原口令（需与库中哈希比对通过）
 * @param newPwd 新口令：长度6~64位，不能是初始口令123456，不能与原口令相同
 * @return true=修改成功；false=原口令错误或新口令不合法
 * @note 修改成功后强制改密标志由DAO自动清除，下次登录不再弹强制改密窗
 */
bool UserController::changePassword(int userId, const QString& oldPwd, const QString& newPwd) {
    User u = m_dao.findUserById(userId);
    if (u.userId == 0) return false;
    if (!AuthService::verifyPassword(oldPwd, u.passwordSalt, u.passwordHash)) return false;
    // 初始口令安全期：用户自设新口令不允许继续使用默认初始口令
    if (newPwd == SC::DEFAULT_INIT_PASSWORD) {
        qWarning() << "[changePassword] 新口令不能是初始口令";
        return false;
    }
    if (newPwd == oldPwd) {
        qWarning() << "[changePassword] 新口令不能与原口令相同";
        return false;
    }
    QString errorMsg;
    if (!validatePassword(newPwd, errorMsg)) {
        qWarning() << "[changePassword] 新口令不合法:" << errorMsg;
        return false;
    }
    QString newSalt = AuthService::generateSalt();
    QString newHash = AuthService::hashPassword(newPwd, newSalt);
    return m_dao.updateUserPassword(userId, newHash, newSalt);
}

/**
 * @brief 首次登录强制修改口令（不校验原口令）
 * @param userId 用户ID
 * @param newPwd 新口令：长度6~64位，且不能是初始口令123456
 * @return true=修改成功；false=新口令不合法或写库失败
 * @note 适用场景：用户用初始口令（公开默认值）登录后被要求自行设置新口令。
 *       此时原口令就是初始口令，校验它没有任何安全意义，故本接口不做原口令校验；
 *       修改成功后强制改密标志由DAO自动清除。
 */
bool UserController::forceChangePassword(int userId, const QString& newPwd) {
    User u = m_dao.findUserById(userId);
    if (u.userId == 0) return false;
    if (newPwd == SC::DEFAULT_INIT_PASSWORD) {
        qWarning() << "[forceChangePassword] 新口令不能是初始口令";
        return false;
    }
    QString errorMsg;
    if (!validatePassword(newPwd, errorMsg)) {
        qWarning() << "[forceChangePassword] 密码不合法:" << errorMsg;
        return false;
    }
    QString newSalt = AuthService::generateSalt();
    QString newHash = AuthService::hashPassword(newPwd, newSalt);
    return m_dao.updateUserPassword(userId, newHash, newSalt);
}

/**
 * @brief 管理员重置用户口令
 * @param userId 用户ID
 * @param newPwd 重置后的口令（允许为初始口令123456，由用户下次登录时修改）
 * @param mustChange true=重置后标记下次登录强制修改（默认）；false=不标记
 * @return true=重置成功
 * @note 用户自助"忘记密码"重置时传 false：用户当场已设新口令，无需再强制
 */
bool UserController::resetPassword(int userId, const QString& newPwd, bool mustChange) {
    User u = m_dao.findUserById(userId);
    if (u.userId == 0) return false;
    // 重置口令同样受6-12位约束（初始密码123456为6位，可通过校验）
    QString errorMsg;
    if (!validatePassword(newPwd, errorMsg)) {
        qWarning() << "[resetPassword] 新口令不合法:" << errorMsg;
        return false;
    }
    QString newSalt = AuthService::generateSalt();
    QString newHash = AuthService::hashPassword(newPwd, newSalt);
    // 先写口令再置标志：updateUserPassword 会清标志，顺序不能反
    if (!m_dao.updateUserPassword(userId, newHash, newSalt)) return false;
    if (mustChange) m_dao.setMustChangePwd(userId, 1);
    return true;
}


/**
 * @brief 查询全部部门名称
 * @return 部门名称列表
 */

/**
 * @brief 查询全部可用角色
 * @return 角色标识列表（管理员、普通用户）
 */

/**
 * @brief 校验登录账号格式（新建账户口径）
 * @param username 登录账号
 * @return true=为6~12位纯数字（闭区间含边界）
 */
bool UserController::validateUsername(const QString& username) {
    // 新建账户工号为纯数字，6-12位（登录侧防呆用 common::isValidWorkNo 的1-32宽口径，
    // 存量账户存在不足6位的工号，两者用途不同不能混用）
    static QRegularExpression re("^[0-9]{6,12}$");
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
 * @return true=符合强度要求（长度6~12位，闭区间含边界）
 */
bool UserController::validatePassword(const QString& password, QString& errorMsg) {
    if (password.length() < SC::PASSWORD_MIN_LENGTH) {
        errorMsg = QStringLiteral("密码长度至少%1位").arg(SC::PASSWORD_MIN_LENGTH);
        return false;
    }
    if (password.length() > SC::PASSWORD_MAX_LENGTH) {
        errorMsg = QStringLiteral("密码长度不能超过%1位").arg(SC::PASSWORD_MAX_LENGTH);
        return false;
    }
    return true;
}
