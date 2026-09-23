#pragma once
// 作者：袁燕  智能柜Qt Widget 2.0  用户管理业务控制层
// 日期：2026-06-21  功能：用户CRUD、状态管理、人脸录入/删除、密码管理
// [V6.9 2026-06-24] 统一到db/目录，使用namespace db::UserDAO
#include <QObject>
#include "model/User.h"
#include "db/UserDAO.h"

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
