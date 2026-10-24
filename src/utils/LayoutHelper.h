/**
 * @file LayoutHelper.h
 * @brief 布局清理工具 - 统一"清空布局后重新填充"的写法
 * @author 袁燕
 *
 * 多个页面在重建按钮区、卡片区、列表区时都要先把旧内容清空再重新添加，
 * 本文件提供两种清空方式，页面直接调用即可，不必各写一遍取项-释放循环。
 */
#pragma once

#include <QLayout>
#include <QLayoutItem>
#include <QWidget>

class LayoutHelper {
public:
    /**
     * @brief 清空布局中的所有布局项，控件本身不删除、归属关系保持不变
     *        适用于"清掉旧按钮再摆新按钮"这类只关心布局结构的场景
     * @param layout 目标布局；为空时不做任何处理
     * @return 无
     */
    static void clearItems(QLayout* layout)
    {
        if (!layout) {
            return;
        }
        while (QLayoutItem* item = layout->takeAt(0)) {
            delete item;
        }
    }

    /**
     * @brief 清空布局中的所有布局项，并让其中的控件在事件循环结束后自行销毁
     *        适用于"清掉旧卡片再摆新卡片"这类需要连控件一起换掉的场景
     * @param layout 目标布局；为空时不做任何处理
     * @return 无
     */
    static void clearItemsAndDeleteWidgets(QLayout* layout)
    {
        if (!layout) {
            return;
        }
        while (QLayoutItem* item = layout->takeAt(0)) {
            if (QWidget* widget = item->widget()) {
                widget->deleteLater();
            }
            delete item;
        }
    }
};
