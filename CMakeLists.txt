cmake_minimum_required(VERSION 3.16)

project(instrument VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(QT_QML_GENERATE_QMLLS_INI ON)

find_package(Qt6 REQUIRED COMPONENTS Quick)

qt_standard_project_setup(REQUIRES 6.8)

# 开启自动处理 RCC
set(CMAKE_AUTORCC ON)

qt_add_executable(appinstrument
    main.cpp
    application.qrc
)

qt_add_qml_module(appinstrument
    URI instrument
    QML_FILES
        Main.qml
        SOURCES dump_handler.h
        SOURCES meter.h meter.cpp
        SOURCES log.cpp log.h

)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appinstrument PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appinstrument
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appinstrument
    PRIVATE Qt6::Quick
    dbghelp
)

install(TARGETS appinstrument
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
