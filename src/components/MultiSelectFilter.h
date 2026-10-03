/**
 * @file MultiSelectFilter.h
 * @brief 通用多选筛选组件（按钮+弹出复选面板，选中后点确定生效）
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

class MultiSelectFilter : public QWidget {
    Q_OBJECT
public:
    explicit MultiSelectFilter(const QString& placeholder = QStringLiteral("请选择"), QWidget* parent = nullptr);
    ~MultiSelectFilter();

    /// 设置选项列表
    void setOptions(const QStringList& options);

    /// 获取当前选中的选项
    QStringList selectedOptions() const;

    /// 设置占位文本
    void setPlaceholderText(const QString& text);

    /// 清空所有选中
    void clear();

    /// 设置全部选中
    void selectAll();

signals:
    /// 选项变化时发射（仅在用户点确定后发射）
    void selectionChanged(const QStringList& selected);

private slots:
    void onFilterBtnClicked();
    void onClear();
    void onConfirm();

private:
    void setupUI();  // 构建筛选按钮与弹出面板

    void updateButtonText();

    QPushButton* m_filterBtn;
    QDialog* m_popup;
    QString m_placeholder;
    QList<QPair<QString, QCheckBox*>> m_checkBoxes;
    QList<QString> m_selectedCache;  // 上次确认的选中值，用于popup取消时恢复
    bool m_visible = false;
};
