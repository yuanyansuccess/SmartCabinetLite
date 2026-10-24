/**
 * @file SystemSettingsPageBackup.cpp
 * @brief 系统设置-数据库备份（自动备份与手动备份，mysqldump 导出）
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
#include "common/DiskSpaceGuard.h"  // 导出前磁盘容量保护
#include "components/MessageDialog.h"
#include <QDebug>
#include <QProcess>  // 调用PowerShell设置显示器亮度
#include <QFile>  // WMI结果日志记录
#include <QDateTime>  // 亮度日志时间戳
#include <QDir>  // 备份目录操作

/**
 * @brief 执行一次数据库自动备份（保存备份设置时立即导出，用于验证备份路径可用）
 */
void SystemSettingsPage::performDatabaseBackup() {
    auto& cfg = AppConfig::instance();

    // 日志路径统一走 Logger 目录（程序数据目录/logs，跨平台且自动创建）
    QString logPath = Log::logDirectory() + "/backup.log";

    if (!cfg.backupAutoEnabled()) {
        Log::appendLog(logPath, QStringLiteral("action=SKIP result=DISABLED detail=auto backup is disabled"));
        return;
    }
    // 防止自动（定时器）/保存触发并发启动多个 mysqldump
    if (m_backingUp) return;
    m_backingUp = true;

    // 备份成功后记录"上次备份日期"，供自动备份按周期去重（避免同一天重复导出）
    startMysqlDumpBackup(cfg.backupPath(), [this](bool ok, const QString&) {
        m_backingUp = false;
        if (ok) {
            AppConfig::instance().setLastBackupDate(QDate::currentDate());
            AppConfig::instance().save();
        }
    });
}

/**
 * @brief 启动一次 mysqldump 全库导出，异步执行不阻塞界面
 * @param backupPath 备份文件存放目录，不存在时自动创建
 * @param onFinished 完成回调：参数1=true 表示导出成功，参数2=备份文件完整路径（失败为空串）
 */
void SystemSettingsPage::startMysqlDumpBackup(const QString& backupPath,
                                              const std::function<void(bool, const QString&)>& onFinished) {
    // 日志路径统一走 Logger 目录（程序数据目录/logs，跨平台且自动创建）
    QString logPath = Log::logDirectory() + "/backup.log";
    auto writeBackupLog = [logPath](const QString& action, const QString& result, const QString& detail) {
        Log::appendLog(logPath, QStringLiteral("action=%1 result=%2 detail=%3")
                                   .arg(action).arg(result).arg(detail));
    };

    // 确保备份目录存在
    QDir dir;
    if (!dir.exists(backupPath) && !dir.mkpath(backupPath)) {
        writeBackupLog(QStringLiteral("CREATE_DIR"), QStringLiteral("FAIL"),
                       QStringLiteral("cannot create: %1").arg(backupPath));
        qWarning() << "[Backup] Cannot create backup dir:" << backupPath;
        if (onFinished) onFinished(false, QString());
        return;
    }

    // 磁盘容量保护：可用比例不足时先静默清理最旧的备份文件，用户无感知
    DiskGuard::CleanupResult diskGuard = DiskGuard::ensureFreePercent(
        backupPath, { QStringLiteral("smartcabinet_backup_*.sql") });
    if (diskGuard.deletedFiles > 0) {
        writeBackupLog(QStringLiteral("DISK_GUARD"), QStringLiteral("OK"),
                       QStringLiteral("deleted=%1 freedKB=%2 freePercent=%3")
                           .arg(diskGuard.deletedFiles)
                           .arg(diskGuard.freedBytes / 1024)
                           .arg(diskGuard.freePercent));
    }

    // 备份文件命名：smartcabinet_backup_YYYYMMDD_HHMMSS.sql
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString backupFile = backupPath + QDir::separator() +
                         QStringLiteral("smartcabinet_backup_%1.sql").arg(timestamp);

    auto& cfg = AppConfig::instance();
    auto* proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [proc, backupFile, writeBackupLog, onFinished](int exitCode, QProcess::ExitStatus) {
        QByteArray errOutput = proc->readAllStandardError();
        proc->deleteLater();
        if (exitCode == 0) {
            writeBackupLog(QStringLiteral("BACKUP"), QStringLiteral("OK"),
                           QStringLiteral("MySQL dump: %1").arg(backupFile));
            qInfo() << "[Backup] MySQL backup success:" << backupFile;
            if (onFinished) onFinished(true, backupFile);
            return;
        }
        // 导出失败时删除残留文件，避免留下内容不完整的假备份
        QFile::remove(backupFile);
        writeBackupLog(QStringLiteral("BACKUP"), QStringLiteral("FAIL"),
                       QStringLiteral("mysqldump exitCode=%1").arg(exitCode));
        qWarning() << "[Backup] MySQL backup failed, exitCode:" << exitCode << errOutput;
        if (onFinished) onFinished(false, QString());
    });
    // 重定向输出到备份文件
    proc->setStandardOutputFile(backupFile);
    // 密码为空时不附加 -p 参数，否则 mysqldump 会交互式等待输入而卡死
    QStringList dumpArgs;
    dumpArgs << QStringLiteral("-h%1").arg(cfg.dbHost())
             << QStringLiteral("-u%1").arg(cfg.dbUser());
    if (!cfg.dbPass().isEmpty())
        dumpArgs << QStringLiteral("-p%1").arg(cfg.dbPass());
    dumpArgs << cfg.dbName();
    proc->start("mysqldump", dumpArgs);
    if (!proc->waitForStarted(3000)) {
        QFile::remove(backupFile);
        writeBackupLog(QStringLiteral("BACKUP"), QStringLiteral("FAIL"),
                       QStringLiteral("cannot start mysqldump"));
        qWarning() << "[Backup] Cannot start mysqldump";
        proc->deleteLater();
        if (onFinished) onFinished(false, QString());
    }
}

