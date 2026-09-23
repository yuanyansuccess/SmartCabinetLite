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

namespace db {


// ════════════════════════════════════════════════════════
// 实体类转换（fromQuery）— 从dao/UserDAO.cpp合并
// ════════════════════════════════════════════════════════
User UserDAO::fromQuery(const QSqlQuery& q) {
    User u;
    u.userId       = q.value("user_id").toInt();
    u.username     = q.value("username").toString();
    u.passwordHash = q.value("password_hash").toString();
    u.passwordSalt = q.value("password_salt").toString();
    u.realName     = q.value("real_name").toString();
    u.workNo       = q.value("work_no").toString();
    u.deptId       = q.value("dept_id").toInt();
    u.department   = q.value("department").toString();
    u.role         = q.value("role").toString();
    u.faceFeature  = q.value("face_feature").toString();
    u.phone        = q.value("phone").toString();
    u.email        = q.value("email").toString();
    u.status       = q.value("status").toString();
    u.lastLoginAt  = q.value("last_login_at").toDateTime();
    u.createdAt    = q.value("created_at").toDateTime();
    u.updatedAt    = q.value("updated_at").toDateTime();
    return u;
}

// ════════════════════════════════════════════════════════
// QJsonObject API 实现（Service层使用）
// ════════════════════════════════════════════════════════

QJsonObject UserDAO::findByUsername(const QString& username) {
    QSqlDatabase db = getDb();
    QSqlQuery query(db);
    query.prepare(
        "SELECT user_id, username, password_hash, password_salt, real_name, "
        "role, work_no, department, phone, email, face_feature, status, "
        "last_login_at, created_at, updated_at "
        "FROM sys_user WHERE username = :username AND status != 'deleted'"
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
    user["workNo"] = query.value("work_no").toString();
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

QJsonObject UserDAO::findById(int userId) {
    QSqlDatabase db = getDb();
    QSqlQuery query(db);
    query.prepare(
        "SELECT user_id, username, password_hash, password_salt, real_name, "
        "role, work_no, department, phone, email, face_feature, status, "
        "last_login_at, created_at, updated_at "
        "FROM sys_user WHERE user_id = :id AND status != 'deleted'"
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
    user["workNo"] = query.value("work_no").toString();
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

QJsonObject UserDAO::findAll(const QString& keyword, const QString& role,
                            const QString& status, const QString& department,
                            int page, int pageSize) {
    QSqlDatabase db = getDb();
    QStringList conditions;
    conditions << "u.status != 'deleted'";
    QMap<QString, QVariant> bindValues;

    if (!keyword.isEmpty()) {
        QString escaped = keyword;
        escaped.replace('\\', "\\\\").replace('%', "\\%").replace('_', "\\_");
        QString like = "%" + escaped + "%";
        conditions << "(u.username LIKE :kw ESCAPE '\\' OR u.real_name LIKE :kw2 ESCAPE '\\' OR u.work_no LIKE :kw3 ESCAPE '\\')";
        bindValues[":kw"] = like; bindValues[":kw2"] = like; bindValues[":kw3"] = like;
    }
    if (!role.isEmpty()) { conditions << "u.role = :role"; bindValues[":role"] = role; }
    if (!status.isEmpty()) { conditions << "u.status = :status"; bindValues[":status"] = status; }
    if (!department.isEmpty()) {
        QStringList depts = department.split(',', Qt::SkipEmptyParts);
        QStringList placeholders;
        for (int i = 0; i < depts.size(); ++i) {
            placeholders << (":dept" + QString::number(i));
            bindValues[":dept" + QString::number(i)] = depts[i].trimmed();
        }
        conditions << "u.department IN (" + placeholders.join(", ") + ")";
    }
    QString where = conditions.join(" AND ");

    QSqlQuery countQuery(db);
    countQuery.prepare("SELECT COUNT(*) FROM sys_user u WHERE " + where);
    for (auto it = bindValues.begin(); it != bindValues.end(); ++it)
        countQuery.bindValue(it.key(), it.value());
    safeExec(countQuery); countQuery.next();
    int total = countQuery.value(0).toInt();

    QSqlQuery dataQuery(db);
    // [2026-06-27] 排序：管理员优先，其次按创建时间倒序
    dataQuery.prepare(
        "SELECT u.user_id, u.username, u.real_name, u.role, u.work_no, u.department, "
        "u.phone, u.email, u.face_feature, u.status, u.last_login_at, u.created_at "
        "FROM sys_user u WHERE " + where + " ORDER BY CASE u.role WHEN 'admin' THEN 0 ELSE 1 END, u.created_at DESC LIMIT :limit OFFSET :offset"
    );
    for (auto it = bindValues.begin(); it != bindValues.end(); ++it)
        dataQuery.bindValue(it.key(), it.value());
    dataQuery.bindValue(":limit", pageSize);
    dataQuery.bindValue(":offset", (page - 1) * pageSize);
    safeExec(dataQuery);

    QJsonArray list;
    while (dataQuery.next()) {
        QJsonObject item;
        item["userId"] = dataQuery.value("user_id").toInt();
        item["username"] = dataQuery.value("username").toString();
        item["realName"] = dataQuery.value("real_name").toString();
        item["role"] = dataQuery.value("role").toString();
        item["workNo"] = dataQuery.value("work_no").toString();
        item["department"] = dataQuery.value("department").toString();
        item["phone"] = dataQuery.value("phone").toString();
        item["email"] = dataQuery.value("email").toString();
        item["faceEnrolled"] = !dataQuery.value("face_feature").toString().isEmpty();
        item["status"] = dataQuery.value("status").toString();
        item["lastLoginAt"] = dataQuery.value("last_login_at").toString();
        item["createdAt"] = dataQuery.value("created_at").toString();
        list.append(item);
    }
    QJsonObject result;
    result["list"] = list; result["total"] = total;
    return result;
}

int UserDAO::insert(const QJsonObject& info) {
    QSqlDatabase db = getDb();
    QSqlQuery query(db);
    query.prepare(
        "INSERT INTO sys_user (username, password_hash, password_salt, real_name, "
        "role, work_no, department, phone, email, status) "
        "VALUES (:un, :ph, :ps, :rn, :role, :wn, :dept, :phone, :email, :st)"
    );
    query.bindValue(":un", info["username"].toString());
    query.bindValue(":ph", info["passwordHash"].toString());
    query.bindValue(":ps", info["passwordSalt"].toString());
    query.bindValue(":rn", info["realName"].toString());
    query.bindValue(":role", info["role"].toString("user"));
    query.bindValue(":wn", info["workNo"].toString(""));
    query.bindValue(":dept", info["department"].toString(""));
    query.bindValue(":phone", info["phone"].toString(""));
    query.bindValue(":email", info["email"].toString(""));
    query.bindValue(":st", info["status"].toString("active"));
    if (!safeExec(query)) { qWarning() << "[UserDAO] insert failed:" << query.lastError().text(); return -1; }
    return query.lastInsertId().toInt();
}

bool UserDAO::update(int userId, const QJsonObject& updates) {
    if (updates.isEmpty()) return false;
    QSqlDatabase db = getDb();
    QSqlQuery query(db);
    QStringList sets; QMap<QString, QVariant> binds;
    QStringList fields = {"username", "real_name", "role", "work_no", "department", "phone", "email", "status"};
    for (const auto& f : fields) {
        if (updates.contains(f)) {
            sets << (f + " = :" + f);
            binds[":" + f] = updates[f].toVariant();
        }
    }
    if (sets.isEmpty()) return false;
    binds[":id"] = userId;
    query.prepare("UPDATE sys_user SET " + sets.join(", ") + " WHERE user_id = :id");
    for (auto it = binds.begin(); it != binds.end(); ++it)
        query.bindValue(it.key(), it.value());
    return safeExec(query);
}

bool UserDAO::softDelete(int userId) {
    QSqlDatabase db = getDb();
    QSqlQuery query(db);
    query.prepare("UPDATE sys_user SET status = 'deleted' WHERE user_id = :id");
    query.bindValue(":id", userId);
    return safeExec(query);
}

bool UserDAO::updatePassword(int userId, const QString& hash, const QString& salt) {
    QSqlDatabase db = getDb();
    QSqlQuery query(db);
    query.prepare("UPDATE sys_user SET password_hash = :h, password_salt = :s WHERE user_id = :id");
    query.bindValue(":h", hash); query.bindValue(":s", salt); query.bindValue(":id", userId);
    return safeExec(query);
}

bool UserDAO::updateFace(int userId, const QString& feature, const QString&) {
    QSqlDatabase db = getDb();
    QSqlQuery query(db);
    query.prepare("UPDATE sys_user SET face_feature = :f WHERE user_id = :id");
    query.bindValue(":f", feature); query.bindValue(":id", userId);
    return safeExec(query);
}

bool UserDAO::deleteFace(int userId) {
    QSqlDatabase db = getDb();
    QSqlQuery query(db);
    query.prepare("UPDATE sys_user SET face_feature = NULL WHERE user_id = :id");
    query.bindValue(":id", userId);
    return safeExec(query);
}

QJsonArray UserDAO::getAllFaceFeatures() {
    QSqlDatabase db = getDb();
    QSqlQuery query(db);
    query.prepare("SELECT user_id, username, real_name, work_no, department, face_feature FROM sys_user "
                  "WHERE face_feature IS NOT NULL AND face_feature != '' AND status = 'active'");
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

bool UserDAO::updateStatus(int userId, const QString& status) {
    QSqlDatabase db = getDb();
    QSqlQuery query(db);
    query.prepare("UPDATE sys_user SET status = :s WHERE user_id = :id");
    query.bindValue(":s", status); query.bindValue(":id", userId);
    return safeExec(query);
}

bool UserDAO::recordLoginSuccess(int userId, const QString&) {
    QSqlDatabase db = getDb();
    QSqlQuery query(db);
    query.prepare("UPDATE sys_user SET last_login_at = :now WHERE user_id = :id");
    query.bindValue(":now", QDateTime::currentDateTime().toString(Qt::ISODate));
    query.bindValue(":id", userId);
    return safeExec(query);
}

QStringList UserDAO::getDistinctDepartments() {
    QStringList result;
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    q.prepare("SELECT DISTINCT department FROM sys_user "
              "WHERE department IS NOT NULL AND department != '' AND status != 'deleted' "
              "ORDER BY department");
    if (safeExec(q)) {
        while (q.next()) {
            QString dept = q.value(0).toString().trimmed();
            if (!dept.isEmpty())
                result.append(dept);
        }
    }
    return result;
}

// ════════════════════════════════════════════════════════
// 实体类API 实现（Controller层使用）— 从dao/UserDAO.cpp合并
// ════════════════════════════════════════════════════════

User UserDAO::findUserById(int userId) {
    QSqlQuery q = query("SELECT * FROM sys_user WHERE user_id = ?", {userId});
    if (q.next()) return fromQuery(q);
    return User();
}

User UserDAO::findUserByUsername(const QString& username) {
    QSqlQuery q = query("SELECT * FROM sys_user WHERE username = ?", {username});
    if (q.next()) return fromQuery(q);
    return User();
}

User UserDAO::findUserByWorkNo(const QString& workNo) {
    QSqlQuery q = query("SELECT * FROM sys_user WHERE work_no = ?", {workNo});
    if (q.next()) return fromQuery(q);
    return User();
}

QList<User> UserDAO::findAllUsers(int page, int pageSize, const QString& keyword,
                                   const QString& dept, const QString& status, const QString& role) {
    static const QString FALLBACK = QString::fromUtf8("\xe6\x9c\xaa\xe5\x88\x86\xe9\x85\x8d");
    QString sql = QString("SELECT u.*, COALESCE(NULLIF(d.dept_name,''), NULLIF(u.department,''), '%1') AS display_dept "
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
            sql += " AND (u.department = ? OR d.dept_name = ?)";
            params << deptList[0].trimmed() << deptList[0].trimmed();
        } else {
            QStringList placeholdersA, placeholdersB;
            for (const auto& d : deptList) {
                placeholdersA << "?";
                params << d.trimmed();
            }
            for (int i = 0; i < deptList.size(); ++i) {
                placeholdersB << "?";
                params << deptList[i].trimmed();
            }
            sql += " AND (u.department IN (" + placeholdersA.join(",") + ") OR d.dept_name IN (" + placeholdersB.join(",") + "))";
        }
    }
    if (!status.isEmpty())  { sql += " AND u.status = ?";      params << status; }
    if (!role.isEmpty())    { sql += " AND u.role = ?";        params << role; }

    // [2026-06-27] 排序：管理员优先，其次按创建时间倒序
    QList<User> list;
    QSqlQuery q = query(paginate(sql, page, pageSize, "CASE WHEN u.role='admin' THEN 0 ELSE 1 END, u.created_at DESC"), params);
    while (q.next()) {
        User u = fromQuery(q);
        u.department = q.value("display_dept").toString();
        list.append(u);
    }
    return list;
}

int UserDAO::countUsers(const QString& keyword, const QString& dept,
                         const QString& status, const QString& role) {
    // [2026-06-26v7 致命修复] countUsers必须排除已删除用户 + 不能使用BaseDAO::count()包装
    //   BaseDAO::count() 会把SQL包成 SELECT COUNT(*) FROM (原SQL) AS _cnt
    //   如果原SQL本身是SELECT COUNT(*)，就会变成双重COUNT，结果永远是1！
    //   修复：直接执行COUNT查询，不使用BaseDAO::count()包装
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
            sql += " AND (u.department = ? OR d.dept_name = ?)";
            params << deptList[0].trimmed() << deptList[0].trimmed();
        } else {
            QStringList placeholdersA, placeholdersB;
            for (const auto& d : deptList) {
                placeholdersA << "?";
                params << d.trimmed();
            }
            for (int i = 0; i < deptList.size(); ++i) {
                placeholdersB << "?";
                params << deptList[i].trimmed();
            }
            sql += " AND (u.department IN (" + placeholdersA.join(",") + ") OR d.dept_name IN (" + placeholdersB.join(",") + "))";
        }
    }
    if (!status.isEmpty()) { sql += " AND u.status = ?";      params << status; }
    if (!role.isEmpty())   { sql += " AND u.role = ?";        params << role; }
    // [2026-06-26v7] 直接执行scalar，不再通过count()包装（避免双重COUNT导致始终返回1）
    return scalar(sql, params).toInt();
}

int UserDAO::insertUser(const User& user) {
    QString sql = "INSERT INTO sys_user (username, password_hash, password_salt, real_name, "
                  "work_no, dept_id, department, role, face_feature, phone, email, status) "
                  "VALUES (?,?,?,?,?,?,?,?,?,?,?,?)";
    // [2026-06-27] dept_id<=0时插入NULL，避免外键约束失败（sys_department中不存在dept_id=0）
    QVariant deptIdVal = (user.deptId > 0) ? QVariant(user.deptId) : QVariant();
    return insertAndGetId(sql, {user.username, user.passwordHash, user.passwordSalt,
                          user.realName, user.workNo, deptIdVal, user.department,
                          user.role, user.faceFeature, user.phone, user.email, user.status});
}

bool UserDAO::updateUser(const User& user) {
    QString sql = "UPDATE sys_user SET real_name=?, work_no=?, dept_id=?, department=?, "
                  "role=?, phone=?, email=?, status=? WHERE user_id=?";
    return execute(sql, {user.realName, user.workNo, user.deptId, user.department,
                   user.role, user.phone, user.email, user.status, user.userId});
}

bool UserDAO::deleteUserById(int userId) {
    return execute("UPDATE sys_user SET status = 'deleted' WHERE user_id = ?", {userId});
}

bool UserDAO::updateUserStatus(int userId, const QString& status) {
    return execute("UPDATE sys_user SET status = ? WHERE user_id = ?", {status, userId});
}

bool UserDAO::updateFaceFeature(int userId, const QString& feature) {
    return execute("UPDATE sys_user SET face_feature = ? WHERE user_id = ?", {feature, userId});
}

bool UserDAO::updateUserPassword(int userId, const QString& hash, const QString& salt) {
    return execute("UPDATE sys_user SET password_hash=?, password_salt=? WHERE user_id=?",
                   {hash, salt, userId});
}

bool UserDAO::updateLastLogin(int userId) {
    return execute("UPDATE sys_user SET last_login_at = CURRENT_TIMESTAMP WHERE user_id = ?", {userId});
}

// 查询DB中CF格式最大工号，返回下一个可用工号（CF001~CF999）
QString UserDAO::generateNextWorkNo()
{
    QSqlDatabase db = getDb();
    QSqlQuery q(db);
    // 先用SQLite兼容的GLOB语法；MySQL 5.7不支持GLOB，会fallback到ORDER BY
    q.prepare("SELECT work_no FROM sys_user WHERE work_no LIKE 'CF%' AND LENGTH(work_no) >= 3 "
              "AND SUBSTR(work_no, 3) GLOB '[0-9]*' ORDER BY CAST(SUBSTR(work_no, 3) AS INTEGER) DESC LIMIT 1");
    if (!safeExec(q) || !q.next()) {
        q.prepare("SELECT work_no FROM sys_user WHERE work_no LIKE 'CF%' AND LENGTH(work_no) >= 3 "
                  "ORDER BY work_no DESC LIMIT 1");
        if (!safeExec(q) || !q.next()) {
            return QStringLiteral("CF001");
        }
    }
    QString maxWorkNo = q.value(0).toString();
    QRegularExpression re("CF(\\d+)", QRegularExpression::CaseInsensitiveOption);
    auto m = re.match(maxWorkNo);
    if (m.hasMatch()) {
        int nextNum = m.captured(1).toInt() + 1;
        if (nextNum > 999) nextNum = 1;
        return QString("CF%1").arg(nextNum, 3, 10, QChar('0'));
    }
    return QStringLiteral("CF001");
}

} // namespace db
