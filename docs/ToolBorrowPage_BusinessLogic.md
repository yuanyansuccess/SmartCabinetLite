# ToolBorrowPage 业务逻辑文档

**作者**: 袁燕  
**日期**: 2026-06-20  
**目标**: 1:1复刻Web前端ToolBorrow.vue

## 页面概述

工具借用页面，支持：
1. **任务类型多选**：从DB动态加载，支持搜索
2. **智能推荐工具**：根据任务类型自动推荐
3. **工具列表**：推荐工具在前，全部在库工具在后
4. **借用信息**：任务单号（自动生成）、预计归还时间
5. **三个标签页**：借用任务/常用工具/借用记录

## 数据结构

### 1. 任务类型（TaskType）
```cpp
struct TaskType {
    int id;                 // 任务类型ID
    QString code;           // 类型代码（ROUTINE/OVERHAUL等）
    QString name;           // 类型名称（例行检查/大修工作等）
    QString description;     // 类型描述
    bool active;            // 是否启用
};
```

### 2. 推荐工具（RecommendedTool）
```cpp
struct RecommendedTool {
    int toolId;            // 工具ID
    QString toolCode;       // 工具编号
    QString toolName;       // 工具名称
    QString spec;           // 规格
    QString cabinet;        // 柜体
    QString position;       // 位置
    int totalQty;          // 总数
    int inStockQty;        // 在库数量
    QStringList recommendTypes;  // 推荐的任务类型
    QString recommendReason; // 推荐原因
};
```

### 3. 借用记录（BorrowRecord）
```cpp
struct BorrowRecord {
    int recordId;          // 记录ID
    int userId;            // 用户ID
    int toolId;           // 工具ID
    QString toolName;      // 工具名称
    QString toolCode;      // 工具编号
    QString spec;          // 规格
    int quantity;          // 借用数量
    QString reason;        // 借用原因（borrow/return/checkin/checkout）
    QString flowNo;       // 流水号
    QString borrowTime;    // 借用时间
    QString expectedReturnTime;  // 预计归还时间
    QString status;        // 状态（borrowed/returned/overdue）
};
```

## Service接口说明

### 1. 获取任务类型列表
**Service**: `BorrowService::getTaskTypes()`
**对应Web API**: `GET /api/task-types`

**请求**:
```cpp
ApiResponse getTaskTypes();
```

**响应**:
```json
{
    "success": true,
    "data": [
        {
            "id": 1,
            "code": "ROUTINE",
            "name": "例行检查",
            "description": "日常例行检查工作",
            "active": true
        },
        {
            "id": 2,
            "code": "OVERHAUL",
            "name": "大修工作",
            "description": "设备大修维护工作",
            "active": true
        }
    ]
}
```

**Qt调用示例**:
```cpp
ApiResponse response = m_borrowService->getTaskTypes();
if (response.success) {
    QJsonArray taskTypes = response.data["data"].toArray();
    for (const QJsonValue& v : taskTypes) {
        QJsonObject obj = v.toObject();
        TaskType tt;
        tt.id = obj["id"].toInt();
        tt.code = obj["code"].toString();
        tt.name = obj["name"].toString();
        tt.description = obj["description"].toString();
        tt.active = obj["active"].toBool();
        // 添加到UI
    }
}
```

---

### 2. 根据任务类型获取推荐工具
**Service**: `BorrowService::getRecommendedTools(typeIds)`
**对应Web API**: `POST /api/tools/recommended`

**请求**:
```cpp
ApiResponse getRecommendedTools(const QList<int>& typeIds);
```

**响应**:
```json
{
    "success": true,
    "data": [
        {
            "toolId": 1,
            "toolCode": "T001",
            "toolName": "扳手",
            "spec": "10mm",
            "cabinet": "A柜",
            "position": "1层01",
            "totalQty": 10,
            "inStockQty": 8,
            "recommendTypes": ["ROUTINE", "OVERHAUL"],
            "recommendReason": "适用于例行检查和大修工作"
        }
    ]
}
```

**Qt调用示例**:
```cpp
QList<int> selectedTypeIds = {1, 2};  // 用户选中的任务类型ID
ApiResponse response = m_borrowService->getRecommendedTools(selectedTypeIds);
if (response.success) {
    QJsonArray tools = response.data["data"].toArray();
    // 解析推荐工具列表
}
```

