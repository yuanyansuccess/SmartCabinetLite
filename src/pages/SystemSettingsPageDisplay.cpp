/**
 * @file SystemSettingsPageDisplay.cpp
 * @brief 系统设置-显示器亮度调节（Windows WMI / 麒麟三级降级）
 * @author 袁燕
 *
 * 本文件实现上述功能，成员函数声明见 SystemSettingsPage.h。
 */

#include "SystemSettingsPage.h"
#include "components/SoftKeyboard.h"
#include "components/NumKeypad.h"
#include "components/BaseDialog.h"  // 统一圆角对话框
#include "utils/StyleHelper.h"
#include "components/FormFactory.h"  // 表单控件工厂（收敛重复lambda）
#include "services/SettingService.h"
#include "services/AuthService.h"
#include "common/AppConfig.h"  // 机组名称配置
#include "common/Logger.h"     // 统一日志写入入口（Log::appendLog）
#include "components/MessageDialog.h"
#include <QDebug>
#include <QProcess>  // 调用PowerShell设置显示器亮度

/**
 * @brief 应用显示亮度
 * @param percent 目标亮度，范围 0~100
 */
void SystemSettingsPage::applyDisplayBrightness(int percent) {
    // 输入校验：亮度值范围 0-100
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
#ifdef Q_OS_WIN
    applyBrightnessWindows(percent);
#else
    applyBrightnessLinux(percent);
#endif
}

/**
 * @brief 亮度调整日志（Windows 写程序数据目录，麒麟写 /tmp）
 * @param percent 目标亮度
 * @param method 调用方式（如 WMI / xrandr）
 * @param result 命令输出内容
 * @param exitCode 进程退出码
 */
void SystemSettingsPage::writeBrightnessLog(int percent, const QString& method, const QString& result, int exitCode) {
    // 日志路径统一走 Logger 目录（程序数据目录/logs，跨平台且自动创建）
    QString logPath = Log::logDirectory() + "/brightness.log";
    // 统一走 Logger 入口：时间戳/建目录/UTF-8/异常处理由 Logger 负责
    Log::appendLog(logPath,
                   QStringLiteral("method=%1 brightness=%2% exitCode=%3 result=%4")
                       .arg(method).arg(percent).arg(exitCode).arg(result));
}

#ifdef Q_OS_WIN
/**
 * @brief Windows 亮度调整：PowerShell + WMI 异步执行不阻塞 UI
 * @param percent 目标亮度，范围 0~100
 */
void SystemSettingsPage::applyBrightnessWindows(int percent) {
    // PowerShell: (Get-WmiObject -Namespace root/WMI -Class WmiMonitorBrightnessMethods).WmiSetBrightness(1, <percent>)
    QString psScript = QStringLiteral(
        "try { "
        "  $monitors = Get-WmiObject -Namespace root/WMI -Class WmiMonitorBrightnessMethods -ErrorAction Stop; "
        "  if ($monitors) { $monitors.WmiSetBrightness(1, %1); Write-Output 'OK' } "
        "  else { Write-Output 'NOMONITOR' } "
        "} catch { Write-Output 'WMI_ERROR' }"
    ).arg(percent);

    auto* proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, percent](int exitCode, QProcess::ExitStatus) {
        auto* p = qobject_cast<QProcess*>(sender());
        if (!p) return;
        QString output = p->readAllStandardOutput().trimmed();
        p->deleteLater();

        writeBrightnessLog(percent, QStringLiteral("WMI"), output, exitCode);
        if (output == "OK") {
            qInfo() << "[Brightness] WMI applied successfully:" << percent << "%";
        } else {
            qWarning() << "[Brightness] WMI failed, output:" << output << "exitCode:" << exitCode;
        }
    });

    QStringList args;
    args << "-NoProfile" << "-NonInteractive" << "-Command" << psScript;
    proc->start("powershell.exe", args);
    if (!proc->waitForStarted(3000)) {
        qWarning() << "[Brightness] Failed to start powershell.exe";
        writeBrightnessLog(percent, QStringLiteral("WMI"), QStringLiteral("START_FAILED"), -1);
        proc->deleteLater();
    }
}

#else
/**
 * @brief 麒麟Linux亮度调整：多级降级链
 *        顺序：backlight内核接口(硬件级，尝试pkexec/sudo提权) → xrandr(软件伽马,X11) → light → brightnessctl
 *        任一方法成功即停止并写日志；全部失败弹窗提示，避免"屏幕无变化却无任何反馈"的误判
 * @param percent 目标亮度百分比，取值范围 0~100
 */
