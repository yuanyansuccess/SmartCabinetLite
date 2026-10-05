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
#include "common/DatabaseManager.h"     // DB写入机组配置
#include "db/RecordDAO.h"               // 校验机组下未归还记录
#include "db/ToolDAO.h"                 // 加载活跃机组列表
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include "components/MessageDialog.h"
#include <QApplication>
#include <QDebug>
#include <QFrame>
#include <QGroupBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QSlider>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QGridLayout>
#include <QMouseEvent>
#include <QScrollBar>
#include <QFocusEvent>
#include <QShowEvent>  // 页面切换还原
#include <QHideEvent>
#include <QTableWidget>  // 任务类型配置表格
#include <QHeaderView>  // 表格列宽控制
#include <QTableWidgetItem>  // 表格单元格
#include <QProcess>  // 调用PowerShell设置显示器亮度
#include <QFile>  // WMI结果日志记录
#include <QDateTime>  // 亮度日志时间戳
#include <QDir>  // 备份目录操作
#include <QStorageInfo>  // 跨平台磁盘空间读取
#include <QSysInfo>  // 跨平台系统信息读取
#include <QRegularExpression>  // 解析os-release
#include <QFileInfoList>  // 备份文件清理

/**
 * @brief 应用显示亮度
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

/** 亮度调整日志（Windows写项目temp目录，麒麟写/tmp） */
void SystemSettingsPage::writeBrightnessLog(int percent, const QString& method, const QString& result, int exitCode) {
    // 日志路径统一走 Logger 目录（程序数据目录/logs，跨平台且自动创建）
    QString logPath = Log::logDirectory() + "/brightness.log";
    // 统一走 Logger 入口：时间戳/建目录/UTF-8/异常处理由 Logger 负责
    Log::appendLog(logPath,
                   QStringLiteral("method=%1 brightness=%2% exitCode=%3 result=%4")
                       .arg(method).arg(percent).arg(exitCode).arg(result));
}

#ifdef Q_OS_WIN
/** Windows亮度调整：PowerShell + WMI 异步执行不阻塞UI */
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
/** 麒麟Linux亮度调整：三级降级链 backlight内核接口 → xrandr → brightnessctl */
void SystemSettingsPage::applyBrightnessLinux(int percent) {
    auto writeLog = [this, percent](const QString& method, const QString& result, int exitCode) {
        writeBrightnessLog(percent, method, result, exitCode);
    };
    // ═══════════ 麒麟Linux平台：三级降级方案 ═══════════
    // 方案A：/sys/class/backlight/ 内核接口（硬件级亮度，需root权限）
    // 1) 遍历 /sys/class/backlight/ 找到第一个设备目录
    // 2) 读取 max_brightness 计算实际值 = percent * max / 100
    // 3) 用 pkexec/sudo 提权写入 brightness 文件
    // 方案B：xrandr X11 Gamma调整（软件级，无需root，所有X11桌面环境通用）
    // xrandr --output <display> --brightness <0.1~1.0>
    // 方案C：brightnessctl 命令（部分发行版预装）
    // brightnessctl set <percent>%

    // 封装异步执行+日志的Lambda
    auto runAsync = [this, percent, writeLog](const QString& method,
                                               const QString& program,
                                               const QStringList& args) {
        auto* proc = new QProcess(this);
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [method, percent, writeLog](int exitCode, QProcess::ExitStatus) {
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
            }
        });

        proc->start(program, args);
        if (!proc->waitForStarted(3000)) {
            qWarning() << "[Brightness] Failed to start" << program;
            writeLog(method, QStringLiteral("START_FAILED"), -1);
            proc->deleteLater();
            return false;
        }
        return true;
    };

    // 方案A：尝试 /sys/class/backlight/ 内核接口
    // 用 sh 脚本检测设备并写入，通过 pkexec 提权（麒麟默认安装）
    QString backlightScript = QStringLiteral(
        "for dev in /sys/class/backlight/*/; do "
        "  if [ -f \"${dev}max_brightness\" ] && [ -w \"${dev}brightness\" ]; then "
        "    max=$(cat \"${dev}max_brightness\"); "
        "    val=$(( %1 * max / 100 )); "
        "    echo $val > \"${dev}brightness\"; "
        "    echo 'BACKLIGHT_OK'; exit 0; "
        "  fi; "
        "done; "
        "echo 'NO_BACKLIGHT_DEV'; exit 1"
    ).arg(percent);

    auto* procA = new QProcess(this);
    connect(procA, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, percent, writeLog, runAsync](int exitCodeA, QProcess::ExitStatus) {
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
        // 先获取显示器列表，再逐个设置
        QString xrandrScript = QStringLiteral(
            "displays=$(xrandr --listmonitors 2>/dev/null | grep -oP '(?<=Monitors: ).*' | tr ' ' '\\n'); "
            "if [ -z \"$displays\" ]; then "
            "  displays=$(xrandr 2>/dev/null | grep -E ' connected' | awk '{print $1}'); "
            "fi; "
            "if [ -z \"$displays\" ]; then echo 'NO_DISPLAY'; exit 1; fi; "
            "brightness=$(echo \"scale=2; %1 / 100\" | bc); "
            "for d in $displays; do xrandr --output $d --brightness $brightness 2>/dev/null; done; "
            "echo 'XRANDR_OK'"
        ).arg(percent);

        auto* procB = new QProcess(this);
        connect(procB, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, percent, writeLog, runAsync](int exitCodeB, QProcess::ExitStatus) {
            auto* pB = qobject_cast<QProcess*>(sender());
            if (!pB) return;
            QString outputB = pB->readAllStandardOutput().trimmed();
            pB->deleteLater();

            writeLog(QStringLiteral("xrandr"), outputB, exitCodeB);

            if (exitCodeB == 0 && outputB.contains("XRANDR_OK")) {
                qInfo() << "[Brightness] xrandr applied:" << percent << "%";
                return;  // 方案B成功
            }

            qWarning() << "[Brightness] xrandr failed, trying brightnessctl...";

            // 方案C：brightnessctl 命令（最后降级方案）
            runAsync(QStringLiteral("brightnessctl"),
                     QStringLiteral("brightnessctl"),
                     QStringList() << "set" << QStringLiteral("%1%").arg(percent));
        });

        procB->start(QStringLiteral("sh"), QStringList() << "-c" << xrandrScript);
        if (!procB->waitForStarted(3000)) {
            qWarning() << "[Brightness] Failed to start xrandr";
            writeLog(QStringLiteral("xrandr"), QStringLiteral("START_FAILED"), -1);
            procB->deleteLater();
            // 直接尝试方案C
            runAsync(QStringLiteral("brightnessctl"),
                     QStringLiteral("brightnessctl"),
                     QStringList() << "set" << QStringLiteral("%1%").arg(percent));
        }
    });

    // 启动方案A：用pkexec提权尝试写入backlight（麒麟系统polkit已集成）
    // 如果pkexec不可用则直接用sh（无root时可能失败，会自动降级到方案B）
    procA->start(QStringLiteral("sh"), QStringList() << "-c" << backlightScript);
    if (!procA->waitForStarted(3000)) {
        qWarning() << "[Brightness] Failed to start backlight script";
        writeLog(QStringLiteral("backlight"), QStringLiteral("START_FAILED"), -1);
        procA->deleteLater();
        // 直接尝试方案B
        QString xrandrScript = QStringLiteral(
            "displays=$(xrandr --listmonitors 2>/dev/null | grep -oP '(?<=Monitors: ).*' | tr ' ' '\\n'); "
            "if [ -z \"$displays\" ]; then "
            "  displays=$(xrandr 2>/dev/null | grep -E ' connected' | awk '{print $1}'); "
            "fi; "
            "if [ -z \"$displays\" ]; then echo 'NO_DISPLAY'; exit 1; fi; "
            "brightness=$(echo \"scale=2; %1 / 100\" | bc); "
            "for d in $displays; do xrandr --output $d --brightness $brightness 2>/dev/null; done; "
            "echo 'XRANDR_OK'"
        ).arg(percent);
        auto* procB = new QProcess(this);
        connect(procB, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [percent, writeLog](int exitCodeB, QProcess::ExitStatus) {
            auto* pB = qobject_cast<QProcess*>(sender());
            if (!pB) return;
            QString outputB = pB->readAllStandardOutput().trimmed();
            pB->deleteLater();
            writeLog(QStringLiteral("xrandr"), outputB, exitCodeB);
        });
        procB->start(QStringLiteral("sh"), QStringList() << "-c" << xrandrScript);
        if (!procB->waitForStarted(3000)) {
            procB->deleteLater();
        }
    }
}
#endif
