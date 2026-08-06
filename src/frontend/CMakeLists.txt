qt_standard_project_setup()

qt_add_executable(dir2md_frontend
    main.cpp
)

qt_add_qml_module(dir2md_frontend
    URI dir2md_frontend
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES Main.qml
)

target_link_libraries(dir2md_frontend PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
)

install(TARGETS dir2md_frontend RUNTIME DESTINATION bin )