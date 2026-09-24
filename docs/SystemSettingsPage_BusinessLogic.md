# SystemSettingsPage 业务逻辑文档

**作者**: 袁燕  
**日期**: 2026-06-20  
**目标**: 1:1复刻Web前端SystemSettings.vue

## 页面概述

系统设置页面（仅管理员），支持：
1. **网络配置**：IP/子网掩码/网关/DNS/网口速率/组网模式
2. **告警参数**：蜂鸣器音量/LED灯/逾期阈值/柜门超时/视觉告警/断电告警
3. **借还设置**：最大借出数量/借用期限/缓冲时间/手动开锁/亮度/锁屏时间/人脸灵敏度
4. **备份管理**：自动备份/周期/存储路径/断网缓存
5. **系统操作**：恢复出厂/重启/清除日志/软件升级

## 数据结构

### 1. 系统设置（SystemSettings）
```cpp
struct SystemSettings {
    // 网络配置
    QString ipAddress;
    QString subnetMask;
    QString gateway;
    QString dns;
    QString networkSpeed;  // 10M/100M/1000M
    QString networkMode;   // 单机/组网
    
    // 告警参数
    int buzzerVolume;        // 蜂鸣器音量（0-100）
    bool ledEnabled;         // LED灯是否启用
    int overdueThreshold;     // 逾期阈值（天）
    int cabinetTimeout;       // 柜门超时（秒）
    bool visionAlarmEnabled;    // 视觉告警是否启用
    bool powerAlarmEnabled;   // 断电告警是否启用
    
    // 借还设置
    int maxBorrowQty;        // 最大借出数量
    int borrowPeriod;         // 借用期限（天）
    int bufferTime;           // 缓冲时间（分钟）
    bool manualUnlock;        // 是否允许手动开锁
    int brightness;           // 亮度（0-100）
    int lockScreenTime;       // 锁屏时间（秒）
    int faceSensitivity;      // 人脸灵敏度（0-100）
    
    // 备份管理
    bool autoBackup;          // 是否自动备份
    QString backupPeriod;      // 备份周期（daily/weekly/monthly）
    QString backupPath;        // 备份路径
    bool offlineCache;        // 是否启用断网缓存
};
```

---

## Service接口说明

### 1. 获取系统设置
**Service**: `SettingService::getSettings()`
**对应Web API**: `GET /api/settings`

**请求**:
```cpp
ApiResponse getSettings();
```

**响应**:
```json
{
    "success": true,
    "data": {
        "ipAddress": "192.168.1.100",
        "subnetMask": "255.255.255.0",
        "gateway": "192.168.1.1",
        "dns": "8.8.8.8",
        "networkSpeed": "1000M",
        "networkMode": "单机",
        "buzzerVolume": 80,
        "ledEnabled": true,
        "overdueThreshold": 3,
        "cabinetTimeout": 30,
        "visionAlarmEnabled": true,
        "powerAlarmEnabled": true,
        "maxBorrowQty": 5,
        "borrowPeriod": 7,
        "bufferTime": 5,
        "manualUnlock": false,
        "brightness": 80,
        "lockScreenTime": 300,
        "faceSensitivity": 85,
        "autoBackup": true,
        "backupPeriod": "weekly",
        "backupPath": "/backup",
        "offlineCache": true
    }
}
```

---

### 2. 保存系统设置
**Service**: `SettingService::saveSettings(data)`
**对应Web API**: `POST /api/settings`

**请求**:
```cpp
QJsonObject data;
data["ipAddress"] = "192.168.1.100";
data["buzzerVolume"] = 80;
// ... 所有设置字段

ApiResponse saveSettings(const QJsonObject& data);
```

**响应**:
```json
{
    "success": true,
    "data": {
        "message": "设置保存成功"
    }
}
```

---

### 3. 恢复出厂设置
**Service**: `SettingService::factoryReset(adminPwd)`
**对应Web API**: `POST /api/system/factory-reset`

**请求**:
```cpp
ApiResponse factoryReset(const QString& adminPwd);
```

**响应**:
```json
{
    "success": true,
    "data": {
        "message": "恢复出厂设置成功"
    }
}
```

---

### 4. 重启系统
**Service**: `SettingService::restartSystem()`
**对应Web API**: `POST /api/system/restart`

**请求**:
```cpp
ApiResponse restartSystem();
```

---

### 5. 清除全部日志
**Service**: `SettingService::clearAllLogs(adminPwd)`
**对应Web API**: `POST /api/system/clear-logs`

**请求**:
```cpp
ApiResponse clearAllLogs(const QString& adminPwd);
```

---

### 6. 检查软件更新
**Service**: `SettingService::checkUpgrade()`
**对应Web API**: `GET /api/system/check-upgrade`

**响应**:
```json
{
    "success": true,
    "data": {
        "hasUpdate": true,
        "newVersion": "V1.00.9",
        "changelog": "修复已知问题，优化性能"
    }
}
```

