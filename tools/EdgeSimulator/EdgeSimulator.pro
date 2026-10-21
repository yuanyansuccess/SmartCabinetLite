# 边缘计算机模拟器（带界面）—— 模拟视觉边缘计算机与上位机联调
# 独立构建：qmake EdgeSimulator.pro && nmake release（不影响主工程）
QT      += core gui widgets network

CONFIG  += c++17

TARGET   = EdgeSimulator
TEMPLATE = app

SOURCES += main.cpp \
    EdgeSimulator.cpp

HEADERS += EdgeSimulator.h

# 构建与依赖部署请用同目录的 build_release.bat（qmake+nmake+windeployqt 一条龙），
# 直接 nmake release 只会产出 exe，缺 Qt6*.dll 与 platforms 插件，双击会报找不到 Qt6Widgets.dll。