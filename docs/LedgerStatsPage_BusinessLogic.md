# LedgerStatsPage 业务逻辑文档

## 1. 页面概述
**LedgerStatsPage** 是台账统计页面，提供工具借用/归还的统计分析功能。

### 核心功能
1. **台账列表**：显示所有借用/归还记录
2. **统计图表**：按工具、按人员、按时间统计
3. **筛选查询**：按时间范围、工具名称、人员筛选
4. **导出功能**：导出Excel报表

## 2. 数据结构

### 2.1 借用记录 (BorrowRecord)
```cpp
struct BorrowRecord {
    int id;                 // 记录ID
    QString flowNo;         // 流水号
    int userId;             // 借用人ID
    QString realName;       // 借用人姓名
    QString department;      // 部门
    int toolId;             // 工具ID
    QString toolName;       // 工具名称
    QString toolCode;       // 工具编号
    int quantity;           // 数量
    QString borrowTime;     // 借出时间 (yyyy-MM-dd HH:mm:ss)
    QString expectedReturnTime; // 预计归还时间
    QString actualReturnTime;   // 实际归还时间
    QString status;         // 状态: borrowing/returned/overdue
    QString borrowReason;   // 借用原因
    QString taskType;       // 任务类型
};
```

### 2.2 统计摘要 (StatsSummary)
```cpp
struct StatsSummary {
    int totalBorrowCount;      // 总借出次数
    int totalReturnCount;      // 总归还次数
    int currentBorrowing;      // 当前借出中
    int overdueCount;          // 逾期数量
    double avgBorrowDuration;  // 平均借用时长(小时)
    QMap<QString, int> toolBorrowCount;    // 工具借用次数统计
    QMap<QString, int> userBorrowCount;    // 人员借用次数统计
    QMap<QString, int> dailyBorrowCount;   // 每日借用次数统计
};
```

## 3. Service接口定义

### 3.1 RecordService 接口

#### getBorrowRecords()
```cpp
// 获取借用记录列表（支持筛选和分页）
QList<BorrowRecord> getBorrowRecords(
    const QString& startTime = "",    // 开始时间
    const QString& endTime = "",     // 结束时间
    const QString& toolName = "",     // 工具名称（模糊搜索）
    const QString& realName = "",     // 借用人姓名（模糊搜索）
    const QString& status = "",       // 状态筛选
    int page = 1,                    // 页码
    int pageSize = 20                // 每页数量
);
```

#### getStatsSummary()
```cpp
// 获取统计摘要
StatsSummary getStatsSummary(
    const QString& startTime = "",    // 开始时间
    const QString& endTime = ""      // 结束时间
);
```

#### exportRecords()
```cpp
// 导出记录到Excel
bool exportRecords(
    const QString& filePath,          // 导出文件路径
    const QString& startTime = "",    // 开始时间
    const QString& endTime = "",     // 结束时间
    const QString& toolName = "",    // 工具名称
    const QString& realName = ""     // 借用人姓名
);
```

## 4. UI组件映射

| Web组件 | Qt Widget | 说明 |
|---------|-----------|------|
| el-table | QTableWidget | 台账记录表格 |
| el-date-picker | QDateEdit + QTimeEdit | 时间范围选择 |
| el-input | QLineEdit | 搜索输入框 |
| el-select | QComboBox | 状态筛选 |
| el-pagination | 自定义分页控件 | 分页 |
| el-button | QPushButton | 查询、导出按钮 |
| el-card | QGroupBox | 统计卡片 |
| el-chart | QChartView | 统计图表 |

## 5. 业务逻辑流程

### 5.1 页面初始化
```
1. 加载时间范围（默认最近7天）
2. 调用getBorrowRecords()获取记录列表
3. 调用getStatsSummary()获取统计摘要
4. 更新表格和统计卡片
```

### 5.2 查询流程
```
1. 获取筛选条件（时间范围、工具名称、人员、状态）
2. 调用getBorrowRecords()重新查询
3. 更新表格数据
4. 更新统计摘要
```

### 5.3 导出流程
```
1. 弹出文件保存对话框
2. 选择保存路径（.xlsx文件）
3. 调用exportRecords()导出数据
4. 显示导出成功提示
```

## 6. 信号槽设计

### 6.1 信号
```cpp
// 数据加载完成
void recordsLoaded(const QList<BorrowRecord>& records, int total);
// 统计摘要加载完成
void statsSummaryLoaded(const StatsSummary& summary);
// 导出完成
void exportFinished(bool success, const QString& message);
```

### 6.2 槽
```cpp
// 查询按钮点击
void onQueryClicked();
// 导出按钮点击
void onExportClicked();
// 分页切换
void onPageChanged(int page);
// 时间范围变化
void onTimeRangeChanged();
```

## 7. 关键代码片段

