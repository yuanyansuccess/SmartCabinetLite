# ToolCheckoutPage 业务逻辑文档

## 1. 页面概述
**ToolCheckoutPage** 是工具出库页面，用于管理员将工具从库存中移除（报废、转移等）。

### 核心功能
1. **工具查询**：根据工具编号/名称查询工具
2. **出库信息录入**：录入出库原因、目标位置等
3. **确认出库**：确认出库操作
4. **出库记录**：查看出库历史记录

## 2. 数据结构

### 2.1 出库记录 (CheckoutRecord)
```cpp
struct CheckoutRecord {
    int id;                 // 记录ID
    int toolId;             // 工具ID
    QString toolName;       // 工具名称
    QString toolCode;       // 工具编号
    int quantity;           // 出库数量
    QString checkoutTime;   // 出库时间 (yyyy-MM-dd HH:mm:ss)
    int operatorId;         // 操作人ID
    QString operatorName;   // 操作人姓名
    QString reason;         // 出库原因
    QString targetLocation; // 目标位置
    QString notes;          // 备注
};
```

### 2.2 出库原因枚举
```cpp
enum CheckoutReason {
    SCRAP = 0,          // 报废
    TRANSFER = 1,       // 转移
    MAINTENANCE = 2,    // 送修
    LOSS = 3,           // 丢失
    OTHER = 4           // 其他
};
```

## 3. Service接口定义

### 3.1 ToolService 接口

#### checkoutTool()
```cpp
// 工具出库
bool checkoutTool(int toolId, int quantity, const QString& reason, 
                  const QString& targetLocation = "", const QString& notes = "");
```

#### getCheckoutRecords()
```cpp
// 获取出库记录
QList<CheckoutRecord> getCheckoutRecords(
    const QString& startTime = "",
    const QString& endTime = "",
    int page = 1,
    int pageSize = 20
);
```

#### getToolByCode()
```cpp
// 根据工具编号查询工具
ToolInfo getToolByCode(const QString& toolCode);
```

#### updateToolStatus()
```cpp
// 更新工具状态
bool updateToolStatus(int toolId, const QString& status);
```

## 4. UI组件映射

| Web组件 | Qt Widget | 说明 |
|---------|-----------|------|
| el-form | QGroupBox + QFormLayout | 出库信息表单 |
| el-input | QLineEdit | 工具编号/名称输入 |
| el-select | QComboBox | 出库原因选择 |
| el-date-picker | QDateEdit | 出库日期 |
| el-button | QPushButton | 查询、出库、重置按钮 |
| el-table | QTableWidget | 出库记录表格 |
| el-message | QMessageBox | 提示信息 |

## 5. 业务逻辑流程

### 5.1 工具查询流程
```
1. 输入工具编号或名称
2. 点击"查询"按钮
3. 调用getToolByCode()或搜索工具
4. 显示工具信息（名称、编号、库存数量等）
5. 如果找到，启用出库按钮
```

### 5.2 出库流程
```
1. 查询到工具
2. 输入出库数量（不能超过库存）
3. 选择出库原因
4. 输入目标位置（可选）
5. 输入备注（可选）
6. 点击"确认出库"按钮
7. 调用checkoutTool()出库
8. 显示出库成功提示
9. 清空表单
```

### 5.3 出库记录查询流程
```
1. 页面加载时自动查询最近30天的出库记录
2. 支持按时间范围筛选
3. 调用getCheckoutRecords()获取记录
4. 更新表格显示
```

## 6. 信号槽设计

### 6.1 信号
```cpp
// 工具查询完成
void toolFound(const ToolInfo& tool);
// 工具未找到
void toolNotFound(const QString& toolCode);
// 出库完成
void checkoutFinished(bool success, const QString& message);
// 出库记录加载完成
void checkoutRecordsLoaded(const QList<CheckoutRecord>& records, int total);
```

### 6.2 槽
```cpp
// 查询按钮点击
void onQueryClicked();
// 出库按钮点击
void onCheckoutClicked();
// 重置按钮点击
void onResetClicked();
// 取消按钮点击
void onCancelClicked();
// 出库数量变化
void onQuantityChanged(int value);
// 出库原因变化
void onReasonChanged(int index);
```

## 7. 关键代码片段

### 7.1 查询工具
```cpp
void ToolCheckoutPage::onQueryClicked() {
    QString toolCode = ui->toolCodeEdit->text().trimmed();
    if (toolCode.isEmpty()) {
        QMessageBox::warning(this, "错误", "请输入工具编号或名称！");
        return;
    }
    
    ToolInfo tool = ToolService::instance()->getToolByCode(toolCode);
    
    if (tool.id > 0) {
        displayToolInfo(tool);
        ui->checkoutButton->setEnabled(true);
        emit toolFound(tool);
    } else {
        QMessageBox::warning(this, "未找到", "未找到该工具！");
        ui->checkoutButton->setEnabled(false);
        emit toolNotFound(toolCode);
    }
}
```

