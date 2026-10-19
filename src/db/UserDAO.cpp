/**
 * @file UserDAO.cpp
 * @brief 用户数据访问对象实现 — QJsonObject API + 实体类API，全部参数化查询
 * @author 袁燕
 */
#include "UserDAO.h"
#include "DatabaseManager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QDateTime>
#include <QDebug>
#include "common/Constants.h"

namespace db {

/**
 * @brief 查询结果行转换为用户实体
 * @param q 已定位到有效行的查询结果
 * @return 填充完成的用户实体
 */
User UserDAO::fromQuery(const QSqlQuery& q) {
    User u;
    u.userId       = q.value("user_id").toInt();
    u.username     = q.value("username").toString();
    u.passwordHash = q.value("password_hash").toString();
    u.passwordSalt = q.value("password_salt").toString();
    u.realName     = q.value("real_name").toString();
    u.workNo       = q.value("work_no").toString();
    u.deptId       = q.value("dept_id").toInt();
    // 部门名称来自 SELECT 中 d.dept_name 的别名，用户表本身不存部门名称
    u.department   = q.value("department_name").toString();
    u.role         = q.value("role").toString();
    u.mustChangePwd = q.value("must_change_pwd").toInt();
    u.faceFeature  = q.value("face_feature").toString();
    u.phone        = q.value("phone").toString();
    u.email        = q.value("email").toString();
    u.status       = q.value("status").toString();
    u.lastLoginAt  = q.value("last_login_at").toDateTime();
    u.createdAt    = q.value("created_at").toDateTime();
    u.updatedAt    = q.value("updated_at").toDateTime();
    return u;
}

/**
 * @brief 按部门名称解析部门ID
 * @param deptId 调用方已给出的部门ID，大于0时直接采用
 * @param deptName 部门名称，仅在 deptId 无效时用于反查 sys_department
 * @return 有效的部门ID；名称为空或库中不存在该部门时返回0
 */
int UserDAO::resolveDeptId(int deptId, const QString& deptName) {
    if (deptId > 0) return deptId;
    const QString name = deptName.trimmed();
    if (name.isEmpty()) return 0;
    QSqlQuery q = query("SELECT dept_id FROM sys_department WHERE dept_name = ? LIMIT 1", {name});
    return q.next() ? q.value(0).toInt() : 0;
}

/**
 * @brief 按登录账号查询用户
 * @param username 登录账号
 * @return 用户信息对象；账号不存在时返回空对象
 */
QJsonObject UserDAO::findByUsername(const QString& username) {
    QSqlDatabase db = getDb();
    QSqlQuery query(db);
    query.prepare(
        "SELECT u.user_id, u.username, u.password_hash, u.password_salt, u.real_name, "
        "u.role, u.must_change_pwd, u.work_no, u.dept_id, d.dept_name AS department, "
        "u.phone, u.email, u.face_feature, u.status, "
        "u.last_login_at, u.created_at, u.updated_at "
        "FROM sys_user u "
        "LEFT JOIN sys_department d ON u.dept_id = d.dept_id "
        "WHERE u.username = :username AND u.status != 'deleted'"
    );
    query.bindValue(":username", username);
    if (!safeExec(query) || !query.next()) return QJsonObject();

    QJsonObject user;
    user["userId"] = query.value("user_id").toInt();
    user["username"] = query.value("username").toString();
    user["passwordHash"] = query.value("password_hash").toString();
    user["passwordSalt"] = query.value("password_salt").toString();
    user["realName"] = query.value("real_name").toString();
    user["role"] = query.value("role").toString();
    user["mustChangePwd"] = query.value("must_change_pwd").toInt();
    user["workNo"] = query.value("work_no").toString();
    user["deptId"] = query.value("dept_id").toInt();
    user["department"] = query.value("department").toString();
    user["phone"] = query.value("phone").toString();
    user["email"] = query.value("email").toString();
    user["faceFeature"] = query.value("face_feature").toString();
    user["status"] = query.value("status").toString();
    user["lastLoginAt"] = query.value("last_login_at").toString();
    user["createdAt"] = query.value("created_at").toString();
    user["updatedAt"] = query.value("updated_at").toString();
    return user;
}

/**
 * @brief 按用户ID查询用户
 * @param userId 用户ID
 * @return 用户信息对象；不存在时返回空对象
 */
QJsonObject UserDAO::findById(int userId) {
    QSqlDatabase db = getDb();
    QSqlQuery query(db);
    query.prepare(
        "SELECT u.user_id, u.username, u.password_hash, u.password_salt, u.real_name, "
        "u.role, u.must_change_pwd, u.work_no, u.dept_id, d.dept_name AS department, "
        "u.phone, u.email, u.face_feature, u.status, "
        "u.last_login_at, u.created_at, u.updated_at "
        "FROM sys_user u "
        "LEFT JOIN sys_department d ON u.dept_id = d.dept_id "
        "WHERE u.user_id = :id AND u.status != 'deleted'"
    );
    query.bindValue(":id", userId);
    if (!safeExec(query) || !query.next()) return QJsonObject();

    QJsonObject user;
    user["userId"] = query.value("user_id").toInt();
    user["username"] = query.value("username").toString();
    user["passwordHash"] = query.value("password_hash").toString();
    user["passwordSalt"] = query.value("password_salt").toString();
    user["realName"] = query.value("real_name").toString();
    user["role"] = query.value("role").toString();
    user["mustChangePwd"] = query.value("must_change_pwd").toInt();
    user["workNo"] = query.value("work_no").toString();
    user["deptId"] = query.value("dept_id").toInt();
    user["department"] = query.value("department").toString();
    user["phone"] = query.value("phone").toString();
    user["email"] = query.value("email").toString();
    user["faceFeature"] = query.value("face_feature").toString();
    user["status"] = query.value("status").toString();
    user["lastLoginAt"] = query.value("last_login_at").toString();
    user["createdAt"] = query.value("created_at").toString();
    user["updatedAt"] = query.value("updated_at").toString();
    return user;
}

/**
 * @brief 更新用户人脸特征
 * @param userId 用户ID
 * @param feature 人脸特征串（128维向量的逗号分隔文本）
 * @return true=写入成功
 */
bool UserDAO::updateFace(int userId, const QString& feature, const QString&) {
    QSqlDatabase db = getDb();
    QSqlQuery query(db);
    query.prepare("UPDATE sys_user SET face_feature = :f WHERE user_id = :id");
    query.bindValue(":f", feature); query.bindValue(":id", userId);
    return safeExec(query);
}

/**
 * @brief 清除用户人脸特征
 * @param userId 用户ID
 * @return true=清除成功
 */
bool UserDAO::deleteFace(int userId) {
    QSqlDatabase db = getDb();
    QSqlQuery query(db);
    query.prepare("UPDATE sys_user SET face_feature = NULL WHERE user_id = :id");
    query.bindValue(":id", userId);
    return safeExec(query);
}

/**
 * @brief 查询全部已录入人脸特征的用户
 * @return 元素含 userId、realName、faceFeature 的数组；无人脸数据时返回空数组
 */
QJsonArray UserDAO::getAllFaceFeatures(bool activeOnly) {
    QSqlDatabase db = getDb();
    QSqlQuery query(db);
    // activeOnly=true：仅启用账户（登录匹配用，禁用账户由登录流程的状态检查单独拦截）；
    // false：全部未注销账户（人脸录入查重用，禁用账户的人脸同样参与通盘比对）
    QString sql = "SELECT u.user_id, u.username, u.real_name, u.work_no, "
                  "d.dept_name AS department, u.face_feature "
                  "FROM sys_user u LEFT JOIN sys_department d ON u.dept_id = d.dept_id "
                  "WHERE u.face_feature IS NOT NULL AND u.face_feature != ''";
    sql += activeOnly ? " AND u.status = :st" : " AND u.status != :st";
    query.prepare(sql);
    query.bindValue(":st", activeOnly ? SC::USER_ACTIVE : SC::USER_DELETED);
    safeExec(query);
    QJsonArray arr;
    while (query.next()) {
        QJsonObject item;
        item["userId"] = query.value("user_id").toInt();
        item["username"] = query.value("username").toString();
        item["realName"] = query.value("real_name").toString();
        item["workNo"] = query.value("work_no").toString();
        item["department"] = query.value("department").toString();
        item["faceFeature"] = query.value("face_feature").toString();
        arr.append(item);
    }
    return arr;
}

/**
 * @brief 记录用户登录成功
 * @param userId 用户ID
 * @param loginIp 登录来源地址
 * @return true=记录成功
 */
bool UserDAO::recordLoginSuccess(int userId, const QString&) {
    QSqlDatabase db = getDb();
    QSqlQuery query(db);
    query.prepare("UPDATE sys_user SET last_login_at = :now WHERE user_id = :id");
    query.bindValue(":now", QDateTime::currentDateTime().toString(Qt::ISODate));
    query.bindValue(":id", userId);
    return safeExec(query);
}

/**
 * @brief 按用户ID查询用户实体
 * @param userId 用户ID
 * @return 用户实体；不存在时返回默认构造的空实体
 */
User UserDAO::findUserById(int userId) {
    QSqlQuery q = query("SELECT u.*, d.dept_name AS department_name FROM sys_user u "
                        "LEFT JOIN sys_department d ON u.dept_id = d.dept_id WHERE u.user_id = ?", {userId});
    if (q.next()) return fromQuery(q);
    return User();
}

/**
 * @brief 按工号查询用户实体
 * @param workNo 工号
 * @return 用户实体；不存在时返回默认构造的空实体
 */
User UserDAO::findUserByWorkNo(const QString& workNo) {
    QSqlQuery q = query("SELECT u.*, d.dept_name AS department_name FROM sys_user u "
                        "LEFT JOIN sys_department d ON u.dept_id = d.dept_id WHERE u.work_no = ?", {workNo});
    if (q.next()) return fromQuery(q);
    return User();
}

/**
 * @brief 分页查询用户实体列表
 * @param page 页码，从1开始
 * @param pageSize 每页条数
 * @param keyword 关键字，为空不过滤
 * @param dept 部门名称，为空不过滤
 * @param status 状态，为空不过滤
 * @param role 角色，为空不过滤
 * @return 管理员优先、其次按创建时间倒序的用户实体列表
 */
QList<User> UserDAO::findAllUsers(int page, int pageSize, const QString& keyword,
                                   const QString& dept, const QString& status, const QString& role) {
    static const QString FALLBACK = QString::fromUtf8("\xe6\x9c\xaa\xe5\x88\x86\xe9\x85\x8d");
    QString sql = QString("SELECT u.*, COALESCE(NULLIF(d.dept_name,''), '%1') AS department_name "
                  "FROM sys_user u "
                  "LEFT JOIN sys_department d ON u.dept_id = d.dept_id WHERE 1=1").arg(FALLBACK);
    QVariantList params;
    if (!keyword.isEmpty()) {
        sql += " AND (u.real_name LIKE ? OR u.work_no LIKE ? OR u.username LIKE ?)";
        QString kw = "%" + keyword + "%";
        params << kw << kw << kw;
    }
    if (!dept.isEmpty()) {
        QStringList deptList = dept.split(",", Qt::SkipEmptyParts);
        if (deptList.size() == 1) {
            sql += " AND d.dept_name = ?";
            params << deptList[0].trimmed();
        } else {
            QStringList placeholders;
            for (const auto& name : deptList) {
                placeholders << "?";
                params << name.trimmed();
            }
            sql += " AND d.dept_name IN (" + placeholders.join(",") + ")";
        }
    }
    if (!status.isEmpty())  { sql += " AND u.status = ?";      params << status; }
    if (!role.isEmpty())    { sql += " AND u.role = ?";        params << role; }

    // 排序：管理员优先，其次按创建时间倒序
    QList<User> list;
    QSqlQuery q = query(paginate(sql, page, pageSize, "CASE WHEN u.role='admin' THEN 0 ELSE 1 END, u.created_at DESC"), params);
    while (q.next()) {
        // 部门名称由 fromQuery 统一读取 department_name 别名
        User u = fromQuery(q);
        list.append(u);
    }
    return list;
}

/**
 * @brief 统计符合条件的用户数量
 * @param keyword 关键字，为空不过滤
 * @param dept 部门名称，为空不过滤
 * @param status 状态，为空不过滤
 * @param role 角色，为空不过滤
 * @return 用户数量；始终排除状态为 deleted 的用户
 */
int UserDAO::countUsers(const QString& keyword, const QString& dept,
                         const QString& status, const QString& role) {
    // countUsers需排除 status='deleted' 的用户，且不能使用BaseDAO::count()包装
    // BaseDAO::count() 会把SQL包成 SELECT COUNT(*) FROM (原SQL) AS _cnt
    // 如果原SQL本身是SELECT COUNT(*)，就会变成双重COUNT，结果永远是1！
    // 注意：直接执行COUNT查询，不使用BaseDAO::count()包装
    QString sql = "SELECT COUNT(*) FROM sys_user u "
                  "LEFT JOIN sys_department d ON u.dept_id = d.dept_id "
                  "WHERE u.status != 'deleted'";
    QVariantList params;
    if (!keyword.isEmpty()) {
        sql += " AND (u.real_name LIKE ? OR u.work_no LIKE ? OR u.username LIKE ?)";
        QString kw = "%" + keyword + "%";
        params << kw << kw << kw;
    }
    if (!dept.isEmpty()) {
        QStringList deptList = dept.split(",", Qt::SkipEmptyParts);
        if (deptList.size() == 1) {
            sql += " AND d.dept_name = ?";
            params << deptList[0].trimmed();
        } else {
            QStringList placeholders;
            for (const auto& name : deptList) {
                placeholders << "?";
                params << name.trimmed();
            }
            sql += " AND d.dept_name IN (" + placeholders.join(",") + ")";
        }
    }
    if (!status.isEmpty()) { sql += " AND u.status = ?";      params << status; }
    if (!role.isEmpty())   { sql += " AND u.role = ?";        params << role; }
    // 直接执行scalar，不通过count()包装（避免双重COUNT导致始终返回1）
    return scalar(sql, params).toInt();
}

/**
 * @brief 新增用户实体
 * @param user 待写入的用户实体
 * @return 新记录的用户ID；写入失败返回 -1
 */
int UserDAO::insertUser(const User& user) {
    // 用户表只保存部门ID：优先用 deptId，缺失时按部门名称反查 sys_department
    const int deptId = resolveDeptId(user.deptId, user.department);
    // dept_id<=0时插入NULL，避免外键约束失败（sys_department中不存在dept_id=0）
    QVariant deptIdVal = (deptId > 0) ? QVariant(deptId) : QVariant();
    QString sql = "INSERT INTO sys_user (username, password_hash, password_salt, real_name, "
                  "work_no, dept_id, role, must_change_pwd, face_feature, phone, email, status) "
                  "VALUES (?,?,?,?,?,?,?,?,?,?,?,?)";
    return insertAndGetId(sql, {user.username, user.passwordHash, user.passwordSalt,
                          user.realName, user.workNo, deptIdVal,
                          user.role, user.mustChangePwd, user.faceFeature, user.phone,
                          user.email, user.status});
}

/**
 * @brief 按实体更新用户信息
 * @param user 含用户ID与待更新字段的实体
 * @return true=更新成功
 */
bool UserDAO::updateUser(const User& user) {
    // 用户表只保存部门ID：优先用 deptId，缺失时按部门名称反查，
    // 避免把无效值0写库触发外键约束fk_user_dept导致保存失败；解析不到写NULL
    const int deptId = resolveDeptId(user.deptId, user.department);
    QVariant deptIdVal = (deptId > 0) ? QVariant(deptId) : QVariant();
    QString sql = "UPDATE sys_user SET real_name=?, work_no=?, dept_id=?, "
                  "role=?, phone=?, email=?, status=? WHERE user_id=?";
    return execute(sql, {user.realName, user.workNo, deptIdVal,
                   user.role, user.phone, user.email, user.status, user.userId});
}

/**
 * @brief 逻辑删除用户：状态置为 deleted
 * @param userId 用户ID
 * @return true=状态已更新
 */
bool UserDAO::deleteUserById(int userId) {
    return execute("UPDATE sys_user SET status = 'deleted' WHERE user_id = ?", {userId});
}

/**
 * @brief 更新用户状态
 * @param userId 用户ID
 * @param status 目标状态，取 SC::USER_* 常量
 * @return true=更新成功
 */
bool UserDAO::updateUserStatus(int userId, const QString& status) {
    return execute("UPDATE sys_user SET status = ? WHERE user_id = ?", {status, userId});
}

/**
 * @brief 更新用户人脸特征
 * @param userId 用户ID
 * @param feature 人脸特征串
 * @return true=更新成功
 */
bool UserDAO::updateFaceFeature(int userId, const QString& feature) {
    return execute("UPDATE sys_user SET face_feature = ? WHERE user_id = ?", {feature, userId});
}

/**
 * @brief 更新用户口令哈希与盐值，并清除强制改密标志
 * @param userId 用户ID
 * @param hash 口令哈希
 * @param salt 口令盐值
 * @return true=更新成功
 * @note 改密成功即视为用户已自设新口令，must_change_pwd 同步清 0，避免重复弹强制改密窗
 */
bool UserDAO::updateUserPassword(int userId, const QString& hash, const QString& salt) {
    // 位置占位符 ?：BaseDAO::execute 内部按 params 顺序做 bindValue(i)，
    // 只支持位置占位符；命名占位符需走 query()+手动 bindValue 路径
    return execute("UPDATE sys_user SET password_hash=?, password_salt=?, must_change_pwd=0 WHERE user_id=?",
                   {hash, salt, userId});
}

/**
 * @brief 设置强制改密标志（新建用户/管理员重置密码后置 1，用户改密成功后由改密方法清 0）
 * @param userId 用户ID
 * @param mustChange 1=下次登录必须修改初始密码；0=不强制
 * @return true=更新成功
 */
bool UserDAO::setMustChangePwd(int userId, int mustChange) {
    return execute("UPDATE sys_user SET must_change_pwd = ? WHERE user_id = ?", {mustChange, userId});
}

// 查询DB中最大数字工号，返回下一个可用工号（001起步，3位补零，超999自然进位4位）
QString UserDAO::generateNextWorkNo()
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    // 工号为纯数字格式，数字项在 C++ 侧用正则过滤后取最大值
    // 不依赖数据库特有函数，保证跨版本 MySQL 行为一致
    q.prepare("SELECT work_no FROM sys_user WHERE status != 'deleted'");
    if (!safeExec(q)) return QStringLiteral("000001");
    int maxNum = 0;
    QRegularExpression re("^\\d+$");
    while (q.next()) {
        const QString workNo = q.value(0).toString().trimmed();
        if (re.match(workNo).hasMatch()) {
            maxNum = qMax(maxNum, workNo.toInt());
        }
    }
    // 6位补零输出，与新建账户6-12位工号规则衔接（超过6位自然进位）
    return QString("%1").arg(maxNum + 1, 6, 10, QChar('0'));
}

} // namespace db
