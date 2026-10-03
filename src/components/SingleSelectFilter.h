/**
 * @file SingleSelectFilter.h
 * @brief 通用单选筛选组件（CheckBox弹出面板样式，但单选行为）
 * @author 袁燕
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
    void setupUI();  // 构建筛选按钮与弹出面板

    void updateButtonText();
    void rebuildPopup();

    QPushButton* m_filterBtn;
    QDialog* m_popup;
    QString m_placeholder;
    QStringList m_options;
    int m_selectedIndex = 0;  // 默认选中第一项（"全部"）
    bool m_popupVisible = false;
};
