/**
 * @file FormFactory.h
 * @brief 表单控件工厂 - 统一各页面重复的标签/字段值/开关创建逻辑
 * @author 袁燕
 *
 * 背景：设置页/详情页等 4+ 处面板各自复制 makeLabel/makeToggle/makeFieldLabel
 *       等 lambda，样式参数完全相同却分散维护，改一处要改多处。
 * 职责：只做"创建纯 UI 控件"，不含任何业务回调（回调由调用方 connect）。
 * 规约：控件尺寸/字号一律取 StyleHelper::Token 令牌，禁止裸写数字。
 */
#pragma once

#include <QCheckBox>
#include <QLabel>
#include <QString>

#include "utils/StyleHelper.h"

class FormFactory {
public:
    /// 表单标签（设置页表单列）：14px/600/主题色/最小高36/固定宽100
    /// @param text 标签文字
    /// @param minHeight 标签最小高度，默认令牌 ControlHeightCompactInput 体系
    /// @param fixedWidth 标签固定宽度（限制标签列不过宽），0=不固定
    static QLabel* formLabel(const QString& text, int minHeight = 36, int fixedWidth = 100) {
        auto* label = new QLabel(text);
        label->setStyleSheet(QString("font-size:%1px;font-weight:600;color:%2;background:transparent;")
            .arg(StyleHelper::Token::FontBody)
            .arg(StyleHelper::textColor()));
        if (minHeight > 0) label->setMinimumHeight(minHeight);
        if (fixedWidth > 0) label->setFixedWidth(fixedWidth);
        return label;
    }

    /// 详情页字段名标签：14px/常规/次要色
    static QLabel* fieldLabel(const QString& text) {
        auto* label = new QLabel(text);
        label->setStyleSheet(QString("font-size:%1px;color:#999;background:transparent;")
            .arg(StyleHelper::Token::FontBody));
        return label;
    }

    /// 详情页字段值标签：15px/加粗/深灰/长文本换行
    static QLabel* fieldValue(const QString& text) {
        auto* label = new QLabel(text);
        label->setStyleSheet(QString("font-size:%1px;color:#333;font-weight:600;background:transparent;")
            .arg(StyleHelper::Token::FontLabel));
        label->setWordWrap(true);
        return label;
    }

    /// 触屏开关：15px文字/22px指示器/选中态主色
    static QCheckBox* toggle(bool checked = true) {
        auto* box = new QCheckBox();
        box->setChecked(checked);
        box->setStyleSheet(
            QString("QCheckBox{font-size:%1px;background:transparent;spacing:6px;}"
                    "QCheckBox::indicator{width:22px;height:22px;border-radius:4px;"
                    "border:2px solid #d0d0d0;background:white;}"
                    "QCheckBox::indicator:hover{border-color:%2;}"
                    "QCheckBox::indicator:checked{background:%2;border-color:%2;}")
            .arg(StyleHelper::Token::FontLabel)
            .arg(StyleHelper::primaryColor()));
        return box;
    }
};
