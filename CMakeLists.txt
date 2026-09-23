cmake_minimum_required(VERSION 3.16)
project(QtSmartCabinet VERSION 2.0.0 LANGUAGES CXX)
# 作者：袁燕  智能柜Qt Widget 2.0 融合版
# 日期：2026-06-21  功能：版本B页面(功能完整)+版本A组件(TopBar/软键盘/摄像头)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ── 交叉编译检测 ──
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm")
    set(KYLIN_ARM TRUE)
    message(STATUS "Kylin ARM64 cross-compile mode")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    if(CMAKE_SYSTEM_NAME MATCHES "Linux")
        set(KYLIN_X86 TRUE)
        message(STATUS "Kylin x86_64 compile mode")
    else()
        message(STATUS "Windows MSVC compile mode")
    endif()
endif()

# ── Qt6 路径查找 ──
if(WIN32)
    set(QT_SEARCH_PATHS
        "C:/Qt/6.5.3/msvc2019_64"
        "D:/Qt/6.5.3/msvc2019_64"
        "E:/Qt/6.5.3/msvc2019_64"
    )
elseif(KYLIN_ARM)
    set(QT_SEARCH_PATHS "/opt/Qt/6.5.3/gcc_arm64")
else()
    set(QT_SEARCH_PATHS "/opt/Qt/6.5.3/gcc_64")
endif()

foreach(QT_PATH ${QT_SEARCH_PATHS})
    if(EXISTS "${QT_PATH}/lib/cmake/Qt6/Qt6Config.cmake")
        set(CMAKE_PREFIX_PATH "${QT_PATH}")
        message(STATUS "Qt6 found: ${QT_PATH}")
        break()
    endif()
endforeach()

if(NOT CMAKE_PREFIX_PATH)
    message(FATAL_ERROR "Qt6 not found in: ${QT_SEARCH_PATHS}")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Sql Network)

# ── 平台特定配置 ──
# [2026-06-21] 麒麟适配：Linux下启用Qt Multimedia摄像头支持
if(KYLIN_ARM OR KYLIN_X86)
    find_package(Qt6 COMPONENTS Multimedia MultimediaWidgets QUIET)
    if(Qt6Multimedia_FOUND AND Qt6MultimediaWidgets_FOUND)
        add_compile_definitions(HAS_QT_MULTIMEDIA)
        message(STATUS "Linux/Kylin: Qt Multimedia camera enabled")
    else()
        message(WARNING "Linux/Kylin: Qt Multimedia not found, camera disabled")
    endif()
    # 麒麟兼容宏
    add_compile_definitions(LINUX_SYSTEM KYLIN_COMPATIBLE)
    # ARM64 编译优化
    if(KYLIN_ARM)
        add_compile_options(-march=armv8-a)
        message(STATUS "Kylin ARM64: march=armv8-a optimization enabled")
    endif()
endif()

# ── MySQL 客户端库 ──
if(WIN32)
    set(MYSQL_LIBS libmysql)
    find_path(MYSQL_INCLUDE_DIR mysql.h PATH_SUFFIXES mysql)
    # [V2.02 2026-06-28] 添加/FS标志解决MSVC多进程编译PDB争用(C1041)
    add_compile_options(/FS)
else()
    find_library(MYSQL_CLIENT_LIB mysqlclient)
    if(MYSQL_CLIENT_LIB)
        set(MYSQL_LIBS ${MYSQL_CLIENT_LIB})
        message(STATUS "libmysqlclient found")
    else()
        message(WARNING "libmysqlclient not found, will use Qt SQL driver only")
        set(MYSQL_LIBS "")
    endif()
endif()

# ═══════════════════════════════════════════
# 源文件配置 (版本B pages/services/db + 版本A components)
# ═══════════════════════════════════════════

# Common层 (共享)
set(COMMON_SOURCES
    src/common/AppConfig.cpp
    src/common/DatabaseManager.cpp
)

# Model层 (纯头文件, 无cpp)
set(MODEL_SOURCES)

# 数据层 (db/ — V6.9统一namespace db，合并原dao/实体类API)
set(DB_SOURCES
    src/db/UserDAO.cpp
    src/db/FaceRecogLogDAO.cpp
    src/db/ToolDAO.cpp
    src/db/RecordDAO.cpp
    src/db/AlertDAO.cpp
    src/db/TaskTypeDAO.cpp
    src/db/DepartmentDAO.cpp
)

