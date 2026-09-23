# UserManagementPage 业务逻辑文档

**作者**: 袁燕  
**日期**: 2026-06-20  
**目标**: 1:1复刻Web前端UserManagement.vue

## 页面概述

人员管理页面（仅管理员），支持：
1. **用户列表**：工号/姓名/部门/角色/状态
2. **搜索筛选**：关键词+部门多选+角色+状态
3. **新增/编辑/删除用户**
4. **启用/禁用/锁定操作**
5. **人脸录入状态显示**

## 数据结构

### 1. 用户信息（User）
```cpp
struct User {
    int userId;            // 用户ID
    QString username;      // 用户名
    QString realName;      // 真实姓名
    QString workNo;        // 工号
    QString department;     // 部门
    QString role;          // 角色（admin/user）
    QString phone;         // 电话
    bool faceEnrolled;    // 是否录入人脸
    QString createTime;     // 创建时间
    QString status;        // 状态（active/disabled/locked）
};
```

### 2. 部门信息（Department）
```cpp
struct Department {
    int id;
    QString name;
};
```

---

## Service接口说明

### 1. 获取用户列表
**Service**: `AuthService::getUsers(params)`
**对应Web API**: `GET /api/users`

**请求**:
```cpp
QJsonObject params;
params["keyword"] = "张三";  // 关键词搜索
params["department"] = "技术部";  // 部门筛选
params["role"] = "admin";       // 角色筛选
params["status"] = "active";     // 状态筛选
params["page"] = 1;
params["pageSize"] = 20;

ApiResponse getUsers(const QJsonObject& params);
```

**响应**:
```json
{
    "success": true,
    "data": [
        {
            "userId": 1,
            "username": "admin",
            "realName": "管理员",
            "workNo": "CF001",
            "department": "技术部",
            "role": "admin",
            "phone": "13800138000",
            "faceEnrolled": true,
            "createTime": "2026-06-20 10:00:00",
            "status": "active"
        }
    ],
    "total": 50,
    "page": 1,
    "pageSize": 20
}
```

---

### 2. 新增用户
**Service**: `AuthService::addUser(data)`
**对应Web API**: `POST /api/users`

**请求**:
```cpp
QJsonObject data;
data["username"] = "newuser";
data["password"] = "123456";
data["realName"] = "新员工";
data["workNo"] = "CF100";
data["department"] = "技术部";
data["role"] = "user";
data["phone"] = "13900139000";

ApiResponse addUser(const QJsonObject& data);
```

**响应**:
```json
{
    "success": true,
    "data": {
        "userId": 101,
        "message": "用户创建成功"
    }
}
```

---

### 3. 更新用户
**Service**: `AuthService::updateUser(id, data)`
**对应Web API**: `PUT /api/users/{id}`

**请求**:
```cpp
int userId = 101;
QJsonObject data;
data["realName"] = "更新姓名";
data["department"] = "研发部";
data["phone"] = "13900139999";

ApiResponse updateUser(int id, const QJsonObject& data);
```

---

### 4. 删除用户
**Service**: `AuthService::deleteUser(id, operatorId)`
**对应Web API**: `DELETE /api/users/{id}`

**请求**:
```cpp
int userId = 101;
int operatorId = 1;  // 操作人ID（当前登录用户）

ApiResponse deleteUser(int id, int operatorId);
```

---

### 5. 设置用户状态
**Service**: `AuthService::setUserStatus(userId, status, operatorId)`
**对应Web API**: `POST /api/users/{userId}/status`

**请求**:
```cpp
ApiResponse setUserStatus(int userId, const QString& status, int operatorId);

// status可选值：
// - "active": 启用
// - "disabled": 禁用
// - "locked": 锁定
```

---

### 6. 重置密码
**Service**: `AuthService::resetUserPassword(userId, operatorId, newPassword)`
**对应Web API**: `POST /api/users/{userId}/password`

**请求**:
```cpp
ApiResponse resetUserPassword(int userId, int operatorId, const QString& newPassword);
```

---

### 7. 解锁用户
**Service**: `AuthService::unlockUser(userId, operatorId)`
**对应Web API**: `POST /api/users/{userId}/unlock`

**请求**:
```cpp
ApiResponse unlockUser(int userId, int operatorId);
```

---

### 8. 获取部门列表
**Service**: `SettingService::getDepartments()`
**对应Web API**: `GET /api/departments`

**响应**:
```json
{
    "success": true,
    "data": [
        {"id": 1, "name": "技术部"},
        {"id": 2, "name": "研发部"}
    ]
}
```

---

## UI组件映射

| Web组件 | Qt组件 | 说明 |
|---------|--------|------|
| `<el-table>` | `QTableWidget` | 用户表格（9列） |
| `<el-input>` | `QLineEdit` | 关键词搜索 |
| `<el-select>` | `QComboBox` | 部门/角色/状态下拉 |
| `<el-dialog>` | `QDialog` | 新增/编辑弹窗 |
| `<el-button>` | `QPushButton` | 操作按钮 |
| `<el-switch>` | `QPushButton`（自定义） | 启用/禁用切换 |

