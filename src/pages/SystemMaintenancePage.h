/**
 * @file    SystemMaintenancePage.h
 * @author  袁燕
 * @brief   系统维护页面 — 任务配置/工具维护/工具对照关系维护
 *
 * 新建系统维护页面
 *   闭环：任务配置→工具借用推荐 / 工具维护→增删改 / 对照关系→位置映射
 * 排布对齐系统设置 + 对照关系支持手动选择
 * 改为Tab选项卡布局（QStackedWidget），去掉滚动
 *   触屏友好：点击Tab切换页面，不用滑轮滚动
 *   样式对齐系统设置：灰底白选中+主色下划线
 */
#pragma once

#include <QWidget>
class QTableWidget;  // 前置声明：仅指针使用，降低编译耦合
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>

class QVBoxLayout;

namespace Ui { class SystemMaintenancePage; }  // Qt Designer 生成的静态布局

class SystemMaintenancePage : public QWidget {
    Q_OBJECT
public:
    explicit SystemMaintenancePage(QWidget* parent = nullptr);
    ~SystemMaintenancePage();
    void refresh();

protected:
    // Tab点击事件过滤
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Tab选项卡栏
    void updateTabStyles();
    void switchTab(int index);

    // 任务配置面板
    void loadTaskTypes();
    void loadTaskTools(int typeId);
    void saveTaskToolConfig();
    // 任务工具增删改
    void onAddTaskTool();           // 新增任务工具
    void onEditTaskTool(int row);   // 修改任务工具
    void onDeleteTaskTool(int row); // 删除任务工具

    // 工具维护面板
    void loadAllTools();
    void onAddTool();
    void onEditTool(int toolId);
    void onDeleteTool(int toolId);
    void onSubmitTool();
    void ensureToolDialogCreated();  // 确保对话框已创建(不弹出)，供onEditTool复用

    // 工具对照关系面板
    void loadPositionMappings();
    void loadAvailablePositions();
    void loadUnboundTools();
    void onBindPosition();
    void onClearPosition(int mappingId);  // 按映射记录ID清除（一工具可多位置）

    // Tab选项卡
    QList<QLabel*> m_tabLabels;
    QStackedWidget* m_stackedWidget = nullptr;
    int m_activeTabIndex = 0;

    // 任务配置面板
    QComboBox* m_taskTypeCombo = nullptr;
    QTableWidget* m_taskToolTable = nullptr;

    // 工具维护面板
    QTableWidget* m_toolTable = nullptr;
    int m_editToolId = 0;
    QLineEdit* m_dlgName = nullptr;
    QLineEdit* m_dlgCode = nullptr;
    QComboBox* m_dlgCategory = nullptr;
    QLineEdit* m_dlgSpec = nullptr;
    QLineEdit* m_dlgUnit = nullptr;
    QComboBox* m_dlgSupplier = nullptr;  // 供应商
    QComboBox* m_dlgRecognition = nullptr;  // 识别方式
    QLineEdit* m_dlgDocumentEdit = nullptr;  // 工具文档路径显示
    QPushButton* m_dlgUploadBtn = nullptr;  // 上传文档按钮
    QString m_dlgDocumentPath;  // 已上传文档本地路径
    class BaseDialog* m_toolDialog = nullptr;

    void onUploadDocument();  // 上传工具文档

    // 对照关系面板
    QTableWidget* m_mappingTable = nullptr;
    QComboBox* m_posCabinetCombo = nullptr;
    QComboBox* m_posLayerCombo = nullptr;
    QComboBox* m_posPositionCombo = nullptr;
    QComboBox* m_posToolCombo = nullptr;

    Ui::SystemMaintenancePage* ui = nullptr;  // 静态布局（SystemMaintenancePage.ui）
};
