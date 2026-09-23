# ToolManagementPage 业务逻辑文档

**作者**: 袁燕  
**日期**: 2026-06-21  
**目标**: 1:1复刻Web前端ToolManagement.vue

## 页面概述

工具管理页面（仅管理员），支持：
1. **工具列表**：编号/名称/规格/柜体/位置/总数/在库/状态
2. **搜索筛选**：关键词 + 分类 + 状态
3. **新增/编辑/删除工具**
4. **分页功能**

## 数据结构

### 1. 工具信息（Tool）
```cpp
struct Tool {
    int toolId;           // 工具ID
    QString toolCode;       // 工具编号
    QString toolName;       // 工具名称
    QString spec;           // 规格
    QString category;       // 分类
    QString cabinet;        // 柜体
    QString position;       // 位置
    int totalQty;          // 总数
    int inStockQty;       // 在库数量
    QString status;         // 状态（in_stock/borrowed/maintenance）
    QString createTime;     // 创建时间
};
```

---

## Service接口说明

### 1. 获取工具列表
**Service**: `ToolService::getTools(params)`
**对应Web API**: `GET /api/tools`

**请求**:
```cpp
QJsonObject params;
params["keyword"] = "扳手";     // 关键词搜索
params["category"] = "工具";   // 分类筛选
params["status"] = "in_stock";  // 状态筛选
params["page"] = 1;
params["pageSize"] = 20;

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
            "category": "工具",
            "cabinet": "A柜",
            "position": "1层01",
            "totalQty": 10,
            "inStockQty": 8,
            "status": "in_stock",
            "createTime": "2026-06-20 10:00:00"
        }
    ],
    "total": 50,
    "page": 1,
    "pageSize": 20
}
```

---

### 2. 新增工具
**Service**: `ToolService::addTool(data)`
**对应Web API**: `POST /api/tools`

**请求**:
```cpp
QJsonObject data;
data["toolCode"] = "T001";
data["toolName"] = "扳手";
data["spec"] = "10mm";
data["category"] = "工具";
data["cabinet"] = "A柜";
data["position"] = "1层01";
data["totalQty"] = 10;

ApiResponse addTool(const QJsonObject& data);
```

---

### 3. 更新工具
**Service**: `ToolService::updateTool(id, data)`
**对应Web API**: `PUT /api/tools/{id}`

**请求**:
```cpp
int toolId = 1;
QJsonObject data;
data["toolName"] = "更新名称";
data["spec"] = "12mm";

ApiResponse updateTool(int id, const QJsonObject& data);
```

---

### 4. 删除工具
**Service**: `ToolService::deleteTool(id, operatorId)`
**对应Web API**: `DELETE /api/tools/{id}`

**请求**:
```cpp
ApiResponse deleteTool(int id, int operatorId);
```

---

### 5. 获取工具分类列表
**Service**: `ToolService::getCategories()`
**对应Web API**: `GET /api/categories`

**响应**:
```json
{
    "success": true,
    "data": ["工具", "设备", "耗材"]
}
```

---

## UI组件映射

| Web组件 | Qt组件 | 说明 |
|---------|--------|------|
| `<el-table>` | `QTableWidget` | 工具表格（8列） |
| `<el-input>` | `QLineEdit` | 关键词搜索 |
| `<el-select>` | `QComboBox` | 分类/状态下拉 |
| `<el-dialog>` | `QDialog` | 新增/编辑弹窗 |
| `<el-button>` | `QPushButton` | 操作按钮 |
| `<el-pagination>` | 自定义 | 分页组件 |

---

## 业务逻辑流程

### 流程1：加载工具列表
1. 页面显示时，调用`getTools(params)`
2. 支持筛选：关键词/分类/状态
3. 填充表格：编号/名称/规格/柜体/位置/总数/在库/状态/操作
4. 支持分页

### 流程2：新增工具
1. 点击"新增"按钮，弹出对话框
2. 输入工具信息（编号/名称/规格/分类/柜体/位置/总数）
3. 点击"确定"，调用`addTool(data)`
4. 成功后刷新表格

### 流程3：编辑工具
1. 点击操作列的"编辑"按钮
2. 弹出对话框，加载工具信息
3. 修改信息后点击"确定"，调用`updateTool(id, data)`
4. 成功后刷新表格

### 流程4：删除工具
1. 点击操作列的"删除"按钮
2. 弹出确认对话框
3. 确认后调用`deleteTool(id, operatorId)`
4. 成功后刷新表格

---

## 信号槽设计

### ToolService信号
```cpp
// 工具列表加载完成
void toolsLoaded(const QJsonArray& tools, int total);

// 工具操作成功
void toolOperationSuccess(const QString& message);

// 工具操作失败
void toolOperationFailed(const QString& errorMessage);
```

### ToolManagementPage槽函数
```cpp
// 处理工具列表加载完成
void onToolsLoaded(const QJsonArray& tools, int total);

// 处理工具操作成功
void onToolOperationSuccess(const QString& message);

// 处理工具操作失败
void onToolOperationFailed(const QString& errorMessage);
```

---

## 关键代码片段

### 1. 加载工具列表
```cpp
void ToolManagementPage::loadTools() {
    QJsonObject params;
    params["keyword"] = m_searchEdit->text();
    params["category"] = m_categoryCombo->currentText();
    params["status"] = m_statusCombo->currentData().toString();
    params["page"] = m_currentPage;
    params["pageSize"] = m_pageSize;
    
    ApiResponse response = m_toolService->getTools(params);
    if (response.success) {
        QJsonArray tools = response.data["data"].toArray();
        int total = response.data["total"].toInt();
        updateTable(tools, total);
    } else {
        QMessageBox::warning(this, "错误", response.errorMessage);
    }
}
```

### 2. 新增工具
```cpp
void ToolManagementPage::onAddButtonClicked() {
    // 显示新增对话框
    if (m_toolDialog->exec() == QDialog::Accepted) {
        QJsonObject data;
        data["toolCode"] = m_dlgCodeEdit->text();
        data["toolName"] = m_dlgNameEdit->text();
        data["spec"] = m_dlgSpecEdit->text();
        data["category"] = m_dlgCategoryCombo->currentText();
        data["cabinet"] = m_dlgCabinetEdit->text();
        data["position"] = m_dlgPositionEdit->text();
        data["totalQty"] = m_dlgQtySpin->value();
        
        ApiResponse response = m_toolService->addTool(data);
        if (response.success) {
            QMessageBox::information(this, "成功", "工具新增成功");
            loadTools();  // 刷新表格
        } else {
            QMessageBox::warning(this, "失败", response.errorMessage);
        }
    }
}
```

---

## 测试清单

### 功能测试
- [ ] 工具列表显示
- [ ] 关键词搜索
- [ ] 分类/状态筛选
- [ ] 新增工具
- [ ] 编辑工具
- [ ] 删除工具
- [ ] 分页功能

### 数据一致性测试
- [ ] 工具数据与Web版一致
- [ ] 字段名使用驼峰
- [ ] 时间格式统一

### 错误处理测试
- [ ] 工具加载失败
- [ ] 新增/编辑/删除失败

---

**文档版本**: V1.0  
**最后更新**: 2026-06-21 00:15
