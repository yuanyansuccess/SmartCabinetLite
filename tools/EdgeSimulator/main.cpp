/**
 * @file  main.cpp
 * @brief 边缘计算机模拟器入口（带界面）
 * @author 袁燕
 *
 * 用法：EdgeSimulator [--auto-connect] [--host IP] [--port N] [--tab N]
 *   --auto-connect  启动后自动连接（免点按钮，快速验证/自动化测试用）
 *   --tab N         启动后直接显示指定步骤页（0=连接与数据 1=单据模拟
 *                   2=识别回应 3=异常注入 4=场景预设），用于截图与演示
 */
#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>
#include "EdgeSimulator.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("EdgeSimulator"));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("auto-connect"), QStringLiteral("启动后自动连接上位机")});
    parser.addOptions({
        {{QStringLiteral("H"), QStringLiteral("host")},
         QStringLiteral("上位机IP"), QStringLiteral("host"), QStringLiteral("127.0.0.1")},
        {{QStringLiteral("p"), QStringLiteral("port")},
         QStringLiteral("上位机端口"), QStringLiteral("port"), QStringLiteral("9000")},
        {{QStringLiteral("t"), QStringLiteral("tab")},
         QStringLiteral("启动页签序号(0~3)"), QStringLiteral("tab"), QStringLiteral("0")},
    });
    parser.process(app);

    EdgeSimulator w;
    w.show();

    // 命令行指定的上位机地址优先于配置文件里的值（不指定就用 ini/界面上的）
    const QString cliHost = parser.isSet(QStringLiteral("host")) ? parser.value(QStringLiteral("host")) : QString();
    const int cliPort = parser.isSet(QStringLiteral("port")) ? parser.value(QStringLiteral("port")).toInt() : 0;
    if (!cliHost.isEmpty() || cliPort > 0) {
        w.setPeerAddress(cliHost, cliPort);
    }
    if (parser.isSet(QStringLiteral("tab"))) {
        w.showTab(parser.value(QStringLiteral("tab")).toInt());
    }
    if (parser.isSet(QStringLiteral("auto-connect"))) {
        QTimer::singleShot(300, &w, &EdgeSimulator::connectToEdge);
    }
    return app.exec();
}
