/**
 * @file SearchBar.h
 * @brief 通用搜索栏组件（圆角边框容器 + 无边框输入框 + 软键盘按钮，全系统统一样式）
 * @author 袁燕
 */
#pragma once
#include <QFrame>
#include <QLineEdit>

class QPushButton;

class SearchBar : public QFrame {
    Q_OBJECT
public:
    explicit SearchBar(QWidget* parent = nullptr);

    /// 内部输入框（页面桥接用：设置占位文字、读写内容、使能等）
    QLineEdit* lineEdit() const { return m_edit; }

    /// 软键盘按钮（页面桥接用：connect 到各自的键盘调起槽函数）
    QPushButton* keyboardButton() const { return m_kbdBtn; }

    /// 输入框左右内边距（默认16px；AlertLogsPage历史样式为14px）
    void setEditPadding(int leftRightPx);

    /// 占位文字
    void setPlaceholderText(const QString& text) { m_edit->setPlaceholderText(text); }

    /// 只读
    void setReadOnly(bool readOnly) { m_edit->setReadOnly(readOnly); }

signals:
    /// 点击软键盘按钮
    void keyboardRequested();

private:
    void setupUI();

    QLineEdit* m_edit = nullptr;
    QPushButton* m_kbdBtn = nullptr;
};
