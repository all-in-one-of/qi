cmake_minimum_required(VERSION 3.5)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/../../cmake/")
include(ConfigureTarget)
include(LinkOpenCV)

project(QChecker)

set(QT_MIN_VERSION "5.6.1")

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 ${QT_MIN_VERSION} REQUIRED Widgets Gui)

add_subdirectory(ZGraphics)
add_subdirectory(BaseAlgrithm)
add_subdirectory(LEDAlgrithm)
add_subdirectory(LEDChecker)

ConfigureAll()