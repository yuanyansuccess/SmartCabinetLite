/**
* @file StyleHelper.h
* @brief 全局样式与格式管理 - 触屏优化统一标准 + 麒麟/Windows双平台适配
* @author 袁燕
*
* 所有页面按钮最小48x48px（大按钮56px），字体16-18px
* 输入框高度48px，字体16px
* 圆角统一：按钮12-14px，输入框10-12px
* 位置格式统一：柜号-层号-位号（两位补零，如A-01-03）
*/
#pragma once

#include <QString>
#include <QStringList>
#include <QFont>
#include <QFontDatabase>
#include <QDebug>
#include <QRegularExpression>

class StyleHelper {
public:
    // ============ 颜色方案 ============
    static QString bgColor()         { return "#f0f2f5"; }
    static QString whiteColor()      { return "#ffffff"; }
    static QString primaryColor()    { return "#4da3ff"; }
    static QString primaryHover()    { return "#3d8ce0"; }
    static QString successColor()    { return "#43a047"; }
    static QString successHover()    { return "#388e3c"; }
    static QString dangerColor()     { return "#e53935"; }
    static QString dangerHover()     { return "#c62828"; }
    static QString warningColor()    { return "#f57c00"; }
    static QString textColor()       { return "#1a1a2e"; }
    static QString textSecondary()   { return "#666666"; }
    static QString borderColor()     { return "#e0e0e0"; }
    static QString purpleColor()     { return "#6c5ce7"; }
    static QString purpleHover()     { return "#5a4bd1"; }

    /// 默认按钮 (白底灰边，用于返回/取消) [2026-06-27] 统一触屏标准: 44px高 14px字体 10px圆角
    static QString buttonDefault() {
        return QString(
            "QPushButton {"
            "  background: #fff; color: #666;"
            "  border: 1px solid #d0d0d0; border-radius: 10px;"
            "  padding: 10px 24px; font-size: 14px; font-weight: 600;"
            "  min-height: 44px; min-width: 80px;"
            "}"
            "QPushButton:hover { background: #f5f5f5; border-color: #b0b0b0; }"
            "QPushButton:pressed { background: #e8e8e8; }"
        );
    }

    // ============ 按钮样式 ============
    /// 主按钮 (蓝色实底，用于保存/确认等主要操作) [2026-06-27] 统一触屏标准: 44px高 14px字体 10px圆角
    static QString buttonPrimary() {
        return QString(
            "QPushButton {"
            "  background: #4da3ff; color: white; border: none; border-radius: 10px;"
            "  padding: 10px 28px; font-size: 14px; font-weight: 700;"
            "  min-height: 44px; min-width: 100px;"
            "}"
            "QPushButton:hover { background: #3d8ae0; }"
            "QPushButton:pressed { background: #2e7bd6; }"
            "QPushButton:disabled { background: #cccccc; color: #888888; }"
        );
    }

    /// 确认按钮 (绿色实底)
    static QString buttonSuccess() {
        return QString(
            "QPushButton {"
            "  background: #43a047; color: white; border: none; border-radius: 10px;"
            "  padding: 10px 28px; font-size: 14px; font-weight: 700;"
            "  min-height: 44px; min-width: 100px;"
            "}"
            "QPushButton:hover { background: #388e3c; }"
            "QPushButton:pressed { background: #2e7d32; }"
            "QPushButton:disabled { background: #cccccc; color: #888888; }"
        );
    }

    /// 轮廓按钮 (白底蓝边，用于辅助操作) [2026-06-27] 统一触屏标准
    static QString buttonOutline() {
        return QString(
            "QPushButton {"
            "  background: white; color: #4da3ff;"
            "  border: 1px solid #4da3ff; border-radius: 10px;"
            "  padding: 10px 24px; font-size: 14px; font-weight: 700;"
            "  min-height: 44px; min-width: 80px;"
            "}"
            "QPushButton:hover { background: #f0f7ff; }"
            "QPushButton:pressed { background: #e6f0ff; }"
        );
    }

    /// 危险按钮 (红色实底)
    static QString buttonDanger() {
        return QString(
            "QPushButton {"
            "  background: %1; color: white; border: none; border-radius: 10px;"
            "  padding: 10px 28px; font-size: 14px; font-weight: 700;"
            "  min-height: 44px; min-width: 100px;"
            "}"
            "QPushButton:hover { background: %2; }"
            "QPushButton:pressed { background: %3; }"
        ).arg(dangerColor(), dangerHover(), "#b71c1c");
    }

    // ============ 输入框样式 ============
    /// [2026-06-27] 统一触屏标准: 48px高 16px字体 10px圆角 1px边框
    static QString lineEdit() {
        return QString(
            "QLineEdit {"
            "  border: 1px solid #d0d0d0; border-radius: 10px;"
            "  padding: 0 14px; font-size: 16px; color: %1;"
            "  background: white; min-height: 48px;"
            "}"
            "QLineEdit:focus { border-color: %2; }"
            "QLineEdit:disabled { background: #f5f5f5; color: #999999; }"
        ).arg(textColor(), primaryColor());
    }

