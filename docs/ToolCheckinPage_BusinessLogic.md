# ToolCheckinPage 业务逻辑文档

## 1. 页面概述
**ToolCheckinPage** 是工具入库页面，用于管理员将新工具录入系统。

### 核心功能
1. **工具信息录入**：录入工具基本信息
2. **视觉标签绑定**：绑定视觉标签到工具
3. **批量入库**：支持批量录入工具
4. **入库确认**：确认入库操作

## 2. 数据结构

### 2.1 工具信息 (ToolInfo)
```cpp
struct ToolInfo {
    int id;                 // 工具ID
    QString toolName;       // 工具名称
    QString toolCode;       // 工具编号（唯一）
    QString category;       // 分类
    QString specification;   // 规格
    QString manufacturer;    // 制造商
    int quantity;           // 数量
    QString location;        // 存放位置
    QString visionTag;        // 视觉标签
    QString status;          // 状态: in_stock/borrowed/maintenance
    QString purchaseDate;    // 采购日期
    QString warrantyPeriod;  // 保修期
    QString notes;           // 备注
};
```

### 2.2 入库记录 (CheckinRecord)
```cpp
struct CheckinRecord {
    int id;                 // 记录ID
    int toolId;             // 工具ID
    QString toolName;       // 工具名称
    QString toolCode;       // 工具编号
    int quantity;           // 入库数量
    QString checkinTime;    // 入库时间 (yyyy-MM-dd HH:mm:ss)
    int operatorId;         // 操作人ID
    QString operatorName;   // 操作人姓名
    QString notes;           // 备注
};
```

## 3. Service接口定义

### 3.1 ToolService 接口

#### checkinTool()
```cpp
// 工具入库
bool checkinTool(const ToolInfo& toolInfo);
```

#### batchCheckin()
```cpp
// 批量入库
bool batchCheckin(const QList<ToolInfo>& toolList);
```

#### generateToolCode()
```cpp
// 生成工具编号
QString generateToolCode(const QString& category = "");
```

#### bindVisionTag()
```cpp
// 绑定视觉标签
bool bindVisionTag(int toolId, const QString& visionTag);
```

#### unbindVisionTag()
```cpp
// 解绑视觉标签
bool unbindVisionTag(int toolId);
```

#### getToolByVision()
```cpp
// 根据视觉标签获取工具
ToolInfo getToolByVision(const QString& visionTag);
```

## 4. UI组件映射

| Web组件 | Qt Widget | 说明 |
|---------|-----------|------|
| el-form | QGroupBox + QFormLayout | 工具信息表单 |
| el-input | QLineEdit | 输入框 |
| el-select | QComboBox | 下拉选择 |
| el-date-picker | QDateEdit | 日期选择 |
| el-button | QPushButton | 入库、重置、取消按钮 |
| el-table | QTableWidget | 批量入库列表 |
| el-tag | QLabel + 样式 | 视觉标签显示 |

## 5. 业务逻辑流程

### 5.1 单个工具入库流程
```
1. 填写工具基本信息（名称、分类、规格等）
2. 点击"生成编号"自动生成工具编号
3. 扫描视觉标签（可选）
4. 点击"入库"按钮
5. 调用checkinTool()入库
6. 显示入库成功提示
7. 清空表单，准备下一个
```

### 5.2 批量入库流程
```
1. 点击"批量入库"切换到批量模式
2. 逐个录入工具信息（或导入Excel）
3. 显示批量列表
4. 点击"批量入库"按钮
5. 调用batchCheckin()批量入库
6. 显示入库结果
```

### 5.3 视觉绑定流程
```
1. 输入工具编号或扫码
2. 点击"绑定视觉"按钮
3. 将视觉读写器靠近工具
4. 读取到视觉标签
5. 调用bindVisionTag()绑定
6. 显示绑定成功
```

## 6. 信号槽设计

### 6.1 信号
```cpp
// 工具入库完成
void toolCheckedIn(bool success, const QString& message);
// 批量入库完成
void batchCheckinFinished(int successCount, int failCount);
// 视觉绑定完成
void visionBound(bool success, const QString& visionTag);
// 工具编号生成完成
void toolCodeGenerated(const QString& toolCode);
```

### 6.2 槽
```cpp
// 入库按钮点击
void onCheckinClicked();
// 批量入库按钮点击
void onBatchCheckinClicked();
// 重置按钮点击
void onResetClicked();
// 取消按钮点击
void onCancelClicked();
// 生成编号按钮点击
void onGenerateCodeClicked();
// 绑定视觉按钮点击
void onBindVisionClicked();
// 表单数据变化
void onFormDataChanged();
```

## 7. 关键代码片段

### 7.1 工具入库
```cpp
void ToolCheckinPage::onCheckinClicked() {
    // 验证表单
    if (!validateForm()) {
        QMessageBox::warning(this, "错误", "请填写完整信息！");
        return;
    }
    
    // 构造工具信息
    ToolInfo tool;
    tool.toolName = ui->toolNameEdit->text().trimmed();
    tool.toolCode = ui->toolCodeEdit->text().trimmed();
    tool.category = ui->categoryComboBox->currentText();
    tool.specification = ui->specEdit->text().trimmed();
    tool.manufacturer = ui->manufacturerEdit->text().trimmed();
    tool.quantity = ui->quantitySpinBox->value();
    tool.location = ui->locationEdit->text().trimmed();
    tool.visionTag = ui->visionEdit->text().trimmed();
    tool.purchaseDate = ui->purchaseDateEdit->date().toString("yyyy-MM-dd");
    tool.notes = ui->notesEdit->toPlainText().trimmed();
    tool.status = "in_stock";
    
    // 调用Service入库
    bool success = ToolService::instance()->checkinTool(tool);
    
    if (success) {
        QMessageBox::information(this, "成功", "工具入库成功！");
        clearForm();
    } else {
        QMessageBox::warning(this, "失败", "工具入库失败，请重试！");
    }
}
```

