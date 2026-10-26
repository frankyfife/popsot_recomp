# POPSoT Launcher (popsot-launcher.exe): Qt 6 Widgets, 64 bit, MSVC. It sits
# beside sot-xbox-retail.exe in bin\, installs the game from the player's
# discs and edits popsot-options.ini. Build: tools/build-launcher.ps1.
cmake_minimum_required(VERSION 3.21)
project(POPSoTLauncher VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
qt_standard_project_setup()

# The icon: the PC version's (bin/pop.ico, written by tools/extract-icon.py
# from the player's POP.EXE, not kept in the repository), else the neutral one.
set(app_icon "${CMAKE_CURRENT_SOURCE_DIR}/../port/windows/res/app.ico")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../bin/pop.ico")
    set(app_icon "${CMAKE_CURRENT_SOURCE_DIR}/../bin/pop.ico")
endif()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/launcher.rc" CONTENT "1 ICON \"${app_icon}\"\n")

qt_add_executable(popsot-launcher WIN32
    src/main.cpp
    src/mainwindow.cpp src/mainwindow.h
    src/wizard.cpp src/wizard.h
    src/installer.cpp src/installer.h
    src/xiso.cpp src/xiso.h
    src/ini.cpp src/ini.h
    src/tr.cpp src/tr.h
    "${CMAKE_CURRENT_BINARY_DIR}/launcher.rc")
target_compile_definitions(popsot-launcher PRIVATE UNICODE _UNICODE NOMINMAX)
target_compile_options(popsot-launcher PRIVATE /W4 /permissive- /utf-8)
target_link_libraries(popsot-launcher PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
set_target_properties(popsot-launcher PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/../bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}/debug")
