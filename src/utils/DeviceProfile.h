/**
 * @file DeviceProfile.h
 * @brief 设备识别与全局 UI 等比缩放（针对 8 寸 1024x768 工控屏）
 * @author 袁燕
 *
 * 设计基准分辨率固定为 1024x768；运行时按主屏逻辑分辨率做 min(宽比,高比)
 * 等比缩放，内容完整可见、不变形。支持 system.ini [UI] uiScale 手动覆盖。
 */
#pragma once

#include <QSize>
#include <qglobal.h>

class DeviceProfile {
public:
    // 设计基准分辨率（8 寸 1024x768 工控屏的最优逻辑分辨率）
    static constexpr int kDesignWidth  = 1024;
    static constexpr int kDesignHeight = 768;

    // 必须在 QApplication 构造【之前】调用：
    // 读 system.ini [UI] uiScale 覆盖 -> 探测主屏逻辑分辨率 -> 设 QT_SCALE_FACTOR
    static void applyScale(int argc, char** argv);

    // 必须在 QApplication 构造【之后】调用：打印设备识别信息
    static void logDeviceInfo();

    static qreal currentScale()   { return s_scale; }
    static bool  isSmallScreen()  { return s_smallScreen; }

private:
    static QSize detectScreenLogicalSize();   // 平台 API，构造前可用
    static qreal readConfigUiScale(int argc, char** argv);  // 轻量读 ini
    static qreal s_scale;
    static bool  s_smallScreen;
};
