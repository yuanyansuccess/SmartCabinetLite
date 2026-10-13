# 边缘计算机模拟器（带界面）—— 跨平台构建（Windows / 麒麟x86 / 麒麟ARM飞腾）
# 麒麟上编译：mkdir build && cd build && cmake .. && make -j4
cmake_minimum_required(VERSION 3.16)
project(EdgeSimulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt6 查找路径与主工程 CMakeLists.txt 保持一致
if(WIN32)
    set(QT_SEARCH_PATHS
        "C:/Qt/6.5.3/msvc2019_64"
        "D:/Qt/6.5.3/msvc2019_64"
        "E:/Qt/6.5.3/msvc2019_64"
    )
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm")
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
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

add_executable(EdgeSimulator
    main.cpp
    EdgeSimulator.cpp
    EdgeSimulator.h
)
target_link_libraries(EdgeSimulator PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network
)