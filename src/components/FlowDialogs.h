/**
 * @file FlowDialogs.h
 * @brief 借用/归还/入库/出库四类页面共用的对话框骨架
 * @author 袁燕
 *
 * 四个页面原本各自复制一份"清单确认对话框"，结构与样式完全一致：
 * 无边框对话框 + 标题栏 + 浅蓝提示条 + 清单表格 + 取消/下一步按钮。
 * 抽到此处统一维护，页面只负责数据与文案，避免样式分叉。
 */
#pragma once

#include <QString>
#include <QStringList>
#include <QList>
#include <functional>

class QDialog;
class QVBoxLayout;
class QTableWidget;
class QWidget;

class FlowDialogs {
public:
    /**
     * @brief 对话框骨架参数
     */
    struct Setup {
        QString windowTitle;      // 窗口标题（任务栏可见）
        int     width   = 560;    // 宽度
        int     height  = 460;    // 高度
        int     spacing = 14;     // 主布局行间距
        int     margin  = 32;     // 主布局左右边距
        int     footerSpacing = 12;      // 底部按钮间距
        int     cancelWidth   = 110;     // 取消按钮最小宽度
        int     actionWidth   = 140;     // 主操作按钮最小宽度
        int     buttonHeight  = -1;      // 按钮高度，-1 表示用 Design Token 的标准高度
    };

    /**
     * @brief 创建统一样式的无边框对话框
     * @param parent 父对象
     * @param setup  骨架参数
     * @param body   输出主布局指针，调用方继续往里添加内容
     * @return 新建的对话框（未执行模态）
     */
    static QDialog* createDialog(QWidget* parent, const Setup& setup, QVBoxLayout** body);

    /**
     * @brief 追加标题栏（图标 + 标题 + 弹性空白）
     * @param body  主布局
     * @param icon  图标文本，如 📋
     * @param title 标题文本
     */
    static void addTitle(QVBoxLayout* body, const QString& icon, const QString& title);

    /**
     * @brief 追加浅蓝提示条（图标 + 一行说明）
     * @param body    主布局
     * @param icon    图标文本，如 📋
     * @param text    提示文字
     * @param padding 提示条上下内边距，默认 8
     */
    static void addTipBar(QVBoxLayout* body, const QString& icon, const QString& text, int padding = 8);

    /**
     * @brief 创建统一样式的清单表格
     * @param headers 表头文本，个数即列数，列宽自适应内容等比拉伸
     * @return 表格对象（调用方需自行加入布局）
     */
    static QTableWidget* createListTable(const QStringList& headers);

    /**
     * @brief 填充清单表格
     * @param table       表格对象
     * @param rows        每行单元格文本，列数需与表头一致
     * @param placeholder 单元格为空时显示的占位文本，为空则显示空串
     */
    static void fillTable(QTableWidget* table, const QList<QStringList>& rows,
                          const QString& placeholder = QString());

    /**
     * @brief 追加底部按钮栏（取消 + 主操作）
     * @param dlg        对话框，用于关闭自身
     * @param body       主布局
     * @param setup      骨架参数（读取按钮宽度、高度与间距）
     * @param actionText 主操作按钮文案
     * @param onAccepted 主操作被点击后的回调（对话框已接受）
     * @param cancelText 取消按钮文案
     */
    static void addFooter(QDialog* dlg, QVBoxLayout* body, const Setup& setup,
                          const QString& actionText,
                          const std::function<void()>& onAccepted,
                          const QString& cancelText = QStringLiteral("取消"));
};