### 7.2 显示工具信息
```cpp
void ToolCheckoutPage::displayToolInfo(const ToolInfo& tool) {
    ui->toolNameLabel->setText(tool.toolName);
    ui->toolCodeLabel->setText(tool.toolCode);
    ui->categoryLabel->setText(tool.category);
    ui->quantityLabel->setText(QString::number(tool.quantity));
    ui->locationLabel->setText(tool.location);
    ui->statusLabel->setText(tool.status == "in_stock" ? "在库" : "借出");
    
    // 设置最大出库数量
    ui->quantitySpinBox->setMaximum(tool.quantity);
    ui->quantitySpinBox->setValue(1);
}
```

### 7.3 确认出库
```cpp
void ToolCheckoutPage::onCheckoutClicked() {
    // 验证表单
    if (!validateForm()) {
        return;
    }
    
    int toolId = m_currentTool.id;
    int quantity = ui->quantitySpinBox->value();
    QString reason = ui->reasonComboBox->currentText();
    QString targetLocation = ui->targetLocationEdit->text().trimmed();
    QString notes = ui->notesEdit->toPlainText().trimmed();
    
    // 确认对话框
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "确认出库",
        QString("确认要出库工具 [%1] %2 个吗？").arg(m_currentTool.toolName).arg(quantity),
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::No) {
        return;
    }
    
    // 调用Service出库
    bool success = ToolService::instance()->checkoutTool(
        toolId, quantity, reason, targetLocation, notes
    );
    
    if (success) {
        QMessageBox::information(this, "成功", "工具出库成功！");
        clearForm();
        loadCheckoutRecords(); // 刷新出库记录
    } else {
        QMessageBox::warning(this, "失败", "工具出库失败，请重试！");
    }
}
```

### 7.4 加载出库记录
```cpp
void ToolCheckoutPage::loadCheckoutRecords() {
    QString startTime = ui->startDateEdit->dateTime().toString("yyyy-MM-dd HH:mm:ss");
    QString endTime = ui->endDateEdit->dateTime().toString("yyyy-MM-dd HH:mm:ss");
    int page = ui->pagination->currentPage();
    int pageSize = ui->pagination->pageSize();
    
    QList<CheckoutRecord> records = ToolService::instance()->getCheckoutRecords(
        startTime, endTime, page, pageSize
    );
    
    updateRecordTable(records);
}
```

## 8. 数据格式转换

### 8.1 Web → Qt (DAO返回 → Qt结构)
```cpp
// Web版: { tool_code: "...", checkout_time: "2024-01-01T12:00:00Z" }
// Qt版: { toolCode: "...", checkoutTime: "2024-01-01 12:00:00" }

CheckoutRecord fromWebFormat(const QVariantMap& webData) {
    CheckoutRecord record;
    record.id = webData["id"].toInt();
    record.toolId = webData["tool_id"].toInt();
    record.toolName = webData["tool_name"].toString();
    record.toolCode = webData["tool_code"].toString();
    record.quantity = webData["quantity"].toInt();
    
    // 时间格式转换
    QString checkoutTime = webData["checkout_time"].toString();
    record.checkoutTime = QDateTime::fromString(checkoutTime, "yyyy-MM-ddTHH:mm:ssZ")
                         .toString("yyyy-MM-dd HH:mm:ss");
    
    record.reason = webData["reason"].toString();
    record.targetLocation = webData["target_location"].toString();
    return record;
}
```

## 9. 错误处理

### 9.1 常见错误
1. **工具未找到**：显示"未找到该工具"
2. **库存不足**：显示"库存不足，无法出库"
3. **出库失败**：显示"出库失败，请重试"
4. **表单验证失败**：显示"请填写完整信息"

### 9.2 错误提示
```cpp
void ToolCheckoutPage::showError(const QString& message) {
    QMessageBox::warning(this, "错误", message);
}
```

## 10. 测试要点

### 10.1 功能测试
- [ ] 工具查询（按编号/名称）
- [ ] 出库操作
- [ ] 出库记录查询
- [ ] 表单验证

### 10.2 边界测试
- [ ] 出库数量 > 库存数量
- [ ] 工具不存在
- [ ] 出库数量为0
- [ ] 必填项为空

### 10.3 交互测试
- [ ] 按钮点击响应
- [ ] 输入框焦点切换
- [ ] 确认对话框

## 11. 注意事项

1. **库存验证**：出库数量不能超过当前库存
2. **工具状态**：只有status="in_stock"的工具才能出库
3. **出库原因**：必须选择出库原因
4. **触屏优化**：按钮高度56px，输入框高度48px
5. **确认对话框**：出库前要确认，防止误操作

## 12. 与Web版差异说明

| 项目 | Web版 | Qt版 |
|------|-------|------|
| 工具查询 | 实时搜索（输入时自动查询） | 点击查询按钮 |
| 出库确认 | el-message-box | QMessageBox |
| 记录分页 | el-pagination | 自定义分页控件 |

## 13. 待实现功能

1. **批量出库**：支持批量选择工具出库
2. **出库审批**：需要管理员审批
3. **出库打印**：打印出库单
4. **出库统计**：统计出库数据

## 14. 依赖库

1. **无特殊依赖**：使用Qt自带库

---
**文档版本**: v1.0  
**创建时间**: 2026-06-20  
**作者**: logic-agent
