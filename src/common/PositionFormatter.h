/**
 * @file PositionFormatter.h
 * @brief 位置格式化工具 — 纯字符串处理，无UI依赖，可供DB/Service/Page层共享
 * @author 袁燕
 *
 * 位置格式统一：柜号-层号-位号（两位补零，如A-01-03）
 * 用于将cabinet_name/layer/position原始字段格式化为统一精简显示格式
 */
#pragma once

#include <QString>
#include <QRegularExpression>

namespace common {

/// 格式化位置为 A-01-03 格式
/// @param cabinetName 柜体名称（取首字母作为柜号，如"A柜"→"A"）
/// @param layer 层号字符串（提取数字后两位补零，如"3"→"03"，"03层"→"03"）
/// @param position 位号字符串（提取数字后两位补零，如"5"→"05"，"05位"→"05"）
/// @return 格式化后的位置字符串，如"A-01-03"
inline QString formatPosition(const QString& cabinetName,
                              const QString& layer,
                              const QString& position)
{
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

} // namespace common
