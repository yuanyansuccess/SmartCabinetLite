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
    // ============ 触屏设计令牌（消除魔鬼数字的唯一来源）============
    // 规约：控件高度只用本表枚举值；新代码禁止裸写数字
    //   表格内紧凑控件另用 Compact* 系列，不得占用触屏高度
    struct Token {
        // ---- 字号（px）----
        static constexpr int FontCaption  = 12;  // 角标/极次要说明
        static constexpr int FontSmall    = 13;  // 版权/次要信息
        static constexpr int FontBody     = 14;  // 表格正文/表单标签
        static constexpr int FontLabel    = 15;  // 表单标签/列表项
        static constexpr int FontInput    = 16;  // 输入框与按钮文字
        static constexpr int FontTitle    = 18;  // 卡片标题
        static constexpr int FontHeading  = 20;  // 页面标题
        static constexpr int FontDisplay  = 22;  // 大标题/关键数值

        // ---- 控件高度（px）----
        static constexpr int ControlHeight       = 44;  // 常规按钮与输入框
        static constexpr int ControlHeightTouch  = 48;  // 触屏标准高度（主要交互控件）
        static constexpr int ControlHeightLarge  = 56;  // 大按钮/强调操作
        static constexpr int ControlHeightCompact = 32;  // 表格内紧凑控件
        static constexpr int ControlHeightCompactInput = 40;  // 设置页紧凑输入框
        static constexpr int RowHeight           = 44;  // 表格行高

        // ---- 圆角（px）----
        static constexpr int RadiusSmall  = 8;
        static constexpr int Radius       = 10;
        static constexpr int RadiusMedium = 12;
        static constexpr int RadiusLarge  = 14;

        // ---- 状态指示点直径（px）----
        static constexpr int StatusDotSize = 8;
    };

    // ============ 触屏尺寸片段生成（消除 QSS 内魔数）============
    /// 生成 font-size 片段（字号统一走 Token，新代码禁止裸写数字）
    /// @param level 取 StyleHelper::Token::Font* 系列值
    static QString fontSize(int level) {
        return QString("font-size:%1px;").arg(level);
    }

    /// 生成 min-height 片段（控件高度统一走 Token）
    static QString minHeight(int level) {
        return QString("min-height:%1px;").arg(level);
    }

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

    /// 默认按钮 (白底灰边，用于返回/取消) 统一触屏标准: 44px高 14px字体 10px圆角
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
    /// 主按钮 (蓝色实底，用于保存/确认等主要操作) 统一触屏标准: 44px高 14px字体 10px圆角
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

    /// 流程确认清单的清单表格样式（借用/归还/出库"确认清单"对话框共用，唯一定义点）
    static QString listTableStyle() {
        return QString(
            "QTableWidget { border:1px solid #f0f0f0; background:#fff; border-radius:10px; "
            "  font-family:\"Microsoft YaHei\",sans-serif; font-size:14px; }"
            "QTableWidget::item { padding:10px 12px; color:#333; border:none; "
            "  border-bottom:1px solid #f3f3f3; }"
            "QHeaderView::section { background:#f8f9fb; color:#666; font-weight:600; "
            "  font-size:13px; padding:10px 12px; border:none; border-bottom:1px solid #f0f0f0; }"
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

    /// 轮廓按钮 (白底蓝边，用于辅助操作) 统一触屏标准
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

    /// 正文/标签文本色（表格单元格、表单标签等 #333 系列）
    static QString textBody()      { return "#333333"; }
    /// 方位达标/通过徽标绿
    static QString acceptColor()   { return "#52c41a"; }
    /// 次要提示文本色（#999 系列）
    static QString textMuted()     { return "#999999"; }
    /// 字段值文本色（#555 系列）
    static QString textField()     { return "#555555"; }
    /// 禁用/极弱化文本色（#bbb 系列）
    static QString textDisabled()  { return "#bbbbbb"; }
    /// 强调橙（方位未达标提示等，区别于 warningColor）
    static QString orangeAccent()  { return "#fa8c16"; }
    /// 浅灰卡片底色
    static QString cardLightBg()   { return "#f8f9fb"; }
    /// 分割线色（比 borderColor 更浅，用于保存栏顶部分割线）
    static QString dividerColor()  { return "#f0f0f0"; }

    /// 通用文本样式（透明底）：收口页面内各类说明/字段值/提示文本
    /// @param sizePx 字号，取 Token::Font* 系列值
    /// @param color  文本色，取 textColor()/textBody()/textMuted()/textField()/primaryColor() 等
    /// @param weight 字重（400=常规，600/700=加粗）
    /// @param extra  追加的 QSS 片段（如 "padding:4px 0;"），无则留空
    static QString textStyle(int sizePx, const QString& color, int weight = 400,
                             const QString& extra = QString()) {
        return QString("font-size:%1px;font-weight:%2;color:%3;%4background:transparent;")
            .arg(sizePx).arg(weight).arg(color).arg(extra);
    }

    // ============ 语义化复合样式（收口全项目高频重复样式串）============
    // 以下样式原在各页面重复出现 3~10 次，统一收口到此处：
    // 改样式只需改一处，避免逐页散改导致风格不一致。

    /// 表格/表单标签文本：15px 600 #333 透明底（原散落 10 处）
    static QString labelText() {
        return QString("font-size:%1px;font-weight:600;color:%2;background:transparent;")
            .arg(Token::FontLabel).arg(textBody());
    }

    /// 弹窗大标题文本：22px 粗体 主文本色 透明底（原散落 5 处）
    static QString dialogTitleText() {
        return QString("font-size:%1px;font-weight:bold;color:%2;background:transparent;")
            .arg(Token::FontDisplay).arg(textColor());
    }

    /// 人脸方位提示徽标：32px 900 白字 绿底 14px圆角（原散落 6 处）
    static QString postureBadge() {
        return QString("font-size:32px;font-weight:900;color:%1;background:%2;"
                       "border-radius:%3px;padding:10px 20px;")
            .arg(whiteColor(), acceptColor()).arg(Token::RadiusLarge);
    }

    /// 状态指示点：主色实心圆点（直径用 Token::StatusDotSize，原散落 4 处）
    static QString statusDot() {
        return QString("background:%1;border-radius:%2px;min-width:%3px;min-height:%3px;")
            .arg(primaryColor())
            .arg(Token::StatusDotSize / 2)
            .arg(Token::StatusDotSize);
    }

    /// 按钮组-主按钮（紧凑版 40px高，用于设置项多选一，原散落 5 处）
    static QString buttonGroupPrimary() {
        return QString(
            "QPushButton { background: %1; color: white; border: none; border-radius: %2px;"
            "  padding: 8px 16px; font-size: %3px; font-weight: 600; min-height: %4px; }"
            "QPushButton:hover { background: %5; }"
            "QPushButton:pressed { background: %6; }"
        ).arg(primaryColor()).arg(Token::Radius)
         .arg(Token::FontBody).arg(Token::ControlHeightCompactInput)
         .arg(primaryHover(), "#2e7bd6");
    }

    /// 按钮组-次按钮（紧凑版 白底灰边，hover 转主色，原散落 5 处）
    static QString buttonGroupDefault() {
        return QString(
            "QPushButton { background: white; color: %1; border: 1px solid %2;"
            "  border-radius: %3px; padding: 8px 16px; font-size: %4px;"
            "  font-weight: 600; min-height: %5px; }"
            "QPushButton:hover { border-color: %6; color: %6; background: #f0f7ff; }"
            "QPushButton:pressed { background: #e6f0ff; }"
        ).arg(textColor(), borderColor()).arg(Token::Radius)
         .arg(Token::FontBody).arg(Token::ControlHeightCompactInput)
         .arg(primaryColor());
    }

    /// 面板标题条（Tab头）：上两角圆角、灰底、44px高，原散落 4 处
    static QString panelTitleBar() {
        return QString("font-size:%1px;font-weight:600;padding:10px 24px;"
                       "border-radius:%2px %2px 0 0;color:%3;background:%4;min-height:%5px;")
            .arg(Token::FontLabel).arg(Token::Radius)
            .arg(textSecondary(), bgColor()).arg(Token::ControlHeight);
    }

    /// 面板内小标题：14px 700 主文本色，原散落 4 处
    static QString sectionTitle() {
        return QString("font-size:%1px;font-weight:700;color:%2;background:transparent;")
            .arg(Token::FontBody).arg(textColor());
    }

    /// 标题下分隔细线（1px 水平线），原散落 4 处
    static QString separatorLine() {
        return QString("QFrame{background:%1;max-height:1px;margin-bottom:6px;}")
            .arg(borderColor());
    }

    /// 面板底部保存栏的顶部分割线，原散落 4 处
    static QString saveBarSeparator() {
        return QString("QFrame{border-top:1px solid %1;background:transparent;}")
            .arg(dividerColor());
    }

    /// 白底卡片容器（统计卡/内容卡），原散落 3 处
    static QString cardWhite() {
        return QString("background:white;border-radius:%1px;border:none;")
            .arg(Token::RadiusMedium);
    }

    /// 浅灰底卡片容器，原散落 3 处
    static QString cardLight() {
        return QString("QFrame{background:%1;border-radius:%2px;}")
            .arg(cardLightBg()).arg(Token::RadiusMedium);
    }

    /// 弹窗统一背景，原散落 3 处
    static QString dialogBackground() {
        return QString("QDialog { background: %1; }").arg(bgColor());
    }

    /// 弹窗大标题（30px 700），原散落 3 处
    static QString largeTitleText() {
        return QString("font-size:30px;font-weight:700;color:%1;background:transparent;")
            .arg(textColor());
    }

    /// 多选筛选弹窗：对话框边框 + 复选框项 + 按钮统一尺寸，原散落 3 处
    static QString popupCheckList() {
        return QString(
            "QDialog{background:white;border:2px solid %1;border-radius:%2px;}"
            "QCheckBox{font-size:%3px;padding:10px 20px;spacing:10px;}"
            "QCheckBox::indicator{width:20px;height:20px;}"
            "QPushButton{min-height:%4px;font-size:%3px;border-radius:%5px;}"
        ).arg(borderColor()).arg(Token::Radius)
         .arg(Token::FontLabel).arg(Token::ControlHeightCompactInput)
         .arg(Token::RadiusSmall);
    }

    // ============ 输入框样式 ============
    /// 统一触屏标准: 48px高 16px字体 10px圆角 1px边框
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
    // 48px, 16px，与其他管理页面风格一致
    // 去掉内部边框+纯白背景，文字深色清晰
    static QString settingLineEdit() {
        return QString(
            "QLineEdit {"
            "  border: none; border-radius: %1px;"
            "  padding: 0 16px; font-size: %2px; color: %3;"
            "  background: white; min-height: %4px;"
            "}"
        ).arg(QString::number(Token::RadiusSmall),
              QString::number(Token::FontBody),
              textColor(),
              QString::number(Token::ControlHeightCompactInput));
    }

    // 去掉内部边框+纯白背景
    static QString settingComboBox() {
        return QString(
            "QComboBox {"
            "  border: none; border-radius: %1px;"
            "  padding: 0 16px; font-size: %2px; color: %3;"
            "  background: white; min-height: %4px;"
            "}"
            "QComboBox::drop-down { border: none; width: 36px; }"
            "QComboBox QAbstractItemView { font-size: %2px; padding: 8px; }"
        ).arg(QString::number(Token::RadiusSmall),
              QString::number(Token::FontInput),
              textColor(),
              QString::number(Token::ControlHeightTouch));
    }

    // 去掉内部边框+纯白背景
    // 注意：数字区域下方仍有边框，彻底去掉所有边框和outline
    static QString settingSpinBox() {
        return QString(
            "QSpinBox {"
            "  border: none; border-radius: %1px;"
            "  padding: 0 16px; font-size: %2px; color: %3;"
            "  background: white; min-height: %4px; min-width: 90px;"
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
        ).arg(QString::number(Token::RadiusSmall),
              QString::number(Token::FontInput),
              textColor(),
              QString::number(Token::ControlHeightTouch));
    }

    /// 设置页面板保存按钮（44px高，14px字体，蓝色实底对齐其他页面主操作）
    static QString settingSaveBtn() {
        return QString(
            "QPushButton {"
            "  background: %1;"
            "  color: white; border: none; border-radius: %2px;"
            "  padding: 10px 24px; font-size: %3px; font-weight: 700;"
            "  min-height: %4px;"
            "}"
            "QPushButton:hover { background: %5; }"
            "QPushButton:pressed { background: %6; }"
        ).arg(primaryColor(), QString::number(Token::Radius),
              QString::number(Token::FontBody), QString::number(Token::ControlHeight),
              primaryHover(), QStringLiteral("#2e7bd6"));
    }

    /// 数值输入框样式
    static QString spinBox() {
        return QString(
            "QSpinBox {"
            "  border: 2px solid #e0e0e0; border-radius: %1px;"
            "  padding: 10px 16px; font-size: %2px; color: %3;"
            "  background: white; min-height: %4px;"
            "}"
            "QSpinBox:focus { border-color: %5; }"
            "QSpinBox::up-button, QSpinBox::down-button { width: 36px; border: none; }"
        ).arg(QString::number(Token::RadiusMedium),
              QString::number(Token::FontTitle),
              textColor(),
              QString::number(Token::ControlHeightLarge),
              primaryColor());
    }

    /// 下拉框样式
    static QString comboBox() {
        return QString(
            "QComboBox {"
            "  border: 1px solid #d0d0d0; border-radius: %1px;"
            "  padding: 0 14px; font-size: %2px; color: %3;"
            "  background: white; min-height: %4px;"
            "}"
            "QComboBox:focus { border-color: %5; }"
            "QComboBox::drop-down { border: none; width: 36px; }"
            "QComboBox QAbstractItemView { font-size: %2px; padding: 8px; }"
        ).arg(QString::number(Token::Radius),
              QString::number(Token::FontInput),
              textColor(),
              QString::number(Token::ControlHeightTouch),
              primaryColor());
    }

    // ============ 表格内嵌控件样式（紧凑版，适配56px行高）============
    // 表格内的ComboBox/SpinBox/Button需要紧凑样式
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
            "  border-radius: 16px;"  // 圆角（弹窗专用尺寸，Token 表无对应值）
            "  border: 1px solid %2;"  // 边框
            "}"
            "QDialog::title {"
            "  font-size: %4px;"  // 标题字体大小
            "  font-weight: bold;"  // 标题字体粗细
            "  color: %3;"  // 标题颜色
            "  padding: 16px 24px;"  // 标题内边距
            "}"
        ).arg(whiteColor(), borderColor(), textColor(), QString::number(Token::FontTitle));
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
    /// 提取层/位号中的数字并规范为两位（不足两位补零，超过两位原样保留）
    /// 注意：按字符串长度判断补零，不能按数值大小（"03"数值3<10会被误补成"003"）
    static QString twoDigitCode(const QString& raw) {
        QString code = raw;
        code.remove(QRegularExpression("[^0-9]"));
        if (!code.isEmpty() && code.size() < 2)
            code = QStringLiteral("0") + code;
        return code;
    }

    /// 格式化位置显示为 柜号-层号-位号（两位补零，如A-01-03）
    /// @param cabinetName 柜体全名（如"A柜"），取首字母作为柜号
    /// @param layer 层号字符串（如"03层"或"3"），提取数字后两位补零
    /// @param position 位号字符串（如"05位"或"5"），提取数字后两位补零
    /// @return 格式化后的位置字符串，如"A-01-03"
    static QString formatPosition(const QString& cabinetName, const QString& layer, const QString& position) {
        QString cabCode = cabinetName.isEmpty() ? "-" : cabinetName.left(1);
        return QStringLiteral("%1-%2-%3").arg(cabCode, twoDigitCode(layer), twoDigitCode(position));
    }
};