---

### 3. 获取在库工具列表
**Service**: `ToolService::getTools(params)`
**对应Web API**: `GET /api/tools?status=in_stock`

**请求**:
```cpp
QJsonObject params;
params["status"] = "in_stock";  // 只查询在库工具
params["page"] = 1;
params["pageSize"] = 100;
ApiResponse getTools(const QJsonObject& params);
```

**响应**:
```json
{
    "success": true,
    "data": [
        {
            "toolId": 1,
            "toolCode": "T001",
            "toolName": "扳手",
            "spec": "10mm",
            "cabinet": "A柜",
            "position": "1层01",
            "totalQty": 10,
            "inStockQty": 8,
            "status": "in_stock"
        }
    ],
    "total": 50,
    "page": 1,
    "pageSize": 100
}
```

---

### 4. 借用工具
**Service**: `BorrowService::borrowTool(data)`
**对应Web API**: `POST /api/tools/borrow`

**请求**:
```cpp
QJsonObject data;
data["userId"] = currentUserId;
data["toolId"] = selectedToolId;
data["quantity"] = borrowQty;
data["reason"] = "borrow";  // 借用原因
data["expectedReturnTime"] = "2026-06-22 14:30:00";
data["flowNo"] = generateFlowNo("borrow");

ApiResponse borrowTool(const QJsonObject& data);
```

**响应**:
```json
{
    "success": true,
    "data": {
        "recordId": 123,
        "flowNo": "BOR-20260620-001"
    }
}
```

---

### 5. 生成流水号
**Service**: `BorrowService::generateFlowNo(reason)`
**逻辑**: 日期时间 + 原因缩写 + 随机数

**示例**:
```cpp
QString flowNo = m_borrowService->generateFlowNo("borrow");
// 结果：BOR-20260620-001
```

**格式说明**:
- `BOR`: borrow（借用）
- `RET`: return（归还）
- `IN`: checkin（入库）
- `OUT`: checkout（出库）
- `20260620`: 日期
- `001`: 随机数

---

## UI组件映射

| Web组件 | Qt组件 | 说明 |
|---------|--------|------|
| `<el-checkbox-group>` | `QButtonGroup` + `QPushButton` | 任务类型多选（自定义复选按钮） |
| `<el-table>` | `QTableWidget` | 推荐工具表格 + 全部工具表格 |
| `<el-tabs>` | `QTabWidget` | 三个标签页 |
| `<el-date-picker>` | `QDateTimeEdit` | 预计归还时间 |
| `<el-input>` | `QLineEdit` | 任务单号（只读） |
| `<el-button>` | `QPushButton` | 借用按钮 |

## 业务逻辑流程

### 流程1：任务类型多选 → 推荐工具
1. 页面加载时，调用`getTaskTypes()`获取所有任务类型
2. 用户点击任务类型按钮（多选），选中的按钮高亮
3. 选中的任务类型ID收集到`QList<int> selectedTypeIds`
4. 调用`getRecommendedTools(selectedTypeIds)`获取推荐工具
5. 推荐工具显示在"推荐工具"表格中

### 流程2：工具选择 → 借用
1. 用户在"推荐工具"或"全部工具"表格中勾选工具
2. 输入借用数量（不能超过在库数量）
3. 选择预计归还时间（默认3天后）
4. 点击"借用"按钮
5. 生成流水号
6. 调用`borrowTool(data)`执行借用
7. 成功后刷新表格和借用记录

### 流程3：标签页切换
- **标签页1：借用任务**（默认）
  - 任务类型多选
  - 推荐工具表格
  - 全部工具表格
  - 借用信息表单
  
- **标签页2：常用工具**
  - 用户经常借用的工具列表
  - 快速借用按钮
  
- **标签页3：借用记录**
  - 当前用户的借用记录
  - 状态筛选（全部/借用中/已归还/逾期）

## 错误处理

### 1. 任务类型加载失败
- 显示错误提示
- 提供重试按钮

### 2. 推荐工具加载失败
- 显示错误提示
- 降级显示全部工具