/**
 * @brief 手动备份按钮：立即导出一次数据库，成功或失败都弹窗告知结果
 * 与自动备份的区别：不受"自动备份"开关与备份周期限制，随时可点击
 */
void SystemSettingsPage::onManualBackupClicked() {
    if (!m_manualBackupBtn) return;
    // 与自动备份互斥，避免并发启动多个 mysqldump
    if (m_backingUp) {
        MessageDialog::showError(this, QStringLiteral("请稍候"),
                                 QStringLiteral("正在执行备份（自动或手动），请稍候完成。"));
        return;
    }

    auto& cfg = AppConfig::instance();
    // 路径优先取界面当前填写值（用户可能改了还没保存），为空时回落到已保存的配置
    QString backupPath = m_backupPathEdit ? m_backupPathEdit->text().trimmed() : QString();
    if (backupPath.isEmpty()) backupPath = cfg.backupPath();

    m_backingUp = true;
    // 导出期间禁用按钮，防止连点同时启动多个 mysqldump
    m_manualBackupBtn->setEnabled(false);
    m_manualBackupBtn->setText(QStringLiteral("正在备份..."));

    startMysqlDumpBackup(backupPath, [this](bool ok, const QString& backupFile) {
        m_backingUp = false;
        if (!m_manualBackupBtn) return;
        m_manualBackupBtn->setEnabled(true);
        m_manualBackupBtn->setText(QStringLiteral("手动备份"));
        if (ok) {
            // 手动备份成功也刷新“上次备份日期”，避免紧接着的自动备份又重复导出
            AppConfig::instance().setLastBackupDate(QDate::currentDate());
            AppConfig::instance().save();
            MessageDialog::showSuccess(this, QStringLiteral("备份成功"),
                                       QStringLiteral("数据库已备份到：\n%1").arg(backupFile));
        } else {
            MessageDialog::showError(this, QStringLiteral("备份失败"),
                                     QStringLiteral("数据库备份未完成，请检查备份路径是否可写、数据库服务是否正常。"));
        }
    });
}
