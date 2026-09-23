# ============================================================================
# QtSmartCabinet.pro - Qt Creator 项目文件
# 作者: 袁燕  创建: 2026-06-15
# 说明: 此文件用于Qt Creator直接打开项目（Linux/Windows通用）
#       同时支持CMake构建系统
# ============================================================================

# 项目配置
QT += core widgets sql network
CONFIG += c++17
CONFIG += warn_on

# 目标名称
TARGET = QtSmartCabinet

# 源文件
SOURCES += \
    src/main.cpp \
    src/MainWindow.cpp \
    src/utils/StyleHelper.cpp \
    src/components/FaceCameraWidget.cpp \
    src/components/SoftKeyboard.cpp \
    src/components/CameraCapture.cpp \
    src/db/DatabaseManager.cpp \
    src/db/UserDAO.cpp \
    src/db/ToolDAO.cpp \
    src/db/RecordDAO.cpp \
    src/db/AlertDAO.cpp \
    src/db/TaskTypeDAO.cpp \
    src/services/AuthService.cpp \
    src/services/BorrowService.cpp \
    src/services/ReturnService.cpp \
    src/services/SettingService.cpp \
    src/services/FaceRecognitionService.cpp \
    src/pages/LoginPage.cpp \
    src/pages/DashboardPage.cpp \
    src/pages/UserManagementPage.cpp \
    src/pages/ToolManagementPage.cpp \
    src/pages/ToolBorrowPage.cpp \
    src/pages/ToolReturnPage.cpp \
    src/pages/ToolCheckinPage.cpp \
    src/pages/ToolCheckoutPage.cpp \
    src/pages/LedgerStatsPage.cpp \
    src/pages/AlertLogsPage.cpp \
    src/pages/SystemSettingsPage.cpp

# 头文件
HEADERS += \
    src/MainWindow.h \
    src/utils/StyleHelper.h \
    src/components/FaceCameraWidget.h \
    src/components/CameraCapture.h \
    src/components/SoftKeyboard.h \
    src/db/DatabaseManager.h \
    src/db/UserDAO.h \
    src/db/ToolDAO.h \
    src/db/RecordDAO.h \
    src/db/AlertDAO.h \
    src/db/TaskTypeDAO.h \
    src/services/AuthService.h \
    src/services/BorrowService.h \
    src/services/ReturnService.h \
    src/services/SettingService.h \
    src/services/FaceRecognitionService.h \
    src/pages/LoginPage.h \
    src/pages/DashboardPage.h \
    src/pages/UserManagementPage.h \
    src/pages/ToolManagementPage.h \
    src/pages/ToolBorrowPage.h \
    src/pages/ToolReturnPage.h \
    src/pages/ToolCheckinPage.h \
    src/pages/ToolCheckoutPage.h \
    src/pages/LedgerStatsPage.h \
    src/pages/AlertLogsPage.h \
    src/pages/SystemSettingsPage.h

# Qt资源文件
RESOURCES += \
    resources/resources.qrc

# 包含路径
INCLUDEPATH += \
    $$PWD/src \
    $$PWD/src/utils \
    $$PWD/src/components \
    $$PWD/src/db \
    $$PWD/src/services \
    $$PWD/src/pages

# Windows特定配置
win32 {
    # 使用MinGW/gcc编译
    message("Using MinGW/gcc - camera simulation mode")
    # 禁用Media Foundation（MinGW不支持）
    DEFINES += CAMERA_SIMULATION_MODE
    # 不链接Windows特定库
    # LIBS -= -lmfplat -lmfreadwrite -lshlwapi -luuid
    # 禁用控制台窗口
    CONFIG -= console
}

# Linux特定配置
unix:!macx {
    # Qt Multimedia (摄像头)
    QT += multimedia multimediawidgets
    # 链接pthread
    LIBS += -lpthread
}

# 调试/发布配置
debug {
    DEFINES += DEBUG_MODE
}

release {
    DEFINES += QT_NO_DEBUG_OUTPUT
}

# 安装规则 (Linux)
unix:!macx {
    target.path = /opt/QtSmartCabinet
    INSTALLS += target
}
