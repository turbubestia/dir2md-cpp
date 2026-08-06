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

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(INSTALL_BIN_DIR bin-debug)
else()
    set(INSTALL_BIN_DIR bin)
endif()

install(TARGETS dir2md_frontend
    RUNTIME DESTINATION ${INSTALL_BIN_DIR}
)