### 7.2 生成工具编号
```cpp
void ToolCheckinPage::onGenerateCodeClicked() {
    QString category = ui->categoryComboBox->currentText();
    QString toolCode = ToolService::instance()->generateToolCode(category);
    ui->toolCodeEdit->setText(toolCode);
}
```

### 7.3 视觉绑定
```cpp
void ToolCheckinPage::onBindVisionClicked() {
    QString toolCode = ui->toolCodeEdit->text().trimmed();
    if (toolCode.isEmpty()) {
        QMessageBox::warning(this, "错误", "请先输入工具编号！");
        return;
    }
    
    // 启动视觉读取线程
    m_visionThread = new VisionReadThread(this);
    connect(m_visionThread, &VisionReadThread::visionRead, this, &ToolCheckinPage::onVisionRead);
    m_visionThread->start();
    
    QMessageBox::information(this, "提示", "请将视觉标签靠近读写器...");
}

void ToolCheckinPage::onVisionRead(const QString& visionTag) {
    ui->visionEdit->setText(visionTag);
    m_visionThread->quit();
    m_visionThread->wait();
    m_visionThread->deleteLater();
    
    QMessageBox::information(this, "成功", "视觉标签读取成功！");
}
```

### 7.4 批量入库
```cpp
void ToolCheckinPage::onBatchCheckinClicked() {
    if (m_batchList.isEmpty()) {
        QMessageBox::warning(this, "错误", "批量列表为空！");
        return;
    }
    
    int successCount = 0;
    int failCount = 0;
    
    foreach (const ToolInfo& tool, m_batchList) {
        bool success = ToolService::instance()->checkinTool(tool);
        if (success) {
            successCount++;
        } else {
            failCount++;
        }
    }
    
    QMessageBox::information(this, "完成", 
        QString("批量入库完成！成功%1个，失败%2个").arg(successCount).arg(failCount));
    
    m_batchList.clear();
    updateBatchTable();
}
```

## 8. 数据格式转换

### 8.1 Web → Qt (表单数据 → ToolInfo)
```cpp
ToolInfo fromFormData(const QVariantMap& formData) {
    ToolInfo tool;
    tool.toolName = formData["toolName"].toString();
    tool.toolCode = formData["toolCode"].toString();
    tool.category = formData["category"].toString();
    tool.specification = formData["specification"].toString();
    tool.quantity = formData["quantity"].toInt();
    tool.visionTag = formData["visionTag"].toString();
    tool.status = "in_stock";
    return tool;
}
```

## 9. 错误处理

### 9.1 常见错误
1. **工具编号重复**：显示"工具编号已存在"
2. **视觉标签重复**：显示"视觉标签已绑定其他工具"
3. **入库失败**：显示"入库失败，请重试"
4. **表单验证失败**：显示"请填写完整信息"

### 9.2 错误提示
```cpp
void ToolCheckinPage::showError(const QString& message) {
    QMessageBox::warning(this, "错误", message);
}
```

## 10. 测试要点

### 10.1 功能测试
- [ ] 单个工具入库
- [ ] 批量工具入库
- [ ] 视觉标签绑定
- [ ] 工具编号生成
- [ ] 表单验证

### 10.2 边界测试
- [ ] 工具编号重复
- [ ] 视觉标签重复
- [ ] 数量为0
- [ ] 必填项为空

### 10.3 交互测试
- [ ] 按钮点击响应
- [ ] 输入框焦点切换
- [ ] 视觉读取响应

## 11. 注意事项

1. **工具编号唯一性**：toolCode必须唯一，入库前要检查
2. **视觉标签唯一性**：visionTag必须唯一，绑定前要检查
3. **数量验证**：入库数量必须大于0
4. **触屏优化**：按钮高度56px，输入框高度48px
5. **视觉读取**：要在后台线程执行，避免阻塞UI

## 12. 与Web版差异说明

| 项目 | Web版 | Qt版 |
|------|-------|------|
| 视觉读取 | Web Serial API | 串口通信（QSerialPort） |
| 批量导入 | Excel文件上传 | Excel文件读取（QXlsx） |
| 编号生成 | 后端API | 本地生成（可调用后端API） |

## 13. 待实现功能

1. **Excel导入**：从Excel文件批量导入工具
2. **二维码生成**：生成工具二维码标签
3. **打印标签**：打印工具标签（含二维码/条形码）
4. **入库模板**：下载入库模板Excel

## 14. 依赖库

1. **Qt Serial Port**：视觉读写器通信
2. **QXlsx**：Excel文件读写
3. **视觉 SDK**：视觉读写器SDK（厂商提供）

---
**文档版本**: v1.0  
**创建时间**: 2026-06-20  
**作者**: logic-agent
