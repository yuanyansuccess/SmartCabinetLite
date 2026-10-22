/**
 * @file ScreenFit.h
 * @brief 全屏对话框铺满屏幕的统一入口（不依赖窗口管理器的全屏能力）
 * @author 袁燕
 *
 * QDialog::showFullScreen() 依赖窗口管理器支持全屏状态（X11 下为
 * _NET_WM_STATE_FULLSCREEN）。麒麟 UKUI 等部分窗口管理器对无边框对话框
 * 可能忽略该请求，窗口退回按内容建议尺寸显示并居中，宽高超出屏幕后四周
 * 被裁（典型现象：顶部顶栏看不到、表格最右侧一列显示不全）。
 * 本入口改为把窗口几何直接设成屏幕矩形，Windows 与麒麟表现一致。
 */
#pragma once

#include <QCursor>
#include <QDebug>
#include <QDialog>
#include <QLayout>
#include <QGuiApplication>
#include <QRect>
#include <QScreen>
#include <QWidget>

namespace UiFit {

/**
 * @brief 让无边框对话框按屏幕矩形铺满并显示
 * @param dlg 目标对话框，须已设置 Qt::Dialog | Qt::FramelessWindowHint 且为模态
 * @param screenOwner 用于确定目标屏幕的控件（通常传父窗口）；为空时先取光标所在屏幕再退回主屏
 * @return 无
 * @note 调用后可直接 exec() 进入模态循环；已显示的窗口重复调用只做几何校准
 */
inline void showFullScreenFit(QDialog* dlg, QWidget* screenOwner = nullptr) {
    if (!dlg) {
        return;
    }

    QScreen* screen = screenOwner ? screenOwner->screen() : nullptr;
    if (!screen) {
        screen = QGuiApplication::screenAt(QCursor::pos());
    }
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    const QRect screenRect = screen ? screen->geometry() : QRect(0, 0, 1024, 768);

    // 内容较宽时布局会把窗口的最小尺寸撑到内容宽度（表格列多时最明显），
    // 必须先解除布局约束，否则窗口无法缩小到屏幕宽度、右侧内容被裁
    if (QLayout* contentLayout = dlg->layout()) {
        contentLayout->setSizeConstraint(QLayout::SetNoConstraint);
    }
    dlg->setWindowState(Qt::WindowNoState);
    dlg->setMinimumSize(0, 0);
    dlg->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

    dlg->setGeometry(screenRect);
    dlg->show();
    // 支持标准全屏的窗口管理器（如 Windows）借此隐藏任务栏；
    // 不支持的窗口管理器忽略该请求，下面的几何设置仍保证窗口铺满屏幕
    dlg->setWindowState(Qt::WindowFullScreen);
    dlg->setGeometry(screenRect);
    dlg->raise();
    dlg->activateWindow();

    qInfo() << "[ScreenFit] 全屏目标" << screenRect << "实际几何" << dlg->geometry();
}

}  // namespace UiFit
