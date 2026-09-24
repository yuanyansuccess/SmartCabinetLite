// 作者：袁燕  智能柜Qt Widget 2.0  程序入口
// 日期：2026-06-21 融合版：版本B页面(功能完整) + 版本A组件(TopBar/软键盘/摄像头)
#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QFile>
#include <QIcon>
#include <QStyleFactory>
#include "common/AppConfig.h"
#include "common/DatabaseManager.h"
#include "components/DeepFaceExtractor.h"
#include "MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("QtSmartCabinet");
    app.setApplicationVersion("2.0.0");
    app.setOrganizationName("SmartCabinet");

    // ── 全局字体（麒麟中文适配） ──
#ifdef Q_OS_LINUX
    QFont font("WenQuanYi Micro Hei", 10);
#else
    QFont font("Microsoft YaHei", 10);
#endif
    font.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(font);

    // ── Fusion样式（跨平台统一外观） ──
    app.setStyle(QStyleFactory::create("Fusion"));

    // ── 初始化数据库连接 ──
    // [2026-06-26] 策略：优先本地SQLite存储，远程MySQL为可选同步
    AppConfig& cfg = AppConfig::instance();
    DatabaseManager& db = DatabaseManager::instance();
    if (!db.initialize(cfg.dbHost(), cfg.dbPort(), cfg.dbName(), cfg.dbUser(), cfg.dbPass())) {
        qCritical("FATAL: Database initialization failed completely, exiting");
        return 1;
    }
    qInfo() << "Database initialized successfully, connected:" << db.isConnected();

    // ── 设置程序图标 ──
    // [v4.1] 窗口标题栏图标用公司logo，exe图标用专业工具图标(app.rc控制)
    app.setWindowIcon(QIcon(":/resources/logo.png"));

    // ── 加载全局QSS ──
    QFile qssFile(":/style/global.qss");
    if (qssFile.open(QFile::ReadOnly | QFile::Text)) {
        QString qss = qssFile.readAll();
        qssFile.close();
        // [2026-06-27] 强制去除所有表格单元格选中时的虚线焦点框
        // 追加在全局样式末尾，确保优先级最高
        qss += "\n/* 强制去除表格焦点框 */\n"
               "QTableWidget { outline: none; }\n"
               "QTableWidget::item { outline: none; border: none; }\n"
               "QTableWidget::item:focus { outline: none; border: none; }\n"
               "QTableWidget::item:selected { outline: none; }\n"
               "QListWidget { outline: none; }\n"
               "QListWidget::item { outline: none; }\n"
               "QTreeWidget { outline: none; }\n"
               "QTreeWidget::item { outline: none; }";
        app.setStyleSheet(qss);
    }

    // ── 人脸识别常驻服务：随主程序启动拉起，随主程序退出停止 ──
    // [2026-09-24] 袁燕：服务进程由应用托管，避免开机后服务未启动导致人脸功能全废
    DeepFaceExtractor::prestartAsync();
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &DeepFaceExtractor::shutdownServer);

    // ── 主窗口 ──
    // [v4.1] 触屏智能柜系统强制全屏模式运行
    MainWindow w;
    w.showFullScreen();

    return app.exec();
}