### 7.1 加载记录列表
```cpp
void LedgerStatsPage::loadRecords() {
    QString startTime = ui->startDateEdit->dateTime().toString("yyyy-MM-dd HH:mm:ss");
    QString endTime = ui->endDateEdit->dateTime().toString("yyyy-MM-dd HH:mm:ss");
    QString toolName = ui->toolNameEdit->text().trimmed();
    QString realName = ui->realNameEdit->text().trimmed();
    QString status = ui->statusComboBox->currentData().toString();
    int page = ui->pagination->currentPage();
    int pageSize = ui->pagination->pageSize();
    
    QList<BorrowRecord> records = RecordService::instance()->getBorrowRecords(
        startTime, endTime, toolName, realName, status, page, pageSize
    );
    
    updateTable(records);
}
```

### 7.2 更新统计卡片
```cpp
void LedgerStatsPage::updateStatsCards(const StatsSummary& summary) {
    ui->totalBorrowLabel->setText(QString::number(summary.totalBorrowCount));
    ui->totalReturnLabel->setText(QString::number(summary.totalReturnCount));
    ui->currentBorrowingLabel->setText(QString::number(summary.currentBorrowing));
    ui->overdueLabel->setText(QString::number(summary.overdueCount));
    
    // 更新图表
    updateCharts(summary);
}
```

### 7.3 导出Excel
```cpp
void LedgerStatsPage::onExportClicked() {
    QString fileName = QFileDialog::getSaveFileName(this, "导出报表", 
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/台账统计.xlsx",
        "Excel Files (*.xlsx)");
    
    if (fileName.isEmpty()) return;
    
    bool success = RecordService::instance()->exportRecords(
        fileName,
        ui->startDateEdit->dateTime().toString("yyyy-MM-dd HH:mm:ss"),
        ui->endDateEdit->dateTime().toString("yyyy-MM-dd HH:mm:ss"),
        ui->toolNameEdit->text().trimmed(),
        ui->realNameEdit->text().trimmed()
    );
    
    if (success) {
        QMessageBox::information(this, "成功", "导出成功！");
    } else {
        QMessageBox::warning(this, "失败", "导出失败，请重试！");
    }
}
```

## 8. 数据格式转换

### 8.1 Web → Qt (DAO返回 → Qt结构)
```cpp
// Web版: { flow_no: "...", borrow_time: "2024-01-01T12:00:00Z" }
// Qt版: { flowNo: "...", borrowTime: "2024-01-01 12:00:00" }

BorrowRecord fromWebFormat(const QVariantMap& webData) {
    BorrowRecord record;
    record.id = webData["id"].toInt();
    record.flowNo = webData["flow_no"].toString();  // DAO返回驼峰flowNo
    record.userId = webData["user_id"].toInt();
    record.realName = webData["real_name"].toString();
    record.toolId = webData["tool_id"].toInt();
    record.toolName = webData["tool_name"].toString();
    record.quantity = webData["quantity"].toInt();
    
    // 时间格式转换
    QString borrowTime = webData["borrow_time"].toString();
    record.borrowTime = QDateTime::fromString(borrowTime, "yyyy-MM-ddTHH:mm:ssZ")
                       .toString("yyyy-MM-dd HH:mm:ss");
    
    record.status = webData["status"].toString();
    return record;
}
```

## 9. 错误处理

### 9.1 常见错误
1. **查询失败**：显示"查询失败，请重试"
2. **导出失败**：显示"导出失败，请检查文件权限"
3. **无数据**：显示"暂无数据"

### 9.2 错误提示
```cpp
void LedgerStatsPage::showError(const QString& message) {
    QMessageBox::warning(this, "提示", message);
}
```

## 10. 测试要点

### 10.1 功能测试
- [ ] 时间范围筛选
- [ ] 工具名称模糊搜索
- [ ] 人员姓名模糊搜索
- [ ] 状态筛选
- [ ] 分页功能
- [ ] 导出Excel功能

### 10.2 边界测试
- [ ] 时间范围跨度为空
- [ ] 搜索结果为空
- [ ] 导出文件路径无效

### 10.3 性能测试
- [ ] 大量数据加载（1000+条）
- [ ] 统计计算性能

## 11. 注意事项

1. **时间格式**：统一使用`"yyyy-MM-dd HH:mm:ss"`字符串
2. **分页**：默认每页20条，支持自定义
3. **导出**：使用QXlsx库生成Excel文件
4. **图表**：使用Qt Charts模块
5. **触屏优化**：按钮和输入框要足够大

## 12. 与Web版差异说明

| 项目 | Web版 | Qt版 |
|------|-------|------|
| 图表库 | ECharts | Qt Charts |
| 导出格式 | CSV/Excel | Excel(.xlsx) |
| 时间选择 | el-date-picker | QDateEdit + QTimeEdit |
| 分页 | el-pagination | 自定义分页控件 |

## 13. 待实现功能

1. **高级筛选**：更多筛选条件（工具分类、部门等）
2. **图表交互**：点击图表查看详细数据
3. **定时刷新**：自动刷新统计数据
4. **打印功能**：直接打印报表

---
**文档版本**: v1.0  
**创建时间**: 2026-06-20  
**作者**: logic-agent
