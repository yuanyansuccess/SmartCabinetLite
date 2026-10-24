/**
 * @file DeviceProfile.cpp
 * @brief 设备识别与全局 UI 等比缩放实现
 * @author 袁燕
 */
#include "DeviceProfile.h"
#include <QSettings>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QRegularExpression>
#include <QDebug>
#include <QtMath>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif
#ifdef Q_OS_LINUX
#  include <QProcess>
#endif

qreal DeviceProfile::s_scale = 1.0;
bool  DeviceProfile::s_smallScreen = false;

/**
 * @brief 算出全局界面缩放比例并写入环境变量，必须在界面对象创建之前调用
 * @param argc 命令行参数个数，用于推算程序所在目录
 * @param argv 命令行参数数组，用第 0 个参数定位同目录下的配置文件
 * @note 手工配置优先于自动计算；算出的比例会被限制在 0.5~3.0 之间
 */
void DeviceProfile::applyScale(int argc, char** argv) {
    const qreal cfg = readConfigUiScale(argc, argv);
    const QSize screen = detectScreenLogicalSize();

    qreal scale;
    if (cfg > 0.0) {
        scale = cfg;                                  // 手动覆盖优先
    } else if (screen.width() > 0 && screen.height() > 0) {
        scale = qMin(qreal(screen.width())  / kDesignWidth,
                     qreal(screen.height()) / kDesignHeight);  // 等比完整显示
    } else {
        scale = 1.0;
    }
    scale = qBound(qreal(0.5), scale, qreal(3.0));     // 钳制防极端值
    s_scale = scale;

    // 必须在 QApplication 构造之前设置，Qt 仅在启动时读取一次
    qputenv("QT_SCALE_FACTOR", QByteArray::number(scale, 'f', 4));

    qInfo() << "[DeviceProfile] 设计基准" << kDesignWidth << "x" << kDesignHeight
            << "| 主屏逻辑" << screen
            << "| 配置覆盖" << (cfg > 0 ? QString::number(cfg) : "无")
            << "| => QT_SCALE_FACTOR" << scale;
}

/**
 * @brief 探测主屏的逻辑分辨率，口径与 Qt 的逻辑坐标保持一致
 * @return 主屏逻辑尺寸（宽x高）；探测不到时返回设计基准尺寸作为保底
 */
QSize DeviceProfile::detectScreenLogicalSize() {
#ifdef Q_OS_WIN
    // per-monitor DPI aware 下 GetSystemMetrics 返回主屏【物理】像素，
    // 需按系统 DPI 折算回逻辑像素（与 Qt 逻辑分辨率口径一致）
    const int px = GetSystemMetrics(SM_CXSCREEN);
    const int py = GetSystemMetrics(SM_CYSCREEN);
    const HDC hdc = GetDC(nullptr);
    int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
    int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(nullptr, hdc);
    if (dpiX <= 0) dpiX = 96;
    if (dpiY <= 0) dpiY = 96;
    return QSize(qRound(px * 96.0 / dpiX), qRound(py * 96.0 / dpiY));
#else
    // Linux / 麒麟：xrandr --current 首行 "current W x H" 即总逻辑分辨率
    QProcess p;
    p.start("xrandr", QStringList() << "--current");
    if (p.waitForFinished(1000)) {
        const QString out = QString::fromLocal8Bit(p.readAllStandardOutput());
        const QRegularExpression re("current\\s+(\\d+)\\s*x\\s*(\\d+)");
        const QRegularExpressionMatch m = re.match(out);
        if (m.hasMatch())
            return QSize(m.captured(1).toInt(), m.captured(2).toInt());
    }
    qWarning() << "[DeviceProfile] xrandr 不可用，回退到基准 1024x768";
    return QSize(kDesignWidth, kDesignHeight);
#endif
}

/**
 * @brief 读取配置文件里手工指定的界面缩放比例
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组，用于找到程序同级目录下的 system.ini
 * @return 配置的有效比例；没配或配了非法值时返回 0，表示交给自动计算
 */
qreal DeviceProfile::readConfigUiScale(int argc, char** argv) {
    // 构造前轻量读取 system.ini [UI] uiScale，不依赖 QApplication
    QString exeDir;
    if (argc > 0 && argv[0] && *argv[0])
        exeDir = QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath();
    else
        exeDir = QDir::currentPath();
    QSettings ini(exeDir + "/system.ini", QSettings::IniFormat);
    ini.setFallbacksEnabled(false);
    bool ok = false;
    const qreal uiScale = ini.value("UI/uiScale", 0).toDouble(&ok);
    return (ok && uiScale > 0) ? uiScale : 0.0;
}

/**
 * @brief 采集屏幕物理尺寸与缩放信息写入日志，并判定本机是否属于小屏设备
 * @note 判定结果影响界面布局取舍：对角线小于 11 英寸按小屏处理
 */
void DeviceProfile::logDeviceInfo() {
    if (QGuiApplication::screens().isEmpty())
        return;
    QScreen* s = QGuiApplication::primaryScreen();
    if (!s)
        return;
    const QSizeF phys = s->physicalSize();                       // 毫米
    const qreal diagInch = qSqrt(phys.width() * phys.width()
                                + phys.height() * phys.height()) / 25.4;
    const QSize logical = s->availableGeometry().size();
    s_smallScreen = (diagInch > 0 && diagInch < 11.0);
    qInfo() << "[DeviceProfile] 设备识别 => 物理对角线"
            << QString::number(diagInch, 'f', 1) << "英寸"
            << "| 逻辑分辨率" << logical
            << "| DPR" << s->devicePixelRatio()
            << "| 实际总缩放" << QString::number(s->devicePixelRatio() * s_scale, 'f', 3)
            << "=>" << (s_smallScreen ? "小屏(<=11寸)" : "常规屏");
}
