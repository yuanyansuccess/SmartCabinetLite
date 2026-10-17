/**
 * @file ToolManagementPage.h
 * @brief 工具管理页面 - 统计卡片导航 + 工具清单查询对话框
 * @author 袁燕
 *   1. 页面中部四张统计卡片：工具总数/在库工具/已借出/已出库
 *   2. 点击"在库工具""已借出"卡片弹出工具清单对话框（搜索+筛选+表格+分页）
 *   3. 工具详情对话框与新增/编辑表单沿用原有实现
 */
#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QEvent>
#include "model/ToolInfo.h"  // ToolInfo含位置信息，用于操作记录按位置过滤

class QLabel;
class QFrame;
class QLineEdit;
class QComboBox;
class QPushButton;
class QTableWidget;
class QTabWidget;
class QVBoxLayout;
class BaseDialog;
struct ToolDetailCtx;  // 工具详情对话框会话上下文（定义于ToolManagementPageDetail.cpp）

namespace Ui { class ToolManagementPage; }

class ToolManagementPage : public QWidget {
    Q_OBJECT
public:
    explicit ToolManagementPage(QWidget* parent = nullptr);
    ~ToolManagementPage();
    void refresh();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;  // 统计卡片点击打开清单对话框

private slots:
    void onAddTool();
    void onEditTool(int toolId);
    void onDeleteTool(int toolId);
    void onSubmitTool();
    void onDetailTool(const ToolInfo& toolInfo);  // 传ToolInfo，含位置信息用于过滤操作记录

private:
    void setupUI();
    void setupStatsCards();    // 统计卡片
    void loadStats();          // 加载统计数据
    void openToolListDialog(const QString& status, const QString& title); // 打开清单对话框
    // 构建详情对话框的工具文档Tab（下载+在线浏览）
    QWidget* createDocumentTab(int toolId, const QString& docPath);
    /** 详情对话框构建（onDetailTool拆解，ctx聚合对话框控件与权威工具数据） */
    ToolInfo resolveToolInfo(const ToolInfo& toolInfo);
    void buildDetailHeader(QVBoxLayout* cl, const ToolInfo& t);
    QTabWidget* createDetailTabWidget();
    void buildToolDetailTab(ToolDetailCtx& ctx);
    void buildOperationRecordTab(ToolDetailCtx& ctx);
    QList<QJsonObject> collectOperationRecords(ToolDetailCtx& ctx);
    void fillRecordTable(QTableWidget* table, const QList<QJsonObject>& records);
    void finalizeDetailDialog(ToolDetailCtx& ctx);
    // 详情页上传工具文档（选文件+校验+复制+更新DB+刷新）
    // 返回: true=上传成功, false=用户取消或上传失败
    bool onUploadDocument(int toolId);

    // 统计卡片（在库/已借出可点击，打开清单对话框）
    QFrame* m_statsCardAll = nullptr;
    QFrame* m_statsCardInStock = nullptr;
    QFrame* m_statsCardBorrowed = nullptr;
    QFrame* m_statsCardCheckedOut = nullptr;
    QLabel* m_labelTotalTools = nullptr;
    QLabel* m_labelInStock = nullptr;
    QLabel* m_labelBorrowed = nullptr;
    QLabel* m_labelCheckedOut = nullptr;

    // 新增/编辑工具表单弹窗（统一圆角风格）
    BaseDialog* m_toolDialog = nullptr;
    QLineEdit* m_dlgName = nullptr;
    QLineEdit* m_dlgCode = nullptr;
    QComboBox* m_dlgCategory = nullptr;
    QLineEdit* m_dlgPosition = nullptr;
    QLineEdit* m_dlgUnit = nullptr;  // 数量恒为 1：一个位置只放一个工具
    QLineEdit* m_dlgSpec = nullptr;
    QPushButton* m_dlgSaveBtn = nullptr;
    int m_editToolId = 0;

    Ui::ToolManagementPage* ui = nullptr;  // 静态布局（ToolManagementPage.ui）
};
