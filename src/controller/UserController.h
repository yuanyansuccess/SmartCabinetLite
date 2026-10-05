/**
 * @file UserController.h
 * @brief 用户管理控制层：用户增删改、启用禁用与密码维护
 * @author 袁燕
 */
#pragma once
#include <QObject>
#include "model/User.h"
#include "db/UserDAO.h"

// 分层定位：Controller 负责用户读查询与密码/状态等管理动作；
// 人脸特征比对与登录鉴权走 services/FaceRecognitionService 与 AuthService。
class UserController : public QObject {
    Q_OBJECT
public:
    explicit UserController(QObject* parent = nullptr);

    struct PageResult { QList<User> list; int total = 0; int page = 1; int pageSize = 20; };

    PageResult  getUserList(int page, int pageSize, const QString& keyword = "",
                            const QString& dept = "", const QString& status = "", const QString& role = "");
    User        getUserById(int userId);
    int         createUser(const User& user, const QString& password);
    bool        updateUser(const User& user);
    bool        deleteUser(int userId);
    bool        setUserStatus(int userId, const QString& status);
    bool        changePassword(int userId, const QString& oldPwd, const QString& newPwd);
    bool        resetPassword(int userId, const QString& newPwd);
    bool        enrollFace(int userId, const QString& faceFeature);
    bool        deleteFace(int userId);
    bool        hasFaceEnrolled(int userId);
    QStringList allDepartments();
    QStringList allRoles();

signals:
    void userCreated(int userId);
    void userUpdated(int userId);
    void userDeleted(int userId);
    void userStatusChanged(int userId, const QString& oldStatus, const QString& newStatus);
    void faceEnrolled(int userId);

private:
    db::UserDAO m_dao;

    bool validateUsername(const QString& username);
    bool validateWorkNo(const QString& workNo, int excludeUserId = 0);
    bool validatePassword(const QString& password, QString& errorMsg);
};