### 3. 借用失败
- 库存不足：提示"库存不足，当前在库X件"
- 参数错误：提示具体错误信息
- 网络错误：提示"网络错误，请稍后重试"

## 信号槽设计

### BorrowService信号
```cpp
// 任务类型加载完成
void taskTypesLoaded(const QJsonArray& taskTypes);

// 推荐工具加载完成
void recommendedToolsLoaded(const QJsonArray& tools);

// 借用成功
void borrowSuccess(int recordId, const QString& flowNo);

// 借用失败
void borrowFailed(const QString& errorMessage);
```

### ToolBorrowPage槽函数
```cpp
// 处理任务类型加载完成
void onTaskTypesLoaded(const QJsonArray& taskTypes);

// 处理推荐工具加载完成
void onRecommendedToolsLoaded(const QJsonArray& tools);

// 处理借用成功
void onBorrowSuccess(int recordId, const QString& flowNo);

// 处理借用失败
void onBorrowFailed(const QString& errorMessage);
```

## 关键代码片段

### 1. 加载任务类型
```cpp
void ToolBorrowPage::loadTaskTypes() {
    ApiResponse response = m_borrowService->getTaskTypes();
    if (response.success) {
        QJsonArray taskTypes = response.data["data"].toArray();
        updateTaskTypeButtons(taskTypes);
    } else {
        QMessageBox::warning(this, "错误", "加载任务类型失败");
    }
}
```

### 2. 任务类型按钮点击
```cpp
void ToolBorrowPage::onTaskTypeButtonClicked(int typeId) {
    // 切换选中状态
    if (m_selectedTypeIds.contains(typeId)) {
        m_selectedTypeIds.removeAll(typeId);
    } else {
        m_selectedTypeIds.append(typeId);
    }
    
    // 更新按钮样式（选中/未选中）
    updateTaskTypeButtonStyle(typeId);
    
    // 重新加载推荐工具
    loadRecommendedTools();
}
```

### 3. 加载推荐工具
```cpp
void ToolBorrowPage::loadRecommendedTools() {
    if (m_selectedTypeIds.isEmpty()) {
        m_recommendTable->setRowCount(0);
        return;
    }
    
    ApiResponse response = m_borrowService->getRecommendedTools(m_selectedTypeIds);
    if (response.success) {
        QJsonArray tools = response.data["data"].toArray();
        updateRecommendTable(tools);
    }
}
```

### 4. 执行借用
```cpp
void ToolBorrowPage::onBorrowButtonClicked() {
    // 验证输入
    if (m_selectedToolIds.isEmpty()) {
        QMessageBox::warning(this, "提示", "请选择要借用的工具");
        return;
    }
    
    // 准备请求数据
    QJsonObject data;
    data["userId"] = m_currentUserId;
    data["toolId"] = m_selectedToolId;
    data["quantity"] = m_qtySpinBox->value();
    data["reason"] = "borrow";
    data["expectedReturnTime"] = m_returnTimeEdit->dateTime().toString("yyyy-MM-dd HH:mm:ss");
    data["flowNo"] = m_borrowService->generateFlowNo("borrow");
    
    // 调用Service
    ApiResponse response = m_borrowService->borrowTool(data);
    if (response.success) {
        QMessageBox::information(this, "成功", "借用成功");
        // 刷新界面
        loadRecommendedTools();
        loadBorrowRecords();
    } else {
        QMessageBox::warning(this, "失败", response.errorMessage);
    }
}
```

## 测试清单

### 功能测试
- [ ] 任务类型多选
- [ ] 推荐工具加载
- [ ] 工具表格显示（推荐+全部）
- [ ] 借用数量输入（最大值限制）
- [ ] 预计归还时间选择
- [ ] 流水号自动生成
- [ ] 借用按钮点击
- [ ] 标签页切换

### 数据一致性测试
- [ ] 任务类型数据与Web版一致
- [ ] 推荐工具算法与Web版一致
- [ ] 借用记录字段与Web版一致
- [ ] 流水号格式与Web版一致

### 错误处理测试
- [ ] 任务类型加载失败
- [ ] 推荐工具加载失败
- [ ] 库存不足借用
- [ ] 网络错误

---

**文档版本**: V1.0  
**最后更新**: 2026-06-20 23:45
