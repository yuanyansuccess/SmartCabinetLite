/**
 * @file CabinetSessionDialog.h
 * @brief 普通用户智能柜会话对话框 - 开柜提示 + 关柜差异清单（演示版，全假数据）
 * @author 袁燕
 * @说明 普通用户登录成功后进入本会话，不进管理主界面。
 *   开柜：仅提示"智能柜已开启，可自行借用或归还工具"。
 *   关柜：对比开柜前后柜内工具差异，弹出借用/归还清单供确认。
 *   演示专用面板（模拟工具拿走/放回）默认隐藏，连点3次开柜图标显示。
 */
#ifndef CABINETSESSIONDIALOG_H
#define CABINETSESSIONDIALOG_H

#include <QDialog>
#include <QStackedWidget>
#include <QJsonObject>
#include <QList>
#include <QElapsedTimer>

class QLabel;
class QPushButton;
class QVBoxLayout;
class QScrollArea;

/**
 * @brief 普通用户智能柜会话（全屏模态）
 *
 * 页面0：开柜提示页（大图标 + 欢迎语 + 关闭智能柜按钮）
 * 页面1：关柜清单页（借用/归还差异列表 + 确认关闭按钮）
 */
class CabinetSessionDialog : public QDialog {
    Q_OBJECT
public:
    explicit CabinetSessionDialog(const QJsonObject& user, QWidget* parent = nullptr);

    /// 以全屏方式启动会话并阻塞，会话结束后返回
    void startSession();

protected:
    /// 事件过滤器：开柜图标连点3次显示/隐藏演示面板
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    /// 点击"关闭智能柜"：对比快照，有差异进清单页，无差异直接结束
    void onCloseCabinetClicked();
    /// 清单页"确认关闭"：结束会话
    void onConfirmCloseClicked();
    /// 演示面板：切换某件工具在柜/已取走状态
    void onDemoToolToggled();
    /// 演示面板：标记/取消某件工具"放错位置"
    void onDemoWrongPlaceToggled();

private:
    void buildOpenPage();      // 构建开柜提示页
    void buildResultPage();    // 构建关柜清单页
    void buildDemoPanel();     // 构建演示专用面板（默认隐藏）
    void updateDemoRows();     // 刷新演示面板各行状态文字
    void toggleDemoPanel();    // 显示/隐藏演示面板

    // [2026-09-23] 开柜页"我的任务工具"只读面板（本机组任务类型+在库工具，位置维度）
    struct ToolRow { QString name; QString code; QString pos; };
    struct MyTypeTools { int typeId; QString typeName; QList<ToolRow> tools; };
    void loadMyTaskTools();                  // 读库：本机组全部任务类型及各自在库工具
    void selectTypeTab(int idx);             // 切换任务类型页签
    void rebuildToolCards();                 // 按当前页签重建工具卡片网格（三列）
    QWidget* makeToolCard(const ToolRow& row); // 构建单个只读工具卡片
    QList<MyTypeTools> m_myTypes;            // 任务类型及在库工具数据
    int m_curTypeIdx = 0;                    // 当前选中页签索引
    QList<QPushButton*> m_typeTabBtns;       // 任务类型页签按钮
    QScrollArea* m_toolsScroll = nullptr;    // 工具卡片滚动区
    QLabel* m_stockCountLabel = nullptr;     // "在库 N 件"统计标签

    /// 位号+1生成"错放位置"（A-01-02 → A-01-03），仅演示用
    static QString bumpedPosition(const QString& position);

    QJsonObject m_user;              // 当前登录用户
    QStackedWidget* m_stack = nullptr;

    // 页面0：开柜提示
    QLabel* m_iconLabel = nullptr;   // 开柜大图标（连点3次入口）

    // 页面1：关柜清单
    QVBoxLayout* m_borrowListLayout = nullptr;   // 借用清单容器
    QVBoxLayout* m_returnListLayout = nullptr;   // 归还清单容器
    QVBoxLayout* m_alertListLayout = nullptr;    // 告警清单容器（放错位置）
    QLabel* m_noChangeLabel = nullptr;           // 无差异提示
    QWidget* m_borrowBlock = nullptr;            // 借用区块（无差异时隐藏）
    QWidget* m_returnBlock = nullptr;            // 归还区块（无差异时隐藏）
    QWidget* m_alertBlock = nullptr;             // 告警区块（无告警时隐藏）

    // 演示专用面板
    QDialog* m_demoPanel = nullptr;
    QList<QPushButton*> m_demoStatusBtns;        // 每行"在柜/已取走"切换按钮
    QList<QPushButton*> m_demoWrongBtns;         // 每行"错放"标记按钮

    // 模拟数据：柜内工具（名称/编号/位置/开柜时是否在柜/是否错放/错放位置）
    struct MockTool {
        QString name;
        QString code;
        QString position;
        bool inCabinet;
        bool misplaced = false;      // 放错位置标记（演示专用）
        QString wrongPosition;       // 错放到的位置
    };
    QList<MockTool> m_tools;         // 当前柜内状态（演示面板实时修改）
    QList<bool> m_openSnapshot;      // 开柜瞬间快照（对比基准）
    QElapsedTimer m_iconClickTimer;  // 图标连点计时
    int m_iconClickCount = 0;        // 图标连点次数
};

#endif // CABINETSESSIONDIALOG_H
