/**
 * @file SystemSettingsPageSystemInfo.cpp
 * @brief 系统设置-系统信息读取（OS/设备号/运行时长/磁盘/CPU/内存）
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
#include <QTimer>  // CPU使用率二次采样延时
#include <QProcess>  // 调用PowerShell设置显示器亮度
#include <QFile>  // WMI结果日志记录
#include <QDateTime>  // 亮度日志时间戳
#include <QDir>  // 备份目录操作
#include <QStorageInfo>  // 跨平台磁盘空间读取
#include <QSysInfo>  // 跨平台系统信息读取
#include <QRegularExpression>  // 解析os-release
#include <QFileInfoList>  // 备份文件清理

// 从系统读取真实系统信息
// 操作系统：Windows用QSysInfo，麒麟读/etc/os-release
// 设备编号：Windows用机器名，麒麟读/etc/machine-id
// 运行时长：Windows用PowerShell计算LastBootUpTime差值，麒麟读/proc/uptime
// 磁盘空间：QStorageInfo跨平台
// CPU占用：Windows用wmic，麒麟读/proc/stat两次采样
// 内存占用：Windows用wmic，麒麟读/proc/meminfo
void SystemSettingsPage::refreshSystemInfo() {
    refreshOsInfo();
    refreshDeviceId();
    refreshUptime();
    refreshDiskSpace();
    refreshCpuUsage();
    refreshMemoryUsage();
}

/** 读取操作系统名称：Windows用QSysInfo，麒麟读/etc/os-release（降级/etc/kylin-build） */
void SystemSettingsPage::refreshOsInfo() {
    QString osInfo;
#ifdef Q_OS_WIN
    osInfo = QSysInfo::prettyProductName();  // 如 "Windows 10 (10.0)"
#else
    // 麒麟系统读 /etc/os-release 获取发行版信息
    QFile osFile("/etc/os-release");
    if (osFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString content = QString::fromUtf8(osFile.readAll());
        osFile.close();
        QRegularExpression re("PRETTY_NAME=\"([^\"]+)\"");
        QRegularExpressionMatch match = re.match(content);
        if (match.hasMatch()) {
            osInfo = match.captured(1);
        }
    }
    if (osInfo.isEmpty()) {
        // 降级读 /etc/kylin-build 或用 QSysInfo
        QFile kylinFile("/etc/kylin-build");
        if (kylinFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            osInfo = QString::fromUtf8(kylinFile.readLine()).trimmed();
            kylinFile.close();
        }
    }
    if (osInfo.isEmpty()) {
        osInfo = QSysInfo::prettyProductName();
    }
#endif
    if (m_osLabel) m_osLabel->setText(osInfo);
}

/** 读取设备编号：Windows用机器名，麒麟读/etc/machine-id */
void SystemSettingsPage::refreshDeviceId() {
    QString deviceId;
#ifdef Q_OS_WIN
    // Windows用机器名
    deviceId = QSysInfo::machineHostName();
#else
    // 麒麟读 /etc/machine-id
    QFile machineIdFile("/etc/machine-id");
    if (machineIdFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        deviceId = QString::fromUtf8(machineIdFile.readAll()).trimmed();
        machineIdFile.close();
    }
    if (deviceId.isEmpty()) {
        deviceId = QSysInfo::machineHostName();
    }
#endif
    if (m_deviceIdLabel) m_deviceIdLabel->setText(deviceId);
}

/** 读取运行时长：Windows用PowerShell计算LastBootUpTime差值，麒麟读/proc/uptime */
void SystemSettingsPage::refreshUptime() {
#ifdef Q_OS_WIN
    // wmic os get LastBootUpTime 返回的是日期格式(如20260627100000.000000+480)
    // 不是秒数，必须用PowerShell计算 (Get-Date) - LastBootUpTime 的差值
    auto* uptimeProc = new QProcess(this);
    connect(uptimeProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) {
        auto* p = qobject_cast<QProcess*>(sender());
        if (!p) return;
        QString output = p->readAllStandardOutput().trimmed();
        p->deleteLater();
        // PowerShell输出格式: "TotalSeconds" 后跟数字，或直接是秒数
        // 用正则提取数字部分
        QRegularExpression re("(\\d+)");
        QRegularExpressionMatchIterator it = re.globalMatch(output);
        qint64 secs = 0;
        // 取最大的数字作为秒数（避免误匹配小数部分）
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            qint64 val = m.captured(1).toLongLong();
            if (val > secs) secs = val;
        }
        if (secs > 0) {
            int days = secs / 86400;
            int hours = (secs % 86400) / 3600;
            int minutes = (secs % 3600) / 60;
            if (m_uptimeLabel) {
                m_uptimeLabel->setText(QStringLiteral("%1天 %2小时%3分").arg(days).arg(hours).arg(minutes));
            }
        } else {
            if (m_uptimeLabel) m_uptimeLabel->setText(QStringLiteral("读取失败"));
        }
    });
    // PowerShell: 计算系统启动至今的总秒数
    uptimeProc->start("powershell.exe", QStringList() << "-NoProfile" << "-NonInteractive" << "-Command"
                      << "[Math]::Floor((Get-Date) - (Get-CimInstance Win32_OperatingSystem).LastBootUpTime | Select-Object -ExpandProperty TotalSeconds)");
    if (!uptimeProc->waitForStarted(3000)) {
        uptimeProc->deleteLater();
        if (m_uptimeLabel) m_uptimeLabel->setText(QStringLiteral("读取失败"));
    }
