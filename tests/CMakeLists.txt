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

# 核心纯逻辑测试（位置格式化/设计令牌/表单工厂），不连数据库
add_executable(test_core_logic
    test_core_logic.cpp
)

target_include_directories(test_core_logic PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(test_core_logic PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test
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

enable_testing()
add_test(NAME test_core_logic COMMAND test_core_logic)
add_test(NAME test_services COMMAND test_services)
