cmake_minimum_required(VERSION 3.20)

project(TaskAutomation VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui DBus)

add_executable(taskautomation
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/MacroEvent.cpp
    src/MacroEvent.h
    src/MacroFile.cpp
    src/MacroFile.h
    src/EvdevRecorder.cpp
    src/EvdevRecorder.h
    src/UInputDevice.cpp
    src/UInputDevice.h
    src/MacroPlayer.cpp
    src/MacroPlayer.h
    src/KWinCursorBridge.cpp
    src/KWinCursorBridge.h
    resources/resources.qrc
)

target_link_libraries(taskautomation PRIVATE Qt6::Widgets Qt6::Core Qt6::Gui Qt6::DBus)

target_compile_definitions(taskautomation PRIVATE TASKAUTOMATION_VERSION="${PROJECT_VERSION}")

target_compile_options(taskautomation PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS taskautomation RUNTIME DESTINATION bin)
