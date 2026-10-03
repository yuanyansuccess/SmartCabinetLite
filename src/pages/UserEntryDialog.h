/**
 * @file UserEntryDialog.h
 * @brief 普通用户功能选择对话框 - "借用/归还" 与 "查询" 二选一入口页
 * @author 袁燕
 * @说明 普通用户登录成功后先进入本选择页（全屏，原"智能柜已开启"页之前）：
 */
#ifndef USERENTRYDIALOG_H
#define USERENTRYDIALOG_H

#include <QDialog>
#include <QJsonObject>

class QLabel;

namespace Ui { class UserEntryDialog; }  // Qt Designer 生成的静态布局

class UserEntryDialog : public QDialog {
    Q_OBJECT
public:
    /// 用户在入口页的选择结果
    enum class Choice { Logout, BorrowReturn, Query };

    explicit UserEntryDialog(const QJsonObject& user, QWidget* parent = nullptr);
    ~UserEntryDialog();

    /// 全屏显示并阻塞，返回用户的选择
    Choice execChoice();

private:
    QJsonObject m_user;
    Choice m_choice = Choice::Logout;

    void showBorrowDetail();  // 查询：弹出当前用户借用明细对话框
    void showAlertDialog();  // 告警日志：弹出系统告警列表对话框
    int  unhandledAlertCount();  // 未处理告警数（提示显隐依据）

    QLabel* m_alertHintLabel = nullptr;  // 存在告警时的醒目提示
    Ui::UserEntryDialog* ui = nullptr;   // 静态布局（UserEntryDialog.ui）
};

#endif // USERENTRYDIALOG_H
