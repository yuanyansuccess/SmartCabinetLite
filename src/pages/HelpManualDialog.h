/**
 * @file HelpManualDialog.h
 * @brief 操作手册对话框 — 全屏展示系统整体框架与各功能操作说明的内置HTML手册
 * @author 袁燕
 */
#pragma once
#include <QDialog>

class QTextBrowser;
class QPushButton;

class HelpManualDialog : public QDialog {
    Q_OBJECT
public:
    /// @brief 构建全屏手册窗口并加载内置HTML手册
    /// @param parent 父窗口
    explicit HelpManualDialog(QWidget* parent = nullptr);

private:
    void setupUI();  ///< 构建顶部标题栏与手册浏览区

    QTextBrowser* m_browser = nullptr;   ///< 手册内容浏览区（加载qrc内置HTML）
    QPushButton* m_closeBtn = nullptr;   ///< 顶部关闭按钮
};
