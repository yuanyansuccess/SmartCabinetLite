/**
 * @file DiskSpaceGuard.h
 * @brief 磁盘容量保护公用模块：写入文件前检查所在磁盘可用比例，不足时自动清理目标目录下的旧文件
 * @author 袁燕
 *
 * 适用场景（用户无感知，后台静默执行）：数据库备份、CSV/台账导出、日志写入、工具文档存放。
 * 只清理匹配指定文件名规则的本程序自有文件，且始终保留最新的若干个，不会误删用户数据。
 */
#pragma once

#include <QString>
#include <QStringList>

#include "Constants.h"

namespace DiskGuard {

/**
 * @brief 清理结果：供调用方写日志排障（界面不感知，不弹窗）
 */
struct CleanupResult {
    bool   spaceOk      = false;  ///< true=清理后可用比例已达标，或磁盘信息不可用而放行
    int    deletedFiles = 0;      ///< 本次删除的文件个数
    qint64 freedBytes   = 0;      ///< 本次释放的字节数
    int    freePercent  = -1;     ///< 清理后的可用百分比(0~100)，-1 表示磁盘信息不可用
};

/**
 * @brief 查询指定路径所在磁盘的可用空间百分比
 * @param path 磁盘上任意路径（文件可不存在，取其所在盘符/挂载点）
 * @return 可用字节数占总字节数的百分比(0~100)；磁盘信息不可用时返回 -1
 */
int freePercentOf(const QString& path);

/**
 * @brief 确保目标目录所在磁盘可用比例达到阈值，不足时静默清理旧文件后再放行
 *
 * 清理规则（按修改时间从旧到新逐个删除，删一个重新查一次真实比例）：
 *   1. 只删除匹配 nameFilters 通配符的文件，不删子目录，不递归；
 *   2. 始终保留最新的 keepNewest 个文件，防止把目录清空；
 *   3. 文件被其他程序占用导致删除失败时跳过该文件，继续处理更旧的；
 *   4. 可用比例已达标时只做一次磁盘查询，不扫描目录；
 *   5. 同一目录清理后仍不达标时，DISK_CLEANUP_INTERVAL_MS 间隔内不再重复扫描，
 *      避免日志高频写入时每行都做目录枚举。
 *
 * @param targetDir      待写入文件所在的目录（清理只动这个目录里的文件）
 * @param nameFilters    文件名通配符清单，限定只清理本程序产生的文件（如 "smartcabinet_backup_*.sql"）
 * @param minFreePercent 可用比例最低要求，默认取 SC::DISK_MIN_FREE_PERCENT
 * @param keepNewest     至少保留的最新文件个数，默认取 SC::DISK_KEEP_NEWEST_FILES
 * @return CleanupResult 清理结果，供调用方记日志；整个过程不弹窗、不阻塞界面
 */
CleanupResult ensureFreePercent(const QString& targetDir,
                                const QStringList& nameFilters,
                                int minFreePercent = SC::DISK_MIN_FREE_PERCENT,
                                int keepNewest    = SC::DISK_KEEP_NEWEST_FILES);

/**
 * @brief 按"即将写入的文件路径"执行容量保护（自动取其所在目录）
 * @param filePath       即将创建/写入的文件完整路径（写盘前调用）
 * @param nameFilters    文件名通配符清单，限定只清理本程序产生的文件
 * @param minFreePercent 可用比例最低要求，默认取 SC::DISK_MIN_FREE_PERCENT
 * @param keepNewest     至少保留的最新文件个数，默认取 SC::DISK_KEEP_NEWEST_FILES
 * @return CleanupResult 清理结果，供调用方记日志
 */
CleanupResult ensureFreePercentForFile(const QString& filePath,
                                       const QStringList& nameFilters,
                                       int minFreePercent = SC::DISK_MIN_FREE_PERCENT,
                                       int keepNewest    = SC::DISK_KEEP_NEWEST_FILES);

}  // namespace DiskGuard
