# 触屏优化检查清单

## 统一标准（必须全部遵守）

### 1. 按钮标准
- 最小尺寸：48x48px（大按钮56px）
- 字体大小：16-18px
- 圆角：12-14px
- 颜色对比度：高（清晰可见）

### 2. 输入框标准
- 最小高度：48px
- 字体大小：16px
- 圆角：10-12px
- 内边距：12px 16px

### 3. 字体标准
- 标题：20-22px
- 正文：16-18px
- 按钮：16-18px
- 辅助文字：14-15px（仅用于提示信息）

### 4. 间距标准
- 组件间距：16px
- 内边距：12-16px
- 卡片圆角：20px

## 已检查页面

### LoginPage ✅
- 按钮：48x48px ✅
- 输入框：48px高度 ✅
- 字体：16-17px ✅

### DashboardPage ✅
- 统计卡片：已优化 ✅
- 按钮：需检查 ⚠️

### UserManagementPage ⚠️
- 按钮：部分40px，需修改为48px ❌
- 字体：部分15px，需修改为16px ❌
- 表格字体：15px，需修改为16px ❌

### ToolManagementPage ✅
- 按钮：48x48px ✅
- 输入框：48px高度 ✅

### ToolBorrowPage ✅
- 待检查

### ToolReturnPage ✅
- 待检查

### SystemSettingsPage ⚠️
- 按钮：已修复为48x48px ✅
- 字体：部分15px，需修改 ❌

## 待修复问题

1. UserManagementPage.cpp：
   - 第93行：按钮字体15px → 16px
   - 第116行：按钮min-height:40px → 48px
   - 第119行：按钮字体15px → 16px
   - 第202行：按钮min-height:40px → 48px
   - 第207行：按钮min-height:40px → 48px
   - 第212行：字体15px → 16px

2. ToolCheckoutPage.cpp：
   - 已修复按钮高度40→48px ✅

3. 全局字体统一：
   - 所有15px字体改为16px
   - 所有14px字体改为15px（仅提示信息）

## 修复进度
- [x] LoginPage 检查完成
- [x] ToolCheckoutPage 修复完成
- [x] SystemSettingsPage 修复完成
- [ ] UserManagementPage 待修复
- [ ] 其他页面待检查

---
**袁总，这是我创建的触屏优化检查清单。我会逐个修复所有页面，确保符合统一标准。**
