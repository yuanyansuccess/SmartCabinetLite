/**
 * @file FlowDialogs.h
 * @brief 借用/归还/入库/出库四类页面共用的对话框骨架
 * @author 袁燕
 *
 * 清单确认对话框由本类统一构建（标题栏 + 提示条 + 清单表格 + 底部按钮），
 * 页面只负责数据与文案，保证四类流程的样式一致。
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
class QAbstractScrollArea;

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
     * @param setup  骨架参数（height 为按内容计算的期望高度，屏幕封顶在此统一处理）
     * @param body   输出主布局指针，调用方继续往里添加内容
     * @param outEffectiveHeight 可选输出：回写封顶后的窗口有效高度，供 fillTable 反算表格高度；不需要时传空
     * @return 新建的对话框（未执行模态）
     */
    static QDialog* createDialog(QWidget* parent, const Setup& setup, QVBoxLayout** body,
                                 int* outEffectiveHeight = nullptr);

    /**
     * @brief 按屏幕可用高度给弹窗高度封顶
     * @param parent 父窗口，为空时取主屏
     * @param wanted 按内容算出的期望高度
     * @param minHeight 弹窗高度下限
     * @param screenMargin 屏幕上下预留余量
     * @return 封顶后的弹窗高度
     */
    static int fitDialogHeight(QWidget* parent, int wanted, int minHeight,
                              int screenMargin = 80);

    /**
     * @brief 给清单表格或滚动区域接入按像素滚动与触屏拖动手势
     * @param area 表格或滚动区域，为空时不做处理
     */
    static void enableTouchScroll(QAbstractScrollArea* area);

    /**
     * @brief 追加标题栏（图标 + 标题 + 弹性空白）
     * @param body  主布局
     * @param icon  图标文本，如 📋
     * @param title 标题文本
     */
    static void addTitle(QVBoxLayout* body, const QString& icon, const QString& title);

    /**
     * @brief 创建统一样式的清单表格
     * @param headers 表头文本，个数即列数，列宽自适应内容等比拉伸
     * @return 表格对象（调用方需自行加入布局）
     * @note 表格按像素滚动并接入触屏手势，条目多时可直接手指拖动查看全部
     */
    static QTableWidget* createListTable(const QStringList& headers);

    /**
     * @brief 填充清单表格并按窗口可用高度确定表格高度
     * @param table          表格对象
     * @param rows           每行单元格文本，列数需与表头一致
     * @param placeholder    单元格为空时显示的占位文本，为空则显示空串
     * @param maxTableHeight 骨架回写的窗口有效高度；大于 0 时按窗口可用高度反算表格高度
     *                       （多余行由表格滚动承接，底部按钮不被挤出），-1 沿用原有按行数的高度
     */
    static void fillTable(QTableWidget* table, const QList<QStringList>& rows,
                          const QString& placeholder = QString(), int maxTableHeight = -1);

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