---

### 7. 开始软件升级
**Service**: `SettingService::startUpgrade()`
**对应Web API**: `POST /api/system/upgrade`

**请求**:
```cpp
ApiResponse startUpgrade();
```

---

## UI组件映射

| Web组件 | Qt组件 | 说明 |
|---------|--------|------|
| `<el-tabs>` | `QTabWidget` | 四个选项卡 |
| `<el-input>` | `QLineEdit` | 文本输入 |
| `<el-input-number>` | `QSpinBox` | 数字输入 |
| `<el-switch>` | `QCheckBox` | 开关切换 |
| `<el-slider>` | `QSlider` | 滑块 |
| `<el-button>` | `QPushButton` | 按钮 |
| `<el-dialog>` | `QDialog` | 确认对话框 |

---

## 业务逻辑流程

### 流程1：加载系统设置
1. 页面显示时，调用`getSettings()`
2. 解析返回的JSON数据
3. 填充所有选项卡的控件

### 流程2：保存设置
1. 用户修改设置后，点击"保存全部"按钮
2. 收集所有选项卡的控件值
3. 构建QJsonObject
4. 调用`saveSettings(data)`
5. 成功后提示"设置保存成功"

### 流程3：系统操作
1. **恢复出厂**：弹出确认对话框，输入管理员密码，调用`factoryReset()`
2. **重启系统**：弹出确认对话框，调用`restartSystem()`
3. **清除日志**：弹出确认对话框，输入管理员密码，调用`clearAllLogs()`
4. **软件升级**：点击"检查更新"，有更新则显示更新信息，点击"开始升级"

---

## 信号槽设计

### SettingService信号
```cpp
// 设置加载完成
void settingsLoaded(const QJsonObject& settings);

// 设置保存成功
void settingsSaved();

// 系统操作成功
void systemOperationSuccess(const QString& message);

// 系统操作失败
void systemOperationFailed(const QString& errorMessage);
```

### SystemSettingsPage槽函数
```cpp
// 处理设置加载完成
void onSettingsLoaded(const QJsonObject& settings);

// 处理设置保存成功
void onSettingsSaved();

// 处理系统操作成功
void onSystemOperationSuccess(const QString& message);

// 处理系统操作失败
void onSystemOperationFailed(const QString& errorMessage);
```

---

## 关键代码片段

### 1. 加载系统设置
```cpp
void SystemSettingsPage::loadSettings() {
    ApiResponse response = m_settingService->getSettings();
    if (response.success) {
        QJsonObject settings = response.data["data"].toObject();
        updateUI(settings);
    } else {
        QMessageBox::warning(this, "错误", "加载设置失败");
    }
}
```

### 2. 保存设置
```cpp
void SystemSettingsPage::onSaveButtonClicked() {
    QJsonObject data;
    
    // 网络配置
    data["ipAddress"] = m_ipEdit->text();
    data["subnetMask"] = m_maskEdit->text();
    data["gateway"] = m_gatewayEdit->text();
    data["dns"] = m_dnsEdit->text();
    
    // 告警参数
    data["buzzerVolume"] = m_buzzerSlider->value();
    data["ledEnabled"] = m_ledCheck->isChecked();
    data["overdueThreshold"] = m_overdueSpin->value();
    
    // ... 其他设置
    
    ApiResponse response = m_settingService->saveSettings(data);
    if (response.success) {
        QMessageBox::information(this, "成功", "设置保存成功");
    } else {
        QMessageBox::warning(this, "失败", response.errorMessage);
    }
}
```

### 3. 恢复出厂设置
```cpp
void SystemSettingsPage::onFactoryResetButtonClicked() {
    // 弹出密码输入对话框
    bool ok;
    QString password = QInputDialog::getText(this, "确认", 
                                                  "请输入管理员密码：", 
                                                  QLineEdit::Password, "", &ok);
    if (ok && !password.isEmpty()) {
        ApiResponse response = m_settingService->factoryReset(password);
        if (response.success) {
            QMessageBox::information(this, "成功", "恢复出厂设置成功");
            loadSettings();  // 重新加载设置
        } else {
            QMessageBox::warning(this, "失败", response.errorMessage);
        }
    }
}
```

---

## 测试清单

### 功能测试
- [ ] 加载系统设置
- [ ] 保存设置（所有选项卡的控件）
- [ ] 恢复出厂设置（密码验证）
- [ ] 重启系统（确认对话框）
- [ ] 清除日志（密码验证）
- [ ] 检查软件更新
- [ ] 开始软件升级

### 数据一致性测试
- [ ] 设置数据与Web版一致
- [ ] 字段名使用驼峰
- [ ] 数值范围限制正确

### 错误处理测试
- [ ] 保存设置失败
- [ ] 恢复出厂密码错误
- [ ] 网络错误

---

**文档版本**: V1.0  
**最后更新**: 2026-06-21 00:05