void SystemSettingsPage::applyBrightnessLinux(int percent) {
    auto writeLog = [this, percent](const QString& method, const QString& result, int exitCode) {
        writeBrightnessLog(percent, method, result, exitCode);
    };

    // 异步执行单个命令；finished 后写日志。isLast=true 时若仍失败则弹窗提示
    auto runAsync = [this, percent, writeLog](const QString& method,
                                               const QString& program,
                                               const QStringList& args,
                                               bool isLast) {
        auto* proc = new QProcess(this);
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, method, percent, writeLog, isLast](int exitCode, QProcess::ExitStatus) {
            auto* p = qobject_cast<QProcess*>(sender());
            if (!p) return;
            QString output = p->readAllStandardOutput().trimmed();
            QString errOutput = p->readAllStandardError().trimmed();
            p->deleteLater();

            QString result = (exitCode == 0) ? QStringLiteral("OK: %1").arg(output)
                                             : QStringLiteral("FAIL: %1").arg(errOutput.isEmpty() ? output : errOutput);
            writeLog(method, result, exitCode);

            if (exitCode == 0) {
                qInfo() << "[Brightness]" << method << "applied:" << percent << "%";
            } else {
                qWarning() << "[Brightness]" << method << "failed:" << result;
                if (isLast) {
                    MessageDialog::showWarning(this, QStringLiteral("亮度调整失败"),
                        QStringLiteral("系统不支持当前亮度调节方式，详细原因见 brightness.log"));
                }
            }
        });

        proc->start(program, args);
        if (!proc->waitForStarted(3000)) {
            qWarning() << "[Brightness] Failed to start" << program;
            writeLog(method, QStringLiteral("START_FAILED"), -1);
            proc->deleteLater();
            if (isLast) {
                MessageDialog::showWarning(this, QStringLiteral("亮度调整失败"),
                    QStringLiteral("系统不支持当前亮度调节方式，详细原因见 brightness.log"));
            }
        }
    };

    // xrandr 软件级亮度脚本：解析"已连接"的显示器名称（避免误取显示器数量），
    // 用awk计算亮度比例（不依赖bc），仅当至少一台显示器设置成功才返回XRANDR_OK标记
    auto buildXrandrScript = [percent]() -> QString {
        return QStringLiteral(
            "displays=$(xrandr 2>/dev/null | awk '/ connected/{print $1}'); "
            "if [ -z \"$displays\" ]; then echo 'NO_DISPLAY'; exit 1; fi; "
            "b=$(awk -v p=%1 'BEGIN{ v=p/100; printf \"%.2f\", v }'); "
            "ok=0; for d in $displays; do xrandr --output \"$d\" --brightness \"$b\" 2>/dev/null && ok=1; done; "
            "if [ \"$ok\" = \"1\" ]; then echo 'XRANDR_OK'; else echo 'XRANDR_FAIL'; exit 1; fi"
        ).arg(percent);
    };

    // 方案A：backlight 内核接口（硬件级亮度）
    // 直接写（root场景即可生效）；普通用户权限不足时依次尝试 pkexec/sudo 提权写入
    QString backlightScript = QStringLiteral(
        "for dev in /sys/class/backlight/*/; do "
        "  if [ -f \"${dev}max_brightness\" ]; then "
        "    max=$(cat \"${dev}max_brightness\"); "
        "    val=$(( %1 * max / 100 )); "
        "    if echo \"$val\" > \"${dev}brightness\" 2>/dev/null; then echo 'BACKLIGHT_OK'; exit 0; fi; "
        "    if command -v pkexec >/dev/null 2>&1 && echo \"$val\" | pkexec tee \"${dev}brightness\" >/dev/null 2>&1; then echo 'BACKLIGHT_OK'; exit 0; fi; "
        "    if command -v sudo >/dev/null 2>&1 && echo \"$val\" | sudo -n tee \"${dev}brightness\" >/dev/null 2>&1; then echo 'BACKLIGHT_OK'; exit 0; fi; "
        "  fi; "
        "done; "
        "echo 'NO_BACKLIGHT_DEV'; exit 1"
    ).arg(percent);

    // 方案C-2：brightnessctl（最后一级，失败则弹窗）
    auto tryBrightnessctl = [runAsync, percent]() {
        runAsync(QStringLiteral("brightnessctl"),
                 QStringLiteral("brightnessctl"),
                 QStringList() << "set" << QStringLiteral("%1%").arg(percent),
                 true);
    };

    auto* procA = new QProcess(this);
    connect(procA, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, percent, writeLog, runAsync, tryBrightnessctl, buildXrandrScript](int exitCodeA, QProcess::ExitStatus) {
        auto* pA = qobject_cast<QProcess*>(sender());
        if (!pA) return;
        QString outputA = pA->readAllStandardOutput().trimmed();
        pA->deleteLater();

        writeLog(QStringLiteral("backlight"), outputA, exitCodeA);

        if (exitCodeA == 0 && outputA.contains("BACKLIGHT_OK")) {
            qInfo() << "[Brightness] backlight kernel interface applied:" << percent << "%";
            return;  // 方案A成功，不降级
        }

        qWarning() << "[Brightness] backlight failed, trying xrandr...";

        // 方案B：xrandr X11 Gamma调整（软件级，percent映射到0.1~1.0）
        QString xrandrScript = buildXrandrScript();

        auto* procB = new QProcess(this);
        connect(procB, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, percent, writeLog, tryBrightnessctl](int exitCodeB, QProcess::ExitStatus) {
            auto* pB = qobject_cast<QProcess*>(sender());
            if (!pB) return;
            QString outputB = pB->readAllStandardOutput().trimmed();
            pB->deleteLater();

            writeLog(QStringLiteral("xrandr"), outputB, exitCodeB);

            if (exitCodeB == 0 && outputB.contains("XRANDR_OK")) {
                qInfo() << "[Brightness] xrandr applied:" << percent << "%";
                return;  // 方案B成功
            }

            qWarning() << "[Brightness] xrandr failed, trying light...";

            // 方案C-1：light 命令（backlight 专用，需root/sudo；失败再降级）
            auto* procLight = new QProcess(this);
            connect(procLight, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                    this, [this, percent, writeLog, tryBrightnessctl](int exitCodeL, QProcess::ExitStatus) {
                auto* pL = qobject_cast<QProcess*>(sender());
                if (!pL) return;
                QString outputL = pL->readAllStandardOutput().trimmed();
                pL->deleteLater();
                writeLog(QStringLiteral("light"), outputL, exitCodeL);
                if (exitCodeL == 0) {
                    qInfo() << "[Brightness] light applied:" << percent << "%";
                    return;
                }
                qWarning() << "[Brightness] light failed, trying brightnessctl...";
                tryBrightnessctl();
            });
            procLight->start(QStringLiteral("light"), QStringList() << "-S" << QString::number(percent));
            if (!procLight->waitForStarted(3000)) {
                qWarning() << "[Brightness] Failed to start light";
                writeLog(QStringLiteral("light"), QStringLiteral("START_FAILED"), -1);
                procLight->deleteLater();
                tryBrightnessctl();
            }
        });

        procB->start(QStringLiteral("sh"), QStringList() << "-c" << xrandrScript);
        if (!procB->waitForStarted(3000)) {
            qWarning() << "[Brightness] Failed to start xrandr";
            writeLog(QStringLiteral("xrandr"), QStringLiteral("START_FAILED"), -1);
            procB->deleteLater();
            tryBrightnessctl();
        }
    });

    procA->start(QStringLiteral("sh"), QStringList() << "-c" << backlightScript);
    if (!procA->waitForStarted(3000)) {
        qWarning() << "[Brightness] Failed to start backlight script";
        writeLog(QStringLiteral("backlight"), QStringLiteral("START_FAILED"), -1);
        procA->deleteLater();
        // 直接尝试方案B
        QString xrandrScript = buildXrandrScript();
        auto* procB = new QProcess(this);
        connect(procB, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, percent, writeLog, tryBrightnessctl](int exitCodeB, QProcess::ExitStatus) {
            auto* pB = qobject_cast<QProcess*>(sender());
            if (!pB) return;
            QString outputB = pB->readAllStandardOutput().trimmed();
            pB->deleteLater();
            writeLog(QStringLiteral("xrandr"), outputB, exitCodeB);
            if (exitCodeB != 0 || !outputB.contains(QStringLiteral("XRANDR_OK"))) {
                tryBrightnessctl();
            }
        });
        procB->start(QStringLiteral("sh"), QStringList() << "-c" << xrandrScript);
        if (!procB->waitForStarted(3000)) {
            procB->deleteLater();
            tryBrightnessctl();
        }
    }
}
#endif
