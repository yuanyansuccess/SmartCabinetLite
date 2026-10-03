/**
 * @file PaginationBar.h
 * @brief 通用分页栏组件（上一页/页码/下一页/共N条，全系统统一样式）
 * @author 袁燕
 */
#pragma once
#include <QWidget>

class QPushButton;
class QLabel;

class PaginationBar : public QWidget {
    Q_OBJECT
public:
    explicit PaginationBar(QWidget* parent = nullptr);

    /// 更新页码显示与按钮使能状态（页码格式"第 x/y 页"，y最小为1）
    void setPageInfo(int currentPage, int totalPages);

    /// 更新总条数显示（格式"共 N 条"）
    void setTotalRecords(int totalRecords);

    // ── 只读访问器（单元测试与外层布局读取当前展示状态用） ──
    QString pageText() const;       ///< 当前页码文本
    QString totalText() const;      ///< 当前总条数文本
    bool prevEnabled() const;       ///< 上一页按钮是否可用
    bool nextEnabled() const;       ///< 下一页按钮是否可用

signals:
    /// 点击上一页
    void prevClicked();
    /// 点击下一页
    void nextClicked();

private:
    void setupUI();

    QPushButton* m_prevBtn = nullptr;
    QLabel* m_pageLabel = nullptr;
    QPushButton* m_nextBtn = nullptr;
    QLabel* m_totalLabel = nullptr;
};
