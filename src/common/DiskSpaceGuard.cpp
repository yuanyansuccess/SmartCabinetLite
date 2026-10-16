/**
 * @file DiskSpaceGuard.cpp
 * @brief 磁盘容量保护实现：可用比例检测 + 按修改时间从旧到新清理目标目录下的自有文件
 * @author 袁燕
 */
#include "DiskSpaceGuard.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QStorageInfo>

#include <algorithm>

namespace DiskGuard {
namespace {

QMutex g_mutex;
// 每个目录上一次执行清理的时间戳(毫秒)：清理后仍不达标的目录，间隔内直接放行不再扫描
QHash<QString, qint64> g_lastCleanupMsec;

/// 当前 Unix 时间戳(毫秒)
qint64 nowMsec()
{
    return QDateTime::currentMSecsSinceEpoch();
}

}  // namespace

int freePercentOf(const QString& path)
{
    if (path.isEmpty()) return -1;

    QStorageInfo info(path);
    if (!info.isValid() || !info.isReady() || info.bytesTotal() <= 0) return -1;

    const qint64 available = info.bytesAvailable();
    if (available < 0) return -1;

    return static_cast<int>(available * 100 / info.bytesTotal());
}

CleanupResult ensureFreePercent(const QString& targetDir,
                                const QStringList& nameFilters,
                                int minFreePercent,
                                int keepNewest)
{
    CleanupResult result;

    // 无清理对象（目录为空或未给文件名规则）时视为通过，不影响调用方写文件
    if (targetDir.isEmpty() || nameFilters.isEmpty()) {
        result.spaceOk = true;
        return result;
    }

    // 第一步：只查磁盘比例。已达标直接返回，绝大多数日志写入走这条快路，不扫描目录
    int freePercent = freePercentOf(targetDir);
    result.freePercent = freePercent;
    if (freePercent < 0 || freePercent >= minFreePercent) {
        // 磁盘信息不可用（网络盘/权限受限）时放行，由写文件自身的错误提示兜底
        result.spaceOk = true;
        return result;
    }

    QMutexLocker lock(&g_mutex);

    // 第二步：同一目录刚清理过仍不达标，间隔内直接放行，避免高频写入时重复扫描
    const QString key = QDir(targetDir).absolutePath();
    const qint64 now  = nowMsec();
    const auto it     = g_lastCleanupMsec.constFind(key);
    if (it != g_lastCleanupMsec.constEnd() && now - it.value() < SC::DISK_CLEANUP_INTERVAL_MS) {
        result.spaceOk = false;
        return result;
    }
    g_lastCleanupMsec[key] = now;

    // 第三步：按修改时间从旧到新排序（自行排序，不依赖 QDir 排序标志的默认方向）
    QDir dir(targetDir);
    QFileInfoList files = dir.entryInfoList(nameFilters, QDir::Files);
    std::sort(files.begin(), files.end(), [](const QFileInfo& left, const QFileInfo& right) {
        return left.lastModified() < right.lastModified();  // 最旧的排在最前
    });

    // 保留最新的 keepNewest 个，更旧的才允许删除
    const int keep    = qMax(0, keepNewest);
    const int deletable = files.size() - qMin(keep, files.size());

    for (int i = 0; i < deletable; ++i) {
        const QFileInfo& fileInfo = files.at(i);
        const qint64 fileSize     = fileInfo.size();

        // 文件被占用（如当天正在写的日志）时删除会失败，跳过继续处理更旧的文件
        if (!QFile::remove(fileInfo.absoluteFilePath())) continue;

        result.deletedFiles++;
        result.freedBytes += fileSize;

        // 每删一个重新查一次真实比例，达标即停
        freePercent = freePercentOf(targetDir);
        if (freePercent >= 0 && freePercent >= minFreePercent) break;
    }

    result.freePercent = freePercentOf(targetDir);
    result.spaceOk     = (result.freePercent < 0) || (result.freePercent >= minFreePercent);
    return result;
}

CleanupResult ensureFreePercentForFile(const QString& filePath,
                                       const QStringList& nameFilters,
                                       int minFreePercent,
                                       int keepNewest)
{
    if (filePath.isEmpty()) {
        CleanupResult result;
        result.spaceOk = true;
        return result;
    }
    return ensureFreePercent(QFileInfo(filePath).absolutePath(),
                             nameFilters, minFreePercent, keepNewest);
}

}  // namespace DiskGuard
