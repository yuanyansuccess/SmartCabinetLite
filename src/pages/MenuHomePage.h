/**
 * @file MenuHomePage.h
 * @brief 功能导航主页 - 登录后的大按钮入口页，管理员与普通用户共用
 * @author 袁燕
 * @说明 页面只负责展示卡片与发出点击信号，按钮内容由 MainWindow 按登录角色注入；
 *   点击后的跳转/弹窗动作也由 MainWindow 统一处理，本页面不连数据库、不读配置。
 */
#ifndef MENUHOMEPAGE_H
#define MENUHOMEPAGE_H

#include <QWidget>
#include <QList>
#include <QString>

class QLabel;
class QGridLayout;
class QResizeEvent;

class MenuHomePage : public QWidget {
    Q_OBJECT
public:
    /// 导航卡片数据：key为页面注册名或动作标识，icon为emoji图标，label为按钮文字
    struct MenuCard { QString key; QString icon; QString label; };

    explicit MenuHomePage(QWidget* parent = nullptr);

    /// 设置欢迎语中的用户姓名（显示"您好，×××"）
    /// @param name 用户姓名；为空时只显示"您好"
    void setUserName(const QString& name);

    /// 重建导航卡片网格：不超过4张时单行居中，超过4张按4列多行排布
    /// @param cards 卡片数据列表（由 MainWindow 按登录角色组装）
    void setCards(const QList<MenuCard>& cards);

signals:
    /// 用户点击某张功能卡片
    /// @param key 所点卡片的标识（页面注册名或普通用户动作标识）
    void cardClicked(const QString& key);

private:
    void buildBaseUI();       ///< 构建欢迎语/卡片容器/告警提示等静态骨架
    void rebuildCards();      ///< 按 m_cards 数据重建卡片按钮网格
    QSize computeCardSize() const;       ///< 按页面可用宽计算卡片尺寸(保持240:225比例)
    void resizeEvent(QResizeEvent* event) override;  ///< 宽度变化时重算卡片尺寸
    static QString cardStyle(int w, int h);  ///< 卡片按钮统一样式(w/h为尺寸约束)

    QLabel* m_welcomeLabel = nullptr;    // "您好，×××"
    QLabel* m_hintLabel = nullptr;       // 副标题"请选择您要进行的操作"
    QWidget* m_cardArea = nullptr;       // 卡片网格容器（水平居中包裹）
    QGridLayout* m_cardGrid = nullptr;   // 卡片网格（4列）
    QList<MenuCard> m_cards;             // 当前卡片数据（setCards暂存，重建时使用）
    QSize m_cardSize;                    // 当前卡片尺寸(用于resize去抖，避免无谓重建)
};

#endif // MENUHOMEPAGE_H
