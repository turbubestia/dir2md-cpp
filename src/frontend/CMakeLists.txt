qt_standard_project_setup()

qt_add_executable(dir2mdQtQuick
    main.cpp
)

qt_add_qml_module(dir2mdQtQuick
    URI dir2mdQtQuick
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES Main.qml
)

target_link_libraries(dir2mdQtQuick PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
)