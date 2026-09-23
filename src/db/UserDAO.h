/**
 * @file UserDAO.h
 * @brief 用户数据访问对象 — db/目录统一namespace db，合并QJsonObject API + 实体类API
 * @author 袁燕
 * @修改说明 V6.9 2026-06-24 合并dao/UserDAO的实体类API到此文件，统一namespace db管理
 */
#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QSqlDatabase>
#include <QSqlQuery>
#include "model/User.h"
#include "BaseDAO.h"

namespace db {

class UserDAO : public BaseDAO {
public:
    UserDAO() = default;

    // ═══════════════════════════════════════════════
    // QJsonObject API（Service层使用）
    // ═══════════════════════════════════════════════
    QJsonObject  findByUsername(const QString& username);
    QJsonObject  findById(int userId);
    QJsonObject  findAll(const QString& keyword, const QString& role,
                         const QString& status, const QString& department,
                         int page, int pageSize);
    int          insert(const QJsonObject& userInfo);
    bool         update(int userId, const QJsonObject& updates);
    bool         softDelete(int userId);
    bool         updatePassword(int userId, const QString& passwordHash, const QString& passwordSalt);
    bool         updateFace(int userId, const QString& faceFeature, const QString& faceImagePath = "");
    bool         deleteFace(int userId);
    QJsonArray   getAllFaceFeatures();
    bool         updateStatus(int userId, const QString& status);
    bool         recordLoginSuccess(int userId, const QString& ip = "");
    QStringList  getDistinctDepartments();
    // 查询DB中CF格式最大工号，返回下一个可用工号（CF001~CF999）
    QString      generateNextWorkNo();

    // ═══════════════════════════════════════════════
    // 实体类API（Controller层使用）——从dao/UserDAO合并
    // ═══════════════════════════════════════════════
    User         findUserById(int userId);
    User         findUserByUsername(const QString& username);
    User         findUserByWorkNo(const QString& workNo);
    QList<User>  findAllUsers(int page = 1, int pageSize = 20, const QString& keyword = "",
                              const QString& dept = "", const QString& status = "", const QString& role = "");
    int          countUsers(const QString& keyword = "", const QString& dept = "",
                            const QString& status = "", const QString& role = "");
    int          insertUser(const User& user);
    bool         updateUser(const User& user);
    bool         deleteUserById(int userId);
    bool         updateUserStatus(int userId, const QString& status);
    bool         updateFaceFeature(int userId, const QString& feature);
    bool         updateUserPassword(int userId, const QString& hash, const QString& salt);
    bool         updateLastLogin(int userId);

    static User fromQuery(const QSqlQuery& q);
};

} // namespace db