    // ============ 设置页面专用样式（触屏优化标准：48px高，16px字体）============
    // [2026-06-23 统一触屏标准] 38px→48px, 14px→16px，与其他管理页面风格一致
    // [2026-06-24v5] 去掉内部边框+纯白背景，文字深色清晰
    static QString settingLineEdit() {
        return QString(
            "QLineEdit {"
            "  border: none; border-radius: 8px;"
            "  padding: 0 16px; font-size: 14px; color: %1;"  // [V2.03l] 16→14小米紧凑
            "  background: white; min-height: 40px;"  // [V2.03l] 48→40紧凑
            "}"
        ).arg(textColor());
    }

    // [2026-06-24v5] 去掉内部边框+纯白背景
    static QString settingComboBox() {
        return QString(
            "QComboBox {"
            "  border: none; border-radius: 8px;"
            "  padding: 0 16px; font-size: 16px; color: %1;"
            "  background: white; min-height: 48px;"
            "}"
            "QComboBox::drop-down { border: none; width: 36px; }"
            "QComboBox QAbstractItemView { font-size: 16px; padding: 8px; }"
        ).arg(textColor());
    }

    // [2026-06-24v5] 去掉内部边框+纯白背景
    // [2026-06-24v8] 修复：数字区域下方仍有边框，彻底去掉所有边框和outline
    static QString settingSpinBox() {
        return QString(
            "QSpinBox {"
            "  border: none; border-radius: 8px;"
            "  padding: 0 16px; font-size: 16px; color: %1;"
            "  background: white; min-height: 48px; min-width: 90px;"
            "  outline: none;"
            "}"
            "QSpinBox:focus { border: none; outline: none; }"
            "QSpinBox::up-button, QSpinBox::down-button {"
            "  width: 32px; border: none; background: transparent;"
            "}"
            "QSpinBox::up-button:hover, QSpinBox::down-button:hover {"
            "  background: #f0f0f0; border-radius: 6px;"
            "}"
            "QSpinBox::up-arrow, QSpinBox::down-arrow {"
            "  width: 10px; height: 10px;"
            "}"
        ).arg(textColor());
    }

    /// 设置页面板保存按钮（44px高，14px字体，蓝色实底对齐其他页面主操作）
    static QString settingSaveBtn() {
        return QString(
            "QPushButton {"
            "  background: #4da3ff;"
            "  color: white; border: none; border-radius: 10px;"
            "  padding: 10px 24px; font-size: 14px; font-weight: 700;"
            "  min-height: 44px;"
            "}"
            "QPushButton:hover { background: #3d8ae0; }"
            "QPushButton:pressed { background: #2e7bd6; }"
        );
    }

    /// 数值输入框样式
    static QString spinBox() {
        return QString(
            "QSpinBox {"
            "  border: 2px solid #e0e0e0; border-radius: 12px;"
            "  padding: 10px 16px; font-size: 18px; color: %1;"
            "  background: white; min-height: 56px;"
            "}"
            "QSpinBox:focus { border-color: %2; }"
            "QSpinBox::up-button, QSpinBox::down-button { width: 36px; border: none; }"
        ).arg(textColor(), primaryColor());
    }

    /// 下拉框样式
    static QString comboBox() {
        return QString(
            "QComboBox {"
            "  border: 1px solid #d0d0d0; border-radius: 10px;"
            "  padding: 0 14px; font-size: 16px; color: %1;"
            "  background: white; min-height: 48px;"
            "}"
            "QComboBox:focus { border-color: %2; }"
            "QComboBox::drop-down { border: none; width: 36px; }"
            "QComboBox QAbstractItemView { font-size: 16px; padding: 8px; }"
        ).arg(textColor(), primaryColor());
    }

    // ============ 表格内嵌控件样式（紧凑版，适配56px行高）============
    // [V7.1 2026-06-24] 表格内的ComboBox/SpinBox/Button需要紧凑样式
    static QString tableComboBox() {
        return QString(
            "QComboBox {"
            "  border: 1px solid #d9d9d9; border-radius: 8px;"
            "  padding: 4px 12px; font-size: 14px; color: %1;"
            "  background: white; min-height: 32px;"
            "}"
            "QComboBox:hover { border-color: %2; }"
            "QComboBox::drop-down { border: none; width: 24px; }"
            "QComboBox QAbstractItemView { font-size: 14px; padding: 6px; }"
        ).arg(textColor(), primaryColor());
    }

    static QString tableSpinBox() {
        return QString(
            "QSpinBox {"
            "  border: 1px solid #d9d9d9; border-radius: 8px;"
            "  padding: 4px 10px; font-size: 14px; color: %1;"
            "  background: white; min-height: 32px; min-width: 70px;"
            "}"
            "QSpinBox:hover { border-color: %2; }"
            "QSpinBox::up-button, QSpinBox::down-button { width: 22px; border: none; }"
        ).arg(textColor(), primaryColor());
    }