# [V6.9 2026-06-24] dao/层已全部合并到db/，此变量保留为空以兼容
set(DAO_SOURCES)

# [V1.00.9.1 架构修复] 添加controller层（统一通过controller访问业务逻辑） —— 作者：袁燕
set(CONTROLLER_SOURCES
    src/controller/AlertController.cpp
    src/controller/AuthController.cpp
    src/controller/BorrowController.cpp
    src/controller/ReturnController.cpp
    src/controller/SettingController.cpp
    src/controller/ToolController.cpp
    src/controller/UserController.cpp
)

# 服务层 (版本B)
set(SERVICE_SOURCES
    src/services/AuthService.cpp
    src/services/BorrowService.cpp
    src/services/ToolService.cpp
    src/services/ReturnService.cpp
    src/services/SettingService.cpp
    src/services/FaceRecognitionService.cpp
)

# 页面层 (版本B的pages/)
set(PAGE_SOURCES
    src/pages/LoginPage.cpp
    src/pages/DashboardPage.cpp
    src/pages/UserManagementPage.cpp
    src/pages/BatchImportDialog.cpp
    src/pages/FaceEnrollPage.cpp
    src/pages/ToolManagementPage.cpp
    src/pages/ToolBorrowPage.cpp
    src/pages/ToolReturnPage.cpp
    src/pages/ToolCheckinPage.cpp
    src/pages/ToolCheckoutPage.cpp
    src/pages/LedgerStatsPage.cpp
    src/pages/AlertLogsPage.cpp
    src/pages/SystemSettingsPage.cpp
    src/pages/SystemMaintenancePage.cpp
)

# 工具类 (版本B)
set(UTILS_SOURCES
    src/utils/StyleHelper.cpp
)

# 组件层 (版本A的触屏组件)
set(COMPONENT_SOURCES
    src/components/SoftKeyboard.cpp
    src/components/NumKeypad.cpp
    src/components/FaceCameraWidget.cpp
    src/components/DeepFaceExtractor.cpp
    src/components/CameraCapture.cpp
    src/components/SidebarMenu.cpp
    src/components/TopBar.cpp
    src/components/MultiSelectFilter.cpp
    src/components/SingleSelectFilter.cpp
    src/components/MessageDialog.cpp
    src/components/BaseDialog.cpp
)

# 主窗口 (版本B, src/根目录)
set(MAINWINDOW_SOURCE
    src/MainWindow.cpp
)

set(ALL_SOURCES
    src/main.cpp
    ${COMMON_SOURCES}
    ${DB_SOURCES}
    ${DAO_SOURCES}
    ${CONTROLLER_SOURCES}
    ${SERVICE_SOURCES}
    ${PAGE_SOURCES}
    ${UTILS_SOURCES}
    ${COMPONENT_SOURCES}
    ${MAINWINDOW_SOURCE}
    resources/resources.qrc
)

# [v4.1] Windows程序图标资源文件
if(WIN32)
    list(APPEND ALL_SOURCES resources/app.rc)
endif()

