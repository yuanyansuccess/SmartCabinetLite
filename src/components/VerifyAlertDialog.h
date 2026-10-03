/**
 * @file VerifyAlertDialog.h
 * @brief 校验异常对话框（借用/归还/入库/出库四流程共用的步骤3骨架）
 * @author 袁燕
 * @说明 统一承载：⚠️图标+右上角倒计时（配置分钟数，到时自动done(2)）、警告标题、
 *   异常详情、忽略（红）/取消/确认三按钮。倒计时结束与点击忽略均返回 done(2)，
 *   页面在 exec() 返回后写入告警（AlertDAO）并执行各自的重置逻辑。
 */
#pragma once
#include <QDialog>

class QLabel;
class QTimer;
class QHBoxLayout;

class VerifyAlertDialog : public QDialog {
    Q_OBJECT
public:
    /// @param windowTitle 窗口内部标题（如"工具核对"）
    /// @param title       警告标题（如"工具核对异常"，橙色22px）
    /// @param descHtml    异常详情（富文本）
    /// @param descStyle   详情样式（各页历史值不同，逐字传入）
    /// @param cancelText  取消按钮文字（取消借用/归还/入库/出库）
    /// @param confirmText 确认按钮文字（"✓ 确认完成核对"或"🔄 重新校验"）
    /// @param confirmMinWidth 确认按钮最小宽度（160/150）
    /// @param bufferMinutes 忽略倒计时分钟数（由调用方从 AppConfig 读取后传入，
    ///        组件层不依赖配置层；<=0 表示不显示倒计时）
    explicit VerifyAlertDialog(const QString& windowTitle, const QString& title,
                               const QString& descHtml, const QString& descStyle,
                               const QString& cancelText, const QString& confirmText,
                               int confirmMinWidth, int bufferMinutes,
                               QWidget* parent = nullptr);

private:
    void buildCountdown(QHBoxLayout* topRow);

    int m_bufferMinutes = 0;  ///< 忽略倒计时分钟数（由调用方传入，组件不读配置）
    QLabel* m_countdownLabel = nullptr;
    QTimer* m_timer = nullptr;
    int m_remainSeconds = 0;
};
