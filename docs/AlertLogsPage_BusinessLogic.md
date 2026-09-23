# AlertLogsPage 业务逻辑文档

**作者**: 袁燕  
**日期**: 2026-06-20  
**目标**: 1:1复刻Web前端AlertLogs.vue

## 页面概述

告警日志页面，支持：
1. **告警列表**：类型/级别/关键词筛选
2. **告警确认/忽略**
3. **声光报警状态管理**
4. **告警统计**：严重/警告/提示/已处理

## 数据结构

### 1. 告警信息（Alert）
```cpp
struct Alert {
    int id;                 // 告警ID
    QString type;           // 类型（overdue/stranger/unauthorized）
    QString level;          // 级别（critical/warning/info）
    QString content;        // 告警内容
    QString time;          // 告警时间
    bool resolved;          // 是否已处理
    int handlerId;         // 处理人ID
    QString handlerName;    // 处理人姓名
    QString resolveTime;     // 处理时间
    QString resolveMethod;   // 处理方式（confirm/ignore）
};
```

---

## Service接口说明

### 1. 获取告警列表
**Service**: `AlertService::getAlerts(params)`
**对应Web API**: `GET /api/alerts`

**请求**:
```cpp
QJsonObject params;
params["type"] = "overdue";     // 类型筛选
params["level"] = "warning";     // 级别筛选
params["keyword"] = "逾期";    // 关键词搜索
params["resolved"] = false;       // 是否已处理
params["page"] = 1;
params["pageSize"] = 20;

ApiResponse getAlerts(const QJsonObject& params);
```

**响应**:
```json
{
    "success": true,
    "data": [
        {
            "id": 1,
            "type": "overdue",
            "level": "warning",
            "content": "工具逾期归还",
            "time": "2026-06-20 14:30:00",
            "resolved": false,
            "handlerId": 0,
            "handlerName": "",
            "resolveTime": "",
            "resolveMethod": ""
        }
    ],
    "total": 50,
    "page": 1,
    "pageSize": 20
}
```

---

### 2. 确认告警
**Service**: `AlertService::confirmAlert(alertId, handlerId)`
**对应Web API**: `POST /api/alerts/{alertId}/confirm`

**请求**:
```cpp
int alertId = 1;
int handlerId = 1;  // 当前登录用户ID

ApiResponse confirmAlert(int alertId, int handlerId);
```

**响应**:
```json
{
    "success": true,
    "data": {
        "message": "告警已确认"
    }
}
```

---

### 3. 忽略告警
**Service**: `AlertService::ignoreAlert(alertId, handlerId)`
**对应Web API**: `POST /api/alerts/{alertId}/ignore`

**请求**:
```cpp
ApiResponse ignoreAlert(int alertId, int handlerId);
```

---

### 4. 解除所有警报
**Service**: `AlertService::dismissAllAlerts()`
**对应Web API**: `POST /api/alerts/dismiss-all`

**请求**:
```cpp
ApiResponse dismissAllAlerts();
```

---

### 5. 获取告警统计
**Service**: `AlertService::getAlertStats()`
**对应Web API**: 无（前端计算）

**响应**:
```json
{
    "success": true,
    "data": {
        "total": 50,
        "critical": 5,
        "warning": 10,
        "info": 35,
        "resolved": 30
    }
}
```

---

## UI组件映射

| Web组件 | Qt组件 | 说明 |
|---------|--------|------|
| `<el-table>` | `QTableWidget` | 告警表格（8列） |
| `<el-select>` | `QComboBox` | 类型/级别筛选 |
| `<el-input>` | `QLineEdit` | 关键词搜索 |
| `<el-button>` | `QPushButton` | 确认/忽略/解除所有 |
| `<el-pagination>` | 自定义 | 分页组件 |

---

## 业务逻辑流程

### 流程1：加载告警列表
1. 页面显示时，调用`getAlerts(params)`
2. 支持筛选：类型/级别/关键词/处理状态
3. 填充表格：类型/级别/内容/时间/状态/操作

### 流程2：确认告警
1. 用户点击"确认"按钮
2. 调用`confirmAlert(alertId, handlerId)`
3. 成功后刷新表格

### 流程3：忽略告警
1. 用户点击"忽略"按钮
2. 调用`ignoreAlert(alertId, handlerId)`
3. 成功后刷新表格

### 流程4：解除所有警报
1. 用户点击"解除所有警报"按钮
2. 弹出确认对话框
3. 确认后调用`dismissAllAlerts()`
4. 成功后刷新表格

---

## 信号槽设计

### AlertService信号
```cpp
// 告警列表加载完成
void alertsLoaded(const QJsonArray& alerts, int total);

// 告警操作成功
void alertOperationSuccess(const QString& message);

// 告警操作失败
void alertOperationFailed(const QString& errorMessage);
```

### AlertLogsPage槽函数
```cpp
// 处理告警列表加载完成
void onAlertsLoaded(const QJsonArray& alerts, int total);

// 处理告警操作成功
void onAlertOperationSuccess(const QString& message);

// 处理告警操作失败
void onAlertOperationFailed(const QString& errorMessage);
```

---

## 关键代码片段

### 1. 加载告警列表
```cpp
void AlertLogsPage::loadAlerts() {
    QJsonObject params;
    params["type"] = m_typeCombo->currentData().toString();
    params["level"] = m_levelCombo->currentData().toString();
    params["keyword"] = m_keywordEdit->text();
    params["resolved"] = m_resolvedCombo->currentData().toBool();
    params["page"] = m_currentPage;
    params["pageSize"] = m_pageSize;
    
    ApiResponse response = m_alertService->getAlerts(params);
    if (response.success) {
        QJsonArray alerts = response.data["data"].toArray();
        int total = response.data["total"].toInt();
        updateTable(alerts, total);
    } else {
        QMessageBox::warning(this, "错误", response.errorMessage);
    }
}
```

### 2. 确认告警
```cpp
void AlertLogsPage::onConfirmButtonClicked(int alertId) {
    ApiResponse response = m_alertService->confirmAlert(alertId, m_currentUserId);
    if (response.success) {
        QMessageBox::information(this, "成功", "告警已确认");
        loadAlerts();  // 刷新表格
    } else {
        QMessageBox::warning(this, "失败", response.errorMessage);
    }
}
```

### 3. 忽略告警
```cpp
void AlertLogsPage::onIgnoreButtonClicked(int alertId) {
    ApiResponse response = m_alertService->ignoreAlert(alertId, m_currentUserId);
    if (response.success) {
        QMessageBox::information(this, "成功", "告警已忽略");
        loadAlerts();
    } else {
        QMessageBox::warning(this, "失败", response.errorMessage);
    }
}
```

---

## 测试清单

### 功能测试
- [ ] 告警列表显示
- [ ] 类型/级别/关键词筛选
- [ ] 确认告警
- [ ] 忽略告警
- [ ] 解除所有警报
- [ ] 分页功能

### 数据一致性测试
- [ ] 告警数据与Web版一致
- [ ] 字段名使用驼峰
- [ ] 时间格式统一

### 错误处理测试
- [ ] 告警加载失败
- [ ] 确认/忽略失败

---

**文档版本**: V1.0  
**最后更新**: 2026-06-20 23:58
