# ToolReturnPage 业务逻辑文档

**作者**: 袁燕  
**日期**: 2026-06-20  
**目标**: 1:1复刻Web前端ToolReturn.vue

## 页面概述

工具归还页面，支持：
1. **用户信息条**：姓名/工号/日期
2. **借用记录表格**：勾选确认归还
3. **底部操作栏**：显示已选数量，归还按钮

## 数据结构

### 1. 借用记录（BorrowRecord）
```cpp
struct BorrowRecord {
    int recordId;          // 记录ID
    int userId;            // 用户ID
    QString toolName;      // 工具名称
    QString toolCode;      // 工具编号
    QString spec;          // 规格
    QString cabinet;        // 柜体
    QString position;       // 位置
    int quantity;          // 借用数量
    QString borrowTime;     // 借用时间
    QString expectedReturnTime; // 预计归还时间
    QString actualReturnTime; // 实际归还时间
    QString reason;        // 借用原因
    QString flowNo;       // 流水号
    QString status;        // 状态（borrowed/returned/overdue）
    bool selected;         // 是否选中（UI用）
};
```

## Service接口说明

### 1. 获取用户借用记录
**Service**: `ReturnService::getUserBorrowingRecords(userId)`
**对应Web API**: `GET /api/records/{userId}`

**请求**:
```cpp
ApiResponse getUserBorrowingRecords(int userId);
```

**响应**:
```json
{
    "success": true,
    "data": [
        {
            "recordId": 1,
            "toolName": "扳手",
            "toolCode": "T001",
            "spec": "10mm",
            "cabinet": "A柜",
            "position": "1层01",
            "quantity": 2,
            "borrowTime": "2026-06-18 10:00:00",
            "expectedReturnTime": "2026-06-20 10:00:00",
            "reason": "borrow",
            "flowNo": "BOR-20260618-001",
            "status": "borrowed"
        }
    ]
}
```

---

### 2. 归还工具
**Service**: `ReturnService::returnTools(recordIds, userId)`
**对应Web API**: `POST /api/tools/return`

**请求**:
```cpp
QList<int> recordIds = {1, 2, 3};  // 选中的记录ID
ApiResponse returnTools(const QList<int>& recordIds, int userId);
```

**响应**:
```json
{
    "success": true,
    "data": {
        "count": 2,
        "message": "成功归还2件工具"
    }
}
```

---

## UI组件映射

| Web组件 | Qt组件 | 说明 |
|---------|--------|------|
| `<el-table>` | `QTableWidget` | 借用记录表格（带复选框） |
| `<el-button>` | `QPushButton` | 归还按钮 |
| `<el-checkbox>` | `QCheckBox`（表格中） | 勾选框 |

## 业务逻辑流程

### 流程1：加载借用记录
1. 页面显示时，调用`getUserBorrowingRecords(userId)`
2. 只显示status='borrowed'的记录
3. 填充表格：工具名称/规格/柜位/数量/原因/流水号/状态

### 流程2：勾选记录
1. 用户点击表格中的复选框
2. 更新`m_checkedRecordIds`集合
3. 更新底部操作栏的已选数量

### 流程3：归还选中工具
1. 用户点击"归还选中"按钮
2. 验证：`m_checkedRecordIds`不能为空
3. 调用`returnTools(m_checkedRecordIds, userId)`
4. 成功后刷新表格，清空选中状态

---

## 信号槽设计

### ReturnService信号
```cpp
// 归还成功
void returnSuccess(int count);

// 归还失败
void returnFailed(const QString& errorMessage);
```

### ToolReturnPage槽函数
```cpp
// 处理归还成功
void onReturnSuccess(int count);

// 处理归还失败
void onReturnFailed(const QString& errorMessage);
```

---

## 关键代码片段

### 1. 加载借用记录
```cpp
void ToolReturnPage::loadBorrowingRecords() {
    ApiResponse response = m_returnService->getUserBorrowingRecords(m_currentUserId);
    if (response.success) {
        QJsonArray records = response.data["data"].toArray();
        updateTable(records);
    }
}
```

### 2. 表格复选框处理
```cpp
void ToolReturnPage::onTableItemChanged(QTableWidgetItem* item) {
    if (item->column() == 0) {  // 复选框列
        int recordId = item->data(Qt::UserRole).toInt();
        if (item->checkState() == Qt::Checked) {
            m_checkedRecordIds.insert(recordId);
        } else {
            m_checkedRecordIds.remove(recordId);
        }
        updateBottomBar();  // 更新已选数量
    }
}
```

### 3. 执行归还
```cpp
void ToolReturnPage::onReturnButtonClicked() {
    if (m_checkedRecordIds.isEmpty()) {
        QMessageBox::warning(this, "提示", "请选择要归还的工具");
        return;
    }
    
    QList<int> recordIds = QList<int>::fromSet(m_checkedRecordIds);
    ApiResponse response = m_returnService->returnTools(recordIds, m_currentUserId);
    
    if (response.success) {
        int count = response.data["data"].toObject()["count"].toInt();
        QMessageBox::information(this, "成功", QString("成功归还%1件工具").arg(count));
        loadBorrowingRecords();  // 刷新表格
        m_checkedRecordIds.clear();  // 清空选中
    } else {
        QMessageBox::warning(this, "失败", response.errorMessage);
    }
}
```

---

## 测试清单

### 功能测试
- [ ] 借用记录表格显示
- [ ] 复选框勾选
- [ ] 已选数量更新
- [ ] 归还选中按钮
- [ ] 全部归还按钮

### 数据一致性测试
- [ ] 记录数据与Web版一致
- [ ] 字段名使用驼峰
- [ ] 时间格式统一

### 错误处理测试
- [ ] 无选中记录点击归还
- [ ] 归还失败处理

---

**文档版本**: V1.0  
**最后更新**: 2026-06-20 23:50
