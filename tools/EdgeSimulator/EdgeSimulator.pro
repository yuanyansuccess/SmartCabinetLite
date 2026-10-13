# 边缘计算机模拟器（带界面）—— 模拟视觉边缘计算机与上位机联调
# 独立构建：qmake EdgeSimulator.pro && nmake release（不影响主工程）
QT      += core gui widgets network

CONFIG  += c++17

TARGET   = EdgeSimulator
TEMPLATE = app

SOURCES += main.cpp \
    EdgeSimulator.cpp

HEADERS += EdgeSimulator.h