#else
    // 麒麟读 /proc/uptime 第一列（秒）
    QString uptime;
    QFile uptimeFile("/proc/uptime");
    if (uptimeFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString content = QString::fromUtf8(uptimeFile.readLine());
        uptimeFile.close();
        QStringList parts = content.split(' ');
        if (parts.size() >= 1) {
            bool ok = false;
            double secs = parts[0].toDouble(&ok);
            if (ok && secs > 0) {
                int days = (int)(secs / 86400);
                int hours = (int)((secs - days * 86400) / 3600);
                int minutes = (int)((secs - days * 86400 - hours * 3600) / 60);
                uptime = QStringLiteral("%1天 %2小时%3分").arg(days).arg(hours).arg(minutes);
            }
        }
    }
    if (uptime.isEmpty()) uptime = QStringLiteral("读取失败");
    if (m_uptimeLabel) m_uptimeLabel->setText(uptime);
#endif
}

/** 读取磁盘空间：QStorageInfo跨平台（根分区） */
void SystemSettingsPage::refreshDiskSpace() {
    QString diskInfo;
    QStorageInfo storage = QStorageInfo::root();
    if (storage.isValid() && storage.isReady()) {
        qint64 total = storage.bytesTotal();
        qint64 free = storage.bytesFree();
        qint64 used = total - free;
        double usedGB = used / (1024.0 * 1024.0 * 1024.0);
        double totalGB = total / (1024.0 * 1024.0 * 1024.0);
        int percent = (total > 0) ? (int)(used * 100 / total) : 0;
        diskInfo = QStringLiteral("已用 %1GB / 共 %2GB (%3%)")
                       .arg(QString::number(usedGB, 'f', 1))
                       .arg(QString::number(totalGB, 'f', 1))
                       .arg(percent);
    } else {
        diskInfo = QStringLiteral("无法读取");
    }
    if (m_diskLabel) m_diskLabel->setText(diskInfo);
}

/** 读取CPU占用率：Windows用wmic，麒麟读/proc/stat两次采样 */
void SystemSettingsPage::refreshCpuUsage() {
#ifdef Q_OS_WIN
    // Windows: wmic cpu get loadpercentage 直接返回占用百分比
    auto* cpuProc = new QProcess(this);
    connect(cpuProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) {
        auto* p = qobject_cast<QProcess*>(sender());
        if (!p) return;
        QString output = p->readAllStandardOutput().trimmed();
        p->deleteLater();
        // wmic输出: "LoadPercentage\n56"，提取数字
        QRegularExpression re("(\\d+)");
        QRegularExpressionMatch m = re.match(output);
        if (m.hasMatch()) {
            if (m_cpuLabel) {
                m_cpuLabel->setText(QStringLiteral("%1%").arg(m.captured(1)));
            }
        } else {
            if (m_cpuLabel) m_cpuLabel->setText(QStringLiteral("读取失败"));
        }
    });
    cpuProc->start("wmic", QStringList() << "cpu" << "get" << "loadpercentage");
    if (!cpuProc->waitForStarted(3000)) {
        cpuProc->deleteLater();
        if (m_cpuLabel) m_cpuLabel->setText(QStringLiteral("读取失败"));
    }
