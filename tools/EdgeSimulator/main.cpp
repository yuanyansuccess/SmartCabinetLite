/**
 * @file  main.cpp
 * @brief 边缘计算机模拟器入口（带界面）
 * @author 袁燕
 *
 * 用法：EdgeSimulator [--auto-connect] [--host IP] [--port N] [--tab N]
 *   --auto-connect  启动后自动连接（免点按钮，快速验证/自动化测试用）
 *   --tab N         启动后直接显示指定页签（0=周期帧 1=识别回应
 *                   2=版本/异常 3=自定义帧），用于截图与演示
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

    if (parser.isSet(QStringLiteral("tab"))) {
        w.showTab(parser.value(QStringLiteral("tab")).toInt());
    }
    if (parser.isSet(QStringLiteral("auto-connect"))) {
        QTimer::singleShot(300, &w, &EdgeSimulator::connectToEdge);
    }
    return app.exec();
}