---

## 业务逻辑流程

### 流程1：加载用户列表
1. 页面显示时，调用`getUsers(params)`
2. 填充表格：工号/姓名/部门/角色/电话/人脸/创建时间/状态/操作
3. 支持分页

### 流程2：搜索筛选
1. 用户输入关键词/选择部门/角色/状态
2. 点击"搜索"按钮
3. 重新调用`getUsers(params)`刷新表格

### 流程3：新增用户
1. 点击"新增"按钮，弹出对话框
2. 输入用户信息（用户名/密码/姓名/工号/部门/角色/电话）
3. 点击"确定"，调用`addUser(data)`
4. 成功后刷新表格

### 流程4：编辑用户
1. 点击操作列的"编辑"按钮
2. 弹出对话框，加载用户信息
3. 修改信息后点击"确定"，调用`updateUser(id, data)`
4. 成功后刷新表格

### 流程5：删除用户
1. 点击操作列的"删除"按钮
2. 弹出确认对话框
3. 确认后调用`deleteUser(id, operatorId)`
4. 成功后刷新表格

### 流程6：设置用户状态（启用/禁用/锁定）
1. 点击操作列的"启用"/"禁用"/"锁定"按钮
2. 调用`setUserStatus(userId, status, operatorId)`
3. 成功后刷新表格

---

## 信号槽设计

### AuthService信号
```cpp
// 用户列表加载完成
void usersLoaded(const QJsonArray& users, int total);

// 用户操作成功
void userOperationSuccess(const QString& message);

// 用户操作失败
void userOperationFailed(const QString& errorMessage);
```

### UserManagementPage槽函数
```cpp
// 处理用户列表加载完成
void onUsersLoaded(const QJsonArray& users, int total);

// 处理用户操作成功
void onUserOperationSuccess(const QString& message);

// 处理用户操作失败
void onUserOperationFailed(const QString& errorMessage);
```

---

## 关键代码片段

### 1. 加载用户列表
```cpp
void UserManagementPage::loadUsers() {
    QJsonObject params;
    params["keyword"] = m_searchEdit->text();
    params["department"] = m_deptCombo->currentText();
    params["role"] = m_roleCombo->currentText();
    params["status"] = m_statusCombo->currentText();
    params["page"] = m_currentPage;
    params["pageSize"] = m_pageSize;
    
    ApiResponse response = m_authService->getUsers(params);
    if (response.success) {
        QJsonArray users = response.data["data"].toArray();
        int total = response.data["total"].toInt();
        updateTable(users, total);
    } else {
        QMessageBox::warning(this, "错误", response.errorMessage);
    }
}
```

### 2. 新增用户
```cpp
void UserManagementPage::onAddButtonClicked() {
    // 显示新增对话框
    if (m_userDialog->exec() == QDialog::Accepted) {
        QJsonObject data;
        data["username"] = m_dlgUsernameEdit->text();
        data["password"] = m_dlgPasswordEdit->text();
        data["realName"] = m_dlgRealNameEdit->text();
        data["workNo"] = m_dlgWorkNoEdit->text();
        data["department"] = m_dlgDeptCombo->currentText();
        data["role"] = m_dlgRoleCombo->currentText();
        data["phone"] = m_dlgPhoneEdit->text();
        
        ApiResponse response = m_authService->addUser(data);
        if (response.success) {
            QMessageBox::information(this, "成功", "用户创建成功");
            loadUsers();  // 刷新表格
        } else {
            QMessageBox::warning(this, "失败", response.errorMessage);
        }
    }
}
```

### 3. 删除用户
```cpp
void UserManagementPage::onDeleteButtonClicked(int userId) {
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "确认", "确定要删除此用户吗？",
                                     QMessageBox::Yes|QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        ApiResponse response = m_authService->deleteUser(userId, m_currentUserId);
        if (response.success) {
            QMessageBox::information(this, "成功", "用户删除成功");
            loadUsers();
        } else {
            QMessageBox::warning(this, "失败", response.errorMessage);
        }
    }
}
```

---

## 测试清单

### 功能测试
- [ ] 用户列表显示
- [ ] 关键词搜索
- [ ] 部门/角色/状态筛选
- [ ] 新增用户
- [ ] 编辑用户
- [ ] 删除用户
- [ ] 启用/禁用/锁定用户
- [ ] 重置密码
- [ ] 解锁用户
- [ ] 分页功能

### 数据一致性测试
- [ ] 用户数据与Web版一致
- [ ] 字段名使用驼峰
- [ ] 时间格式统一

### 权限测试
- [ ] 管理员可以访问
- [ ] 普通用户无法访问（跳转）

---

**文档版本**: V1.0  
**最后更新**: 2026-06-20 23:55
