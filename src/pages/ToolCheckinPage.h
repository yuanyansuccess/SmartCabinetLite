/**
 * @file ToolCheckinPage.h
 * @brief 工具入库页面 - 双选项卡（入库管理 + 入库记录）+ 四步对话框流程
 * @author 袁燕
 *
 * 去掉步骤条流程提示，改为对话框引导四步流程
 *   1. 柜体/层号/位置/工具类型改为下拉选择
 *   2. 确认入库→弹出入库清单对话框
 *   3. 柜体打开动画（三点跳动）
 *   4. 假异常告警提示
 *   5. 入库成功
 * 模仿出库页面风格，增加双Tab结构：入库管理 + 入库记录
 *   入库成功后写sys_operation_log(operation_type='checkin')，入库记录Tab展示历史
 */
#pragma once
#include <QWidget>
#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
class QTableWidget;  // 前置声明：仅指针使用，降低编译耦合
#include <QLabel>
#include <QTabWidget>
#include <QJsonObject>
#include <QJsonArray>  // 可用位置列表

class PaginationBar;  // 通用分页栏组件

namespace Ui { class ToolCheckinPage; }  // Qt Designer 生成的静态布局

class ToolCheckinPage : public QWidget {
    Q_OBJECT
public:
    explicit ToolCheckinPage(QWidget* parent = nullptr);
    ~ToolCheckinPage();
    void refresh();
    void setUser(const QJsonObject& user) { m_user = user; }  // 设置当前登录用户

private slots:
    void onReset();
    void onSubmit();
    void onTabChanged(int index);  // Tab切换时加载数据
    void onRecordPrevPage();  // 入库记录上一页
    void onRecordNextPage();  // 入库记录下一页

private:
    void setupUI();
    bool validateForm(QString& errorMsg);
    void updateCabinetBtnStyles();
    void loadLocalMachineGroup();
    void loadCategoryOptions();  // 加载工具类型下拉选项
    void loadCabinetOptions();  // 加载入库柜体下拉选项
    void loadToolsByCategory(int categoryId);  // 按工具类型加载对应工具下拉
    void onCategoryChanged();  // 工具类型切换时刷新工具下拉
    void onToolSelected();  // 选择工具后自动填充信息
    void loadCheckinRecords();  // 加载入库历史记录
    // 移除generateUniqueToolCode：入库不创建新品类，只更新现有工具

    // 四步入库流程
    void showCheckinListDialog();        // 步骤1: 入库清单确认
    void showCheckinDrawerOpeningDialog(); // 步骤2: 柜体打开中（三点动画）
    void showCheckinWarningDialog();     // 步骤3: 假异常告警
    void showCheckinSuccessDialog();     // 步骤4: 入库成功（写DB+记日志）

    // Tab容器
    QTabWidget* m_tabWidget = nullptr;

    // 表单控件
    QLineEdit* m_toolNameEdit = nullptr;
    QLineEdit* m_toolCodeEdit = nullptr;
    QLineEdit* m_specEdit = nullptr;
    QComboBox* m_qtyCombo = nullptr;    // 入库数量可选1~N，N=空闲位置数
    QLabel* m_qtyHintLabel = nullptr;  // 入库数量最大提示（空闲位置数）
    QComboBox* m_cabinetCombo = nullptr;     // 入库柜体下拉
    QComboBox* m_layerCombo = nullptr;       // 层号下拉
    QComboBox* m_positionCombo = nullptr;    // 位置下拉
    QComboBox* m_categoryCombo = nullptr;    // 工具类型下拉
    // 新增：工具选择下拉（按工具类型关联选择对应工具）
    QComboBox* m_toolSelectCombo = nullptr;  // 工具选择下拉
    int m_selectedToolId = 0;                // 当前选中的工具ID
    QLabel* m_machineGroupLabel = nullptr;
    int m_localMachineGroupId = 0;

    // 入库数量可选1~N，N=该工具在对照表中的空闲位置数
    // 同一工具有多个空闲位置时，用户选择入库数量，每件放到不同位置
    // 入库位置从映射表选取，一个位置对应一个工具
    QJsonArray m_availablePositions;
    int m_selectedCheckinQty = 1;  // 用户选择的入库数量

    // 状态
    QLabel* m_errorLabel = nullptr;
    QPushButton* m_submitBtn = nullptr;
    QPushButton* m_resetBtn = nullptr;

    // 入库记录表格 + 分页
    QTableWidget* m_recordTable = nullptr;
    PaginationBar* m_recordPaginationBar = nullptr;  // 通用分页栏
    int m_recordCurrentPage = 1;
    int m_recordPageSize = 20;
    int m_recordTotalRecords = 0;

    // 当前登录用户信息（用于入库记录操作人）
    QJsonObject m_user;

    Ui::ToolCheckinPage* ui = nullptr;  // 静态布局（ToolCheckinPage.ui）
};