# ═══════════════════════════════════════════
# 头文件 (用于MOC)
# ═══════════════════════════════════════════
set(ALL_HEADERS
    # Common
    src/common/AppConfig.h
    src/common/DatabaseManager.h
    src/common/Constants.h
    src/common/PositionFormatter.h
    # Model (共享)
    src/model/User.h
    src/model/Department.h
    src/model/ToolInfo.h
    src/model/ToolCategory.h
    src/model/ToolCabinet.h
    src/model/BorrowRecord.h
    src/model/ReturnRecord.h
    src/model/AlertLog.h
    src/model/OperationLog.h
    src/model/TaskType.h
    src/model/Stranger.h
    src/model/BorrowDetail.h
    src/model/MachineGroup.h
    # DB (V6.9统一namespace db，合并QJsonObject + 实体类API)
    src/db/BaseDAO.h
    src/db/UserDAO.h
    src/db/FaceRecogLogDAO.h
    src/db/ToolDAO.h
    src/db/RecordDAO.h
    src/db/AlertDAO.h
    src/db/TaskTypeDAO.h
    src/db/DepartmentDAO.h
    # Controller (版本A)
    src/controller/AlertController.h
    src/controller/AuthController.h
    src/controller/BorrowController.h
    src/controller/ReturnController.h
    src/controller/SettingController.h
    src/controller/ToolController.h
    src/controller/UserController.h
    # Services (版本B)
    src/services/AuthService.h
    src/services/BorrowService.h
    src/services/ToolService.h
    src/services/ReturnService.h
    src/services/SettingService.h
    src/services/FaceRecognitionService.h
    # Pages (版本B)
    src/pages/LoginPage.h
    src/pages/DashboardPage.h
    src/pages/UserManagementPage.h
    src/pages/BatchImportDialog.h      # [2026-06-26] 批量导入用户对话框
    src/pages/ToolManagementPage.h
    src/pages/ToolBorrowPage.h
    src/pages/ToolReturnPage.h
    src/pages/ToolCheckinPage.h
    src/pages/ToolCheckoutPage.h
    src/pages/LedgerStatsPage.h
    src/pages/AlertLogsPage.h
    src/pages/SystemSettingsPage.h
    src/pages/SystemMaintenancePage.h
    src/pages/FaceEnrollPage.h
    # Utils (版本B)
    src/utils/StyleHelper.h
    # Components (版本A)
    src/components/SoftKeyboard.h
    src/components/NumKeypad.h        # [2026-06-26] 独立数字软键盘
    src/components/FaceCameraWidget.h
    src/components/DeepFaceExtractor.h  # [V8.0] 深度学习人脸特征提取
    src/components/TopBar.h
    src/components/MultiSelectFilter.h
    src/components/SingleSelectFilter.h
    src/components/MessageDialog.h
    src/components/BaseDialog.h
    src/components/SidebarMenu.h
    src/components/CameraCapture.h
    # MainWindow (版本B)
    src/MainWindow.h
)

# ═══════════════════════════════════════════
# include目录
# ═══════════════════════════════════════════
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common   # DatabaseManager优先使用common版本
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model
    ${CMAKE_CURRENT_SOURCE_DIR}/src/db       # [V6.9] 统一DAO层 namespace db::
    ${CMAKE_CURRENT_SOURCE_DIR}/src/controller  # 版本A控制器
    ${CMAKE_CURRENT_SOURCE_DIR}/src/services
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pages
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/src/components
)

# ═══════════════════════════════════════════
# [V5.4修复] VS解决方案资源管理器文件夹分组 —— 作者：袁燕
# 使用source_group TREE让VS按目录树显示源文件，消除同名文件(如两个AlertDAO.cpp)显示混淆
# db/DatabaseManager.cpp/.h已删除（未参与编译的废弃文件）
# ═══════════════════════════════════════════
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Source" FILES ${ALL_SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Header" FILES ${ALL_HEADERS})

# ═══════════════════════════════════════════
# 可执行文件
# ═══════════════════════════════════════════
# [2026-06-21] 麒麟适配：WIN32标志仅在Windows下使用，Linux下移除
if(WIN32)
    add_executable(QtSmartCabinet WIN32 ${ALL_SOURCES} ${ALL_HEADERS})
else()
    add_executable(QtSmartCabinet ${ALL_SOURCES} ${ALL_HEADERS})
endif()

target_link_libraries(QtSmartCabinet PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Sql Qt6::Network
)

# ── Windows 摄像头/MySQL ──
if(WIN32)
    set(WIN_LIBS mfplat mf mfreadwrite shlwapi uuid)
    target_link_libraries(QtSmartCabinet PRIVATE ${WIN_LIBS})
    if(MSVC)
        set_target_properties(QtSmartCabinet PROPERTIES
            WIN32_EXECUTABLE TRUE
        )
    endif()
    # windeployqt POST_BUILD
    find_program(WINDEPLOYQT windeployqt HINTS ${CMAKE_PREFIX_PATH}/bin)
    if(WINDEPLOYQT)
        add_custom_command(TARGET QtSmartCabinet POST_BUILD
            COMMAND ${WINDEPLOYQT} --no-translations $<TARGET_FILE:QtSmartCabinet>
            COMMENT "Deploying Qt runtime libraries..."
        )
    endif()
endif()

# ── VS启动项目设置 ──
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT QtSmartCabinet)

# ── 安装规则 ──
install(TARGETS QtSmartCabinet RUNTIME DESTINATION bin)
install(DIRECTORY resources/ DESTINATION share/QtSmartCabinet/resources OPTIONAL)
install(FILES src/style/global.qss DESTINATION share/QtSmartCabinet/style OPTIONAL)

message(STATUS "QtSmartCabinet 2.0 Fusion configuration complete")