#else
    // 麒麟: 读 /proc/stat 两次采样计算CPU占用率
    // CPU占用 = (idle2-idle1) / (total2-total1) 的反值
    auto readCpuStat = []() -> QPair<qint64, qint64> {
        QFile f("/proc/stat");
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {0, 0};
        QString line = QString::fromUtf8(f.readLine());
        f.close();
        // 格式: cpu user nice system idle iowait irq softirq steal guest guest_nice
        QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() < 5) return {0, 0};
        qint64 total = 0;
        for (int i = 1; i < parts.size(); ++i) total += parts[i].toLongLong();
        qint64 idle = parts[4].toLongLong();
        return {total, idle};
    };
    QPair<qint64, qint64> s1 = readCpuStat();
    QTimer::singleShot(SC::UI_CPU_STATS_DELAY_MS, this, [this, s1]() {
        auto readCpuStat2 = []() -> QPair<qint64, qint64> {
            QFile f("/proc/stat");
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {0, 0};
            QString line = QString::fromUtf8(f.readLine());
            f.close();
            QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (parts.size() < 5) return {0, 0};
            qint64 total = 0;
            for (int i = 1; i < parts.size(); ++i) total += parts[i].toLongLong();
            qint64 idle = parts[4].toLongLong();
            return {total, idle};
        };
        QPair<qint64, qint64> s2 = readCpuStat2();
        qint64 totalDiff = s2.first - s1.first;
        qint64 idleDiff = s2.second - s1.second;
        int cpuPercent = 0;
        if (totalDiff > 0) {
            cpuPercent = (int)((totalDiff - idleDiff) * 100 / totalDiff);
        }
        if (m_cpuLabel) m_cpuLabel->setText(QStringLiteral("%1%").arg(cpuPercent));
    });
#endif
}

/** 读取内存占用率：Windows用wmic，麒麟读/proc/meminfo */
void SystemSettingsPage::refreshMemoryUsage() {
#ifdef Q_OS_WIN
    // Windows: wmic OS get TotalVisibleMemorySize,FreePhysicalMemory
    // 返回KB单位，计算 (total-free)/total*100
    auto* memProc = new QProcess(this);
    connect(memProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) {
        auto* p = qobject_cast<QProcess*>(sender());
        if (!p) return;
        QString output = p->readAllStandardOutput().trimmed();
        p->deleteLater();
        // wmic输出两行: "FreePhysicalMemory TotalVisibleMemorySize\n1234567 8388608"
        QStringList lines = output.split('\n');
        if (lines.size() >= 2) {
            QStringList vals = lines[1].trimmed().split(QRegularExpression("\\s+"));
            if (vals.size() >= 2) {
                qint64 freeKB = vals[0].toLongLong();
                qint64 totalKB = vals[1].toLongLong();
                if (totalKB > 0) {
                    qint64 usedKB = totalKB - freeKB;
                    int percent = (int)(usedKB * 100 / totalKB);
                    double usedGB = usedKB / (1024.0 * 1024.0);
                    double totalGB = totalKB / (1024.0 * 1024.0);
                    if (m_memoryLabel) {
                        m_memoryLabel->setText(QStringLiteral("已用 %1GB / 共 %2GB (%3%)")
                            .arg(QString::number(usedGB, 'f', 1))
                            .arg(QString::number(totalGB, 'f', 1))
                            .arg(percent));
                    }
                }
            }
        }
        if (m_memoryLabel && m_memoryLabel->text() == QStringLiteral("读取中...")) {
            m_memoryLabel->setText(QStringLiteral("读取失败"));
        }
    });
    memProc->start("wmic", QStringList() << "OS" << "get" << "FreePhysicalMemory,TotalVisibleMemorySize");
    if (!memProc->waitForStarted(3000)) {
        memProc->deleteLater();
        if (m_memoryLabel) m_memoryLabel->setText(QStringLiteral("读取失败"));
    }
#else
    // 麒麟: 读 /proc/meminfo
    // MemTotal: 总内存, MemAvailable: 可用内存（含缓存）
    qint64 memTotal = 0, memAvailable = 0;
    QFile memFile("/proc/meminfo");
    if (memFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!memFile.atEnd()) {
            QString line = QString::fromUtf8(memFile.readLine());
            if (line.startsWith("MemTotal:")) {
                QRegularExpression re("(\\d+)");
                QRegularExpressionMatch m = re.match(line);
                if (m.hasMatch()) memTotal = m.captured(1).toLongLong();
            } else if (line.startsWith("MemAvailable:")) {
                QRegularExpression re("(\\d+)");
                QRegularExpressionMatch m = re.match(line);
                if (m.hasMatch()) memAvailable = m.captured(1).toLongLong();
            }
            if (memTotal > 0 && memAvailable > 0) break;
        }
        memFile.close();
    }
    if (memTotal > 0) {
        qint64 memUsed = memTotal - memAvailable;
        int percent = (int)(memUsed * 100 / memTotal);
        double usedGB = memUsed / (1024.0 * 1024.0);
        double totalGB = memTotal / (1024.0 * 1024.0);
        if (m_memoryLabel) {
            m_memoryLabel->setText(QStringLiteral("已用 %1GB / 共 %2GB (%3%)")
                .arg(QString::number(usedGB, 'f', 1))
                .arg(QString::number(totalGB, 'f', 1))
                .arg(percent));
        }
    } else {
        if (m_memoryLabel) m_memoryLabel->setText(QStringLiteral("读取失败"));
    }
#endif
}
