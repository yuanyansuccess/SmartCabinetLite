/**
 * @file SingleSelectFilter.h
 * @brief 通用单选筛选组件（CheckBox弹出面板样式，但单选行为）
 * @author 袁燕
 * @说明 2026-06-24 从MultiSelectFilter派生，用于替换筛选栏QComboBox
 *   - 外观与MultiSelectFilter完全一致（CheckBox弹出面板）
 *   - 行为是单选：点击任意项即选中该项并关闭面板
 *   - 无"清空"和"确定"按钮（点击即生效，不需要二次确认）
 *   - 所有页面筛选栏的单选下拉框统一使用此组件
 */
#pragma once
#include <QWidget>
#include <QPushButton>
#include <QDialog>
#include <QCheckBox>
#include <QStringList>
#include <QList>
#include <QPair>

class SingleSelectFilter : public QWidget {
    Q_OBJECT
public:
    explicit SingleSelectFilter(const QString& placeholder = QStringLiteral("请选择"), QWidget* parent = nullptr);
    ~SingleSelectFilter();

    /// 设置选项列表（第一项通常为"全部xxx"，选中此项表示不筛选）
    void setOptions(const QStringList& options);

    /// 获取当前选中的文本（单值）
    QString selectedText() const;

    /// 获取当前选中的索引（-1表示未选中，0表示第一项"全部"）
    int selectedIndex() const;

    /// 设置占位文本
    void setPlaceholderText(const QString& text);

    /// 重置为默认（选中第一项"全部"）
    void reset();

    /// 程序化选中指定文本
    void selectText(const QString& text);

    /// 程序化选中指定索引
    void selectIndex(int index);

signals:
    /// 选中项变化时发射（点击即发射，无需确认）
    void selectionChanged(const QString& selected);

private slots:
    void onFilterBtnClicked();

private:
    void updateButtonText();
    void rebuildPopup();

    QPushButton* m_filterBtn;
    QDialog* m_popup;
    QString m_placeholder;
    QStringList m_options;
    int m_selectedIndex = 0;  // 默认选中第一项（"全部"）
    bool m_popupVisible = false;
};
