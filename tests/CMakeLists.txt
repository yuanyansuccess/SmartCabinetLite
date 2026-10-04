# ═══════════════════════════════════════════
# 单元测试（Qt Test）
# ═══════════════════════════════════════════
# 新人使用方式：
#   1. 构建：cmake --build build --target test_components
#   2. 运行：./build/tests/test_components.exe（或 ctest --test-dir build）
#   3. VS 里直接对 test_components 右键"设为启动项目"F5 运行
# 说明：本目录下的测试分两类
#   - test_components / test_core_logic：纯逻辑与UI断言，不连数据库
#   - test_services：Service 层业务规则，连 MySQL 测试库（见同目录下
#     prepare_test_db.ps1 与 test_services.cpp 头部的安全说明）
#     ★ 未配置环境变量时其数据库用例自动 SKIP，不会失败，
#       也不会以任何方式连接生产库。

find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(test_components
    test_components.cpp
    ${CMAKE_SOURCE_DIR}/src/components/PaginationBar.cpp
    ${CMAKE_SOURCE_DIR}/src/components/SearchBar.cpp
)

target_include_directories(test_components PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_components PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test
)

# 核心纯逻辑测试（位置格式化/设计令牌/表单工厂/配置注入），不连数据库
# 说明：AppConfig/SettingController 用例需要真实符号才能链接，故引入 COMMON_SOURCES
# （AppConfig/DatabaseManager/Logger，DatabaseManager 仅被 SettingController 引用，测试内不会连库）
# 与 SettingController.cpp。路径转换规则同 test_services。
set(TEST_CORE_SRCS)
foreach(_f IN LISTS COMMON_SOURCES)
    list(APPEND TEST_CORE_SRCS "${CMAKE_SOURCE_DIR}/${_f}")
endforeach()

add_executable(test_core_logic
    test_core_logic.cpp
    ${TEST_CORE_SRCS}
    ${CMAKE_SOURCE_DIR}/src/controller/SettingController.cpp
)

target_include_directories(test_core_logic PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_core_logic PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test Qt6::Sql Qt6::Network
)

# ═══════════════════════════════════════════
# Service 层业务规则测试（连接 MySQL 测试库）
# ═══════════════════════════════════════════
# 新人使用方式：
#   1. 准备测试库：库名必须以 _test 或 _unittest 结尾（安全闸门，防误连生产库）
#   2. 设置环境变量：SC_TEST_DB_HOST/PORT/NAME/USER/PASS
#   3. 运行：build\tests\Debug\test_services.exe
# 说明：未配置环境变量时，数据库用例自动 SKIP，纯逻辑用例仍全部执行。
# 上层变量(COMMON_SOURCES/DB_SOURCES/SERVICE_SOURCES)是相对 src/ 的路径，
# 在子目录里解析基准为 tests/，需统一转换为基于 CMAKE_SOURCE_DIR 的绝对路径。
set(TEST_SERVICES_SRCS)
foreach(_f IN LISTS COMMON_SOURCES DB_SOURCES SERVICE_SOURCES)
    list(APPEND TEST_SERVICES_SRCS "${CMAKE_SOURCE_DIR}/${_f}")
endforeach()

add_executable(test_services
    test_services.cpp
    ${TEST_SERVICES_SRCS}
)

target_include_directories(test_services PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_services PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test Qt6::Sql Qt6::Network
)

# Qt SQL 驱动插件(qsqlodbcd.dll)是运行时按需加载的，测试 exe 位于 tests/ 子目录，
# Qt 默认不会在该目录找到 sqldrivers。插件已由主程序的 windeployqt 部署到
# build/<配置>/sqldrivers，测试运行时通过 QT_PLUGIN_PATH 指向该目录即可，
# 不再逐个复制（多项目并行复制到同一目录会产生竞争导致构建失败）。

# ═══════════════════════════════════════════
# DAO 层测试（BaseDAO 类型映射 + 位置维度/机组隔离铁律）
# ═══════════════════════════════════════════
# 纯逻辑用例走 SQLite 内存库（Qt 自带 QSQLITE），无需任何外部数据库；
# 数据库用例复用与 test_services 相同的三重安全闸门（环境变量 + 库名白名单 + SKIP）。
set(TEST_DAO_SRCS)
foreach(_f IN LISTS COMMON_SOURCES DB_SOURCES)
    list(APPEND TEST_DAO_SRCS "${CMAKE_SOURCE_DIR}/${_f}")
endforeach()

add_executable(test_dao
    test_dao.cpp
    ${TEST_DAO_SRCS}
)

target_include_directories(test_dao PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_dao PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test Qt6::Sql Qt6::Network
)

enable_testing()
add_test(NAME test_components COMMAND test_components)
add_test(NAME test_core_logic COMMAND test_core_logic)
add_test(NAME test_services COMMAND test_services)
add_test(NAME test_dao COMMAND test_dao)

# 统一为四个测试指定 Qt 运行时与插件目录：
#   QT_PLUGIN_PATH —— 让 QSqlDatabase 能找到 sqldrivers 下的驱动插件
#   PATH           —— 让测试 exe 能找到 Qt6*.dll（测试目录不部署 Qt 运行时）
# 用 path_list_prepend 追加而非覆盖，保留调用者原有环境变量。
set_tests_properties(test_components test_core_logic test_services test_dao PROPERTIES
    ENVIRONMENT "QT_PLUGIN_PATH=${CMAKE_BINARY_DIR}/$<CONFIG>"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${CMAKE_BINARY_DIR}/$<CONFIG>"
)

# 测试 exe 依赖 Qt Test 模块，主程序不链接该模块，因此主程序目录里没有 Qt6Test.dll。
# 四个测试 exe 位于同一输出目录，只需部署一次即可共享。
if(WIN32)
    find_program(TEST_WINDEPLOYQT windeployqt HINTS ${CMAKE_PREFIX_PATH}/bin)
    if(TEST_WINDEPLOYQT)
        add_custom_command(TARGET test_components POST_BUILD
            COMMAND ${TEST_WINDEPLOYQT} --no-translations --no-opengl-sw
                    --dir "$<TARGET_FILE_DIR:test_components>" $<TARGET_FILE:test_components>
            COMMENT "Deploying Qt runtime for tests"
        )
    endif()
endif()
