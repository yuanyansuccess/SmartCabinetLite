# ═══════════════════════════════════════════
# 单元测试（Qt Test）
# ═══════════════════════════════════════════
# 新人使用方式：
#   1. 构建：cmake --build build --target test_components
#   2. 运行：./build/tests/test_components.exe（或 ctest --test-dir build）
#   3. VS 里直接对 test_components 右键"设为启动项目"F5 运行
# 说明：纯逻辑/UI断言，不连数据库，无需MySQL环境。

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

enable_testing()
add_test(NAME test_core_logic COMMAND test_core_logic)
