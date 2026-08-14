cmake_minimum_required(VERSION 3.7.0)

set(CMAKE_AUTOMOC ON)

project(fxpresser)

find_package(Qt5 COMPONENTS Core Gui Widgets REQUIRED)

add_executable(fxpresser WIN32 fxmainwindow.cpp main.cpp icon.rc)

# 发布版本不携带调试符号，减小体积并避免非标准发布特征触发启发式误报。
set_target_properties(fxpresser PROPERTIES LINK_FLAGS_RELEASE "-s")
target_compile_options(fxpresser PRIVATE $<$<CONFIG:Release>:-g0>)

target_link_libraries(fxpresser Qt5::Core Qt5::Gui Qt5::Widgets)
