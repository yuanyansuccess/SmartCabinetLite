/**
 * @file main.cpp
 * @brief 程序入口：装配配置、数据库、日志与人脸识别服务，创建主窗口并进入事件循环
 * @author 袁燕
 */
#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QFile>
#include <QIcon>
#include <QStyleFactory>
#include "common/AppConfig.h"
#include "common/DatabaseManager.h"
#include "common/Logger.h"
#include "components/DeepFaceExtractor.h"
#include "comm/EdgeService.h"
#include "utils/DeviceProfile.h"
#include "MainWindow.h"

int main(int argc, char* argv[]) {
    // ── 全局 UI 等比缩放（必须在 QApplication 构造之前设置）──
    // 以 1024x768 为设计基准，按主屏逻辑分辨率自动等比缩放；
    // 支持 system.ini [UI] uiScale 手动覆盖。设备识别详情见 logDeviceInfo()。
    DeviceProfile::applyScale(argc, argv);

    QApplication app(argc, argv);

    // 应用标识必须先设置：Log::install() 内部按 org/app 拼日志目录，
    // 若在安装消息处理器之后才设置，日志目录会缺少 org/app 两层、位置飘忽
    app.setApplicationName("QtSmartCabinet");
    app.setApplicationVersion("2.0.0");
    app.setOrganizationName("SmartCabinet");

    // 统一日志：安装后所有 qDebug/qInfo/qWarning/qCritical 均按级别落盘
    // （级别/控制台/敏感日志由 SC_LOG_LEVEL、SC_LOG_CONSOLE、SC_LOG_SENSITIVE 控制）
    Log::install();

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
    // 纯 MySQL 架构：连接参数取自本地配置，连接失败即无法继续启动
    AppConfig& cfg = AppConfig::instance();
    DatabaseManager& db = DatabaseManager::instance();
    if (!db.initialize(cfg.dbHost(), cfg.dbPort(), cfg.dbName(), cfg.dbUser(), cfg.dbPass())) {
        qCritical("FATAL: Database initialization failed completely, exiting");
        return 1;
    }
    qInfo() << "Database initialized successfully, connected:" << db.isConnected();

    // ── 设置程序图标 ──
    // 窗口标题栏图标用公司logo，exe图标用专业工具图标(app.rc控制)
    app.setWindowIcon(QIcon(":/resources/logo.png"));

    // ── 加载全局QSS ──
    QFile qssFile(":/style/global.qss");
    if (qssFile.open(QFile::ReadOnly | QFile::Text)) {
        QString qss = qssFile.readAll();
        qssFile.close();
        // 强制去除所有表格单元格选中时的虚线焦点框
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
    // 袁燕：服务进程由应用托管，避免开机后服务未启动导致人脸功能全废
    DeepFaceExtractor::prestartAsync();
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &DeepFaceExtractor::shutdownServer);

    // ── 与边缘计算机通信接口：随主程序启动拉起，监听9000等边缘机接入 ──
    // 当前只做链路接入（上线/周期帧/离线判定）；识别请求由后续会话业务流程发起
    EdgeService* edgeService = new EdgeService(&app);
    edgeService->start(EdgeConfig());   // 缺省配置：端口9000，日志自动进 logs/app_日期.log
    QObject::connect(edgeService, &EdgeService::onlineChanged, [](bool on) {
        qInfo() << "[EdgeService] 边缘机链路" << (on ? "在线" : "离线");
    });
    QObject::connect(edgeService, &EdgeService::periodicUpdated, [](const PeriodicInfo& info) {
        qInfo() << "[EdgeService] 收到周期帧，运行" << info.uptimeSec
                << "秒，摄像头" << info.cameras.size() << "个";
    });
    QObject::connect(edgeService, &EdgeService::recognizeFinished,
                     [](const QString& id, const RecognizeResult& r) {
        qInfo() << "[EdgeService] 识别完成" << id << "共" << r.items.size() << "项";
    });
    QObject::connect(edgeService, &EdgeService::recognizeFailed,
                     [](const QString& id, const QString& why) {
        qWarning() << "[EdgeService] 识别失败" << id << why;
    });
    QObject::connect(edgeService, &EdgeService::versionReceived, [](const QString& v) {
        qInfo() << "[EdgeService] 边缘机软件版本" << v;
    });

    // ── 主窗口 ──
    // 触屏智能柜系统强制全屏模式运行
    MainWindow w;
    DeviceProfile::logDeviceInfo();   // 主屏已就绪，打印设备识别与缩放信息
    w.showFullScreen();

    return app.exec();
}