    static QString tableCheckBox() {
        return QString(
            "QCheckBox { spacing: 6px; background: transparent; }"
            "QCheckBox::indicator { width: 22px; height: 22px; border-radius: 4px;"
            "  border: 2px solid #d0d0d0; background: white; }"
            "QCheckBox::indicator:hover { border-color: %1; }"
            "QCheckBox::indicator:checked { background: %1; border-color: %1; }"
        ).arg(primaryColor());
    }

    static QString tableActionBtn() {
        return QString(
            "QPushButton { background: %1; color: white; border: none;"
            "  border-radius: 8px; padding: 6px 18px; font-size: 14px;"
            "  font-weight: 600; min-height: 32px; }"
            "QPushButton:hover { background: %2; }"
            "QPushButton:pressed { background: %3; }"
            "QPushButton:disabled { background: #cccccc; color: #999999; }"
        ).arg(primaryColor(), "#3d8ce0", "#2e7ad6");
    }

    // ============ 全局样式表 ============
    static QString globalStyle() {
        QString fontFamily = detectChineseFont();
        return QString(
            "* { font-family: '%1', 'Microsoft YaHei', 'Source Han Sans SC', sans-serif; }"
            "QWidget { background: %2; color: %3; }"
        ).arg(fontFamily, bgColor(), textColor());
    }
    
    // ============ 弹窗样式 ============
    /// 统一弹窗样式（标题栏、背景、边框）
    static QString dialogStyle() {
        return QString(
            "QDialog {"
            "  background: %1;"  // 白色背景
            "  border-radius: 16px;"  // 圆角
            "  border: 1px solid %2;"  // 边框
            "}"
            "QDialog::title {"
            "  font-size: 18px;"  // 标题字体大小
            "  font-weight: bold;"  // 标题字体粗细
            "  color: %3;"  // 标题颜色
            "  padding: 16px 24px;"  // 标题内边距
            "}"
        ).arg(whiteColor(), borderColor(), textColor());
    }
    
    // ============ 麒麟系统字体适配 ============
    /// 检测系统中文字体，返回最佳可用字体
    static QString detectChineseFont() {
        // 麒麟系统中文字体优先级
        QStringList fontCandidates = {
            "WenQuanYi Micro Hei",    // 文泉驿微米黑 (麒麟常用)
            "Noto Sans CJK SC",       // Noto Sans CJK (Google开源)
            "Droid Sans Fallback",     // Droid Sans
            "AR PL UMing CN",          // AR PL UMing
            "AR PL UKai CN",          // AR PL UKai
            "SimHei",                 // 黑体
            "Microsoft YaHei"         // 微软雅黑 (备用)
        };
        
        // 检测系统可用字体
        QFontDatabase fontDb;
        QStringList availableFonts = fontDb.families();
        
        for (const QString &font : fontCandidates) {
            for (const QString &available : availableFonts) {
                if (available.contains(font, Qt::CaseInsensitive)) {
                    qInfo() << "[StyleHelper] Detected Chinese font:" << available;
                    return available;
                }
            }
        }
        
        // 如果没有找到任何中文字体，返回默认sans-serif
        qWarning() << "[StyleHelper] No Chinese font detected, using default";
        return "sans-serif";
    }
    
    /// 获取适配的字体对象
    static QFont getChineseFont(int pointSize = 16) {
        QString fontName = detectChineseFont();
        QFont font(fontName, pointSize);
        
        // 设置字体属性
        font.setStyleStrategy(QFont::PreferAntialias);
        
        // 如果是文泉驿微米黑，稍微增大字号（该字体偏小）
        if (fontName.contains("WenQuanYi", Qt::CaseInsensitive)) {
            font.setPointSize(pointSize + 1);
        }
        
        return font;
    }

    // ============ 位置格式统一 ============
    /// 格式化位置显示为 柜号-层号-位号（两位补零，如A-01-03）
    /// @param cabinetName 柜体全名（如"A柜"），取首字母作为柜号
    /// @param layer 层号字符串（如"03层"或"3"），提取数字后两位补零
    /// @param position 位号字符串（如"05位"或"5"），提取数字后两位补零
    /// @return 格式化后的位置字符串，如"A-01-03"
    static QString formatPosition(const QString& cabinetName, const QString& layer, const QString& position) {
        QString cabCode = cabinetName.isEmpty() ? "-" : cabinetName.left(1);
        QString layerCode = layer;
        layerCode.remove(QRegularExpression("[^0-9]"));
        if (!layerCode.isEmpty() && layerCode.toInt() < 10)
            layerCode = QStringLiteral("0") + layerCode;
        QString posCode = position;
        posCode.remove(QRegularExpression("[^0-9]"));
        if (!posCode.isEmpty() && posCode.toInt() < 10)
            posCode = QStringLiteral("0") + posCode;
        return QStringLiteral("%1-%2-%3").arg(cabCode, layerCode, posCode);
    }
};
