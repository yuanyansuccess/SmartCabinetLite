/**
 * @file Logger.cpp
 * @brief 轻量统一日志框架实现
 * @author 袁燕
 */
#include "Logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTextStream>
#include <QtGlobal>
#include <QtLogging>

namespace Log {
namespace {

QMutex g_mutex;
Level  g_level           = Level::Debug;
bool   g_console         = true;
bool   g_sensitive       = false;   // 默认关闭敏感判定日志
bool   g_installed       = false;
QString g_dir;

// 敏感判定关键词：命中且未显式开启时丢弃（Release 环境默认不外泄判定细节）
const QStringList& sensitiveKeywords() {
    static const QStringList kws = {
        QStringLiteral("similarity"), QStringLiteral("相似度"),
        QStringLiteral("threshold"),  QStringLiteral("阈值"),
        QStringLiteral("best.sim"),   QStringLiteral("rejectThreshold"),
        QStringLiteral("descriptor"),  QStringLiteral("faceFeature"),
        QStringLiteral("password"),    QStringLiteral("口令"),
    };
    return kws;
}

const char* levelTag(QtMsgType t) {
    switch (t) {
        case QtDebugMsg:    return "DEBUG";
        case QtInfoMsg:     return "INFO ";
        case QtWarningMsg:  return "WARN ";
        case QtCriticalMsg: return "ERROR";
        case QtFatalMsg:    return "FATAL";
    }
    return "?????";
}

QtMsgType minTypeOf(Level l) {
    switch (l) {
        case Level::Debug:    return QtDebugMsg;
        case Level::Info:     return QtInfoMsg;
        case Level::Warning:  return QtWarningMsg;
        case Level::Critical: return QtCriticalMsg;
        case Level::Off:      return QtFatalMsg;  // 只留致命
    }
    return QtDebugMsg;
}

/// 提取已有约定中的分类前缀，例如 "[DB] xxx" -> "DB"
QString categoryOf(const QString& msg) {
    if (msg.startsWith(QLatin1Char('['))) {
        const int end = msg.indexOf(QLatin1Char(']'));
        if (end > 1 && end <= 24) return msg.mid(1, end - 1);
    }
    return QStringLiteral("APP");
}

QString ensureDir() {
    if (!g_dir.isEmpty()) return g_dir;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    g_dir = env.value(QStringLiteral("SC_LOG_DIR"));
    if (g_dir.isEmpty()) {
        g_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                + QStringLiteral("/logs");
    }
    QDir().mkpath(g_dir);
    return g_dir;
}

void writeLine(const QString& line) {
    const QString path = ensureDir() + QStringLiteral("/app_")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd"))
        + QStringLiteral(".log");
    QFile f(path);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream ts(&f);
        ts.setEncoding(QStringConverter::Utf8);
        ts << line << '\n';
        f.close();
    }
}

void handler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    // 级别过滤
    if (type < minTypeOf(g_level)) return;

    // 敏感过滤（相似度/阈值/特征/口令等判定细节）
    if (!g_sensitive && isSensitiveMessage(msg)) return;

    // 许多既有日志自带 [分类] 前缀（如 "[DB] ..."），此处避免重复打印分类
    const QString cat = categoryOf(msg);
    QString body = msg;
    if (cat != QStringLiteral("APP")) {
        const int cut = body.indexOf(QLatin1Char(']'));
        if (cut >= 0) body = body.mid(cut + 1).trimmed();
    }

    const QString line = QStringLiteral("%1 [%2] [%3] %4")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
             QString::fromLatin1(levelTag(type)),
             cat, body);

    QMutexLocker lock(&g_mutex);
    writeLine(line);
    if (g_console) {
        QTextStream ts(stderr);
        ts.setEncoding(QStringConverter::Utf8);
        ts << line << '\n';
    }
}

}  // namespace

void install() {
    QMutexLocker lock(&g_mutex);
    if (g_installed) return;

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString lv = env.value(QStringLiteral("SC_LOG_LEVEL"), QStringLiteral("debug")).toLower();
    if (lv == QStringLiteral("info"))          g_level = Level::Info;
    else if (lv == QStringLiteral("warning"))  g_level = Level::Warning;
    else if (lv == QStringLiteral("critical")) g_level = Level::Critical;
    else if (lv == QStringLiteral("off"))      g_level = Level::Off;
    else                                       g_level = Level::Debug;

    g_console   = env.value(QStringLiteral("SC_LOG_CONSOLE"), QStringLiteral("1")) != QStringLiteral("0");
    g_sensitive = env.value(QStringLiteral("SC_LOG_SENSITIVE"), QStringLiteral("0")) == QStringLiteral("1");

    ensureDir();
    qInstallMessageHandler(handler);
    g_installed = true;
}

Level level() { return g_level; }

void setLevel(Level l) {
    QMutexLocker lock(&g_mutex);
    g_level = l;
}

bool sensitiveEnabled() { return g_sensitive; }

void setSensitiveEnabled(bool on) {
    QMutexLocker lock(&g_mutex);
    g_sensitive = on;
}

QString logDirectory() { return ensureDir(); }

bool isSensitiveMessage(const QString& msg) {
    for (const QString& kw : sensitiveKeywords()) {
        if (msg.contains(kw, Qt::CaseInsensitive)) return true;
    }
    return false;
}

}  // namespace Log
