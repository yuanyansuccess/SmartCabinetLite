/**
 * @file UserDAO.h
 * @brief 用户数据访问对象，提供用户记录的查询、新增、更新与人脸特征管理接口
 * @author 袁燕
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
    bool         updateFace(int userId, const QString& faceFeature, const QString& faceImagePath = "");
    bool         deleteFace(int userId);
    /// 查询所有已录入人脸的用户
    /// @param activeOnly true=仅启用账户（登录匹配用）；false=全部未注销账户（录入查重用，
    ///                   禁用/待激活账户的人脸同样占用一张脸，防止账户重新启用后一张脸挂两户）
    QJsonArray   getAllFaceFeatures(bool activeOnly = true);
    bool         recordLoginSuccess(int userId, const QString& ip = "");
    // 查询DB中CF格式最大工号，返回下一个可用工号（CF001~CF999）
    QString      generateNextWorkNo();

    // ═══════════════════════════════════════════════
    // 实体类API（Controller层使用）——从dao/UserDAO合并
    // ═══════════════════════════════════════════════
    User         findUserById(int userId);
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
    bool         setMustChangePwd(int userId, int mustChange);  // 置/清强制改密标志（1=强制，0=不强制）

    static User fromQuery(const QSqlQuery& q);

private:
    /// 解析部门ID：deptId>0 直接采用；否则按部门名称反查 sys_department，查不到返回 0
    int resolveDeptId(int deptId, const QString& deptName);
};

} // namespace db
