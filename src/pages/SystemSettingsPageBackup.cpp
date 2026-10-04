/**
 * @file SystemSettingsPageBackup.cpp
 * @brief 系统设置-数据库自动备份（SQLite拷贝 / MySQL mysqldump）
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

// 执行数据库自动备份
// 备份策略：
// 1. 保存备份设置时立即执行一次备份（验证备份路径可用）
// 2. 根据备份周期（每日/每周一/每周日）计算下次备份时间
// 3. MainWindow 启动定时器每小时检查一次是否到了备份时间
// 备份内容：SQLite文件拷贝 / MySQL用mysqldump导出
// 备份文件命名：smartcabinet_backup_YYYYMMDD_HHMMSS.db
void SystemSettingsPage::performDatabaseBackup() {
    auto& cfg = AppConfig::instance();
    QString backupPath = cfg.backupPath();
    bool isAutoEnabled = cfg.backupAutoEnabled();
    int period = cfg.backupPeriod();

    // 日志路径跨平台
    QString logPath;
#ifdef Q_OS_WIN
    logPath = QStringLiteral("d:/CFDZ/smartCabinet/trunk/code/temp/backup.log");
#else
    logPath = QStringLiteral("/tmp/smartcabinet_backup.log");
#endif

    auto writeBackupLog = [logPath](const QString& action, const QString& result, const QString& detail) {
        Log::appendLog(logPath, QStringLiteral("action=%1 result=%2 detail=%3")
                                   .arg(action).arg(result).arg(detail));
    };

    if (!isAutoEnabled) {
        writeBackupLog(QStringLiteral("SKIP"), QStringLiteral("DISABLED"),
                       QStringLiteral("auto backup is disabled"));
        return;
    }

    // 检查备份周期：每日/每周一/每周日
    QDate today = QDate::currentDate();
    int dayOfWeek = today.dayOfWeek();  // 1=周一, 7=周日
    bool shouldBackup = false;
    QString periodDesc;
    if (period == 0) {
        // 每日备份
        shouldBackup = true;
        periodDesc = QStringLiteral("每日");
    } else if (period == 1) {
        // 每周一备份
        shouldBackup = (dayOfWeek == 1);
        periodDesc = QStringLiteral("每周一");
    } else if (period == 2) {
        // 每周日备份
        shouldBackup = (dayOfWeek == 7);
        periodDesc = QStringLiteral("每周日");
    }

    // 如果不是备份日，跳过（但保存设置时的首次备份强制执行）
    // 这里首次保存时强制备份，定时检查时才按周期判断

    // 确保备份目录存在
    QDir dir;
    if (!dir.exists(backupPath)) {
        if (!dir.mkpath(backupPath)) {
            writeBackupLog(QStringLiteral("CREATE_DIR"), QStringLiteral("FAIL"),
                           QStringLiteral("cannot create: %1").arg(backupPath));
            qWarning() << "[Backup] Cannot create backup dir:" << backupPath;
            return;
        }
    }

    // 生成备份文件名
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString backupFile;

    // 判断数据库类型：SQLite文件拷贝 / mysqldump导出
    DatabaseManager& db = DatabaseManager::instance();

#ifdef Q_OS_WIN
    // Windows: 检查SQLite文件是否存在
    QString dbPath = QStringLiteral("d:/CFDZ/smartCabinet/trunk/QtSmartCabinet/build/smartcabinet.db");
#else
    QString dbPath = QStringLiteral("/var/lib/smartcabinet/smartcabinet.db");
#endif

    if (QFile::exists(dbPath)) {
        // SQLite模式：拷贝数据库文件
        backupFile = backupPath + QDir::separator() +
                     QStringLiteral("smartcabinet_backup_%1.db").arg(timestamp);
        if (QFile::copy(dbPath, backupFile)) {
            writeBackupLog(QStringLiteral("BACKUP"), QStringLiteral("OK"),
                           QStringLiteral("SQLite file copied to: %1").arg(backupFile));
            qInfo() << "[Backup] SQLite backup success:" << backupFile;

            // 清理超过7天的旧备份文件
            QDir backupDir(backupPath);
            QStringList filters;
            filters << "smartcabinet_backup_*.db";
            QFileInfoList oldFiles = backupDir.entryInfoList(filters, QDir::Files, QDir::Time);
            for (const QFileInfo& fi : oldFiles) {
                if (fi.lastModified().daysTo(QDateTime::currentDateTime()) > 7) {
                    QFile::remove(fi.absoluteFilePath());
                    writeBackupLog(QStringLiteral("CLEANUP"), QStringLiteral("OK"),
                                   QStringLiteral("removed old: %1").arg(fi.fileName()));
                }
            }
        } else {
            writeBackupLog(QStringLiteral("BACKUP"), QStringLiteral("FAIL"),
                           QStringLiteral("cannot copy %1 to %2").arg(dbPath).arg(backupFile));
            qWarning() << "[Backup] SQLite backup failed:" << backupFile;
        }
    } else {
        // MySQL模式：用mysqldump导出
        backupFile = backupPath + QDir::separator() +
                     QStringLiteral("smartcabinet_backup_%1.sql").arg(timestamp);
        QString dbName = cfg.dbName();
        QString dbUser = cfg.dbUser();
        QString dbPass = cfg.dbPass();
        QString dbHost = cfg.dbHost();

        QString dumpCmd = QStringLiteral("mysqldump -h%1 -u%2 -p%3 %4")
                              .arg(dbHost).arg(dbUser).arg(dbPass).arg(dbName);

        auto* proc = new QProcess(this);
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, backupFile, writeBackupLog](int exitCode, QProcess::ExitStatus) {
            auto* p = qobject_cast<QProcess*>(sender());
            if (!p) return;
            p->deleteLater();
            if (exitCode == 0) {
                writeBackupLog(QStringLiteral("BACKUP"), QStringLiteral("OK"),
                               QStringLiteral("MySQL dump: %1").arg(backupFile));
                qInfo() << "[Backup] MySQL backup success:" << backupFile;
            } else {
                writeBackupLog(QStringLiteral("BACKUP"), QStringLiteral("FAIL"),
                               QStringLiteral("mysqldump exitCode=%1").arg(exitCode));
                qWarning() << "[Backup] MySQL backup failed, exitCode:" << exitCode;
            }
        });
        // 重定向输出到文件
        proc->setStandardOutputFile(backupFile);
        proc->start("mysqldump", QStringList()
                    << QStringLiteral("-h%1").arg(dbHost)
                    << QStringLiteral("-u%1").arg(dbUser)
                    << QStringLiteral("-p%1").arg(dbPass)
                    << dbName);
        if (!proc->waitForStarted(3000)) {
            writeBackupLog(QStringLiteral("BACKUP"), QStringLiteral("FAIL"),
                           QStringLiteral("cannot start mysqldump"));
            qWarning() << "[Backup] Cannot start mysqldump";
            proc->deleteLater();
        }
    }
}
