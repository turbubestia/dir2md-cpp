qt_add_executable(dir2md_cli main.cpp)

target_link_libraries(dir2md_cli PRIVATE
    Qt6::Core
)

set_target_properties(dir2md_cli PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(INSTALL_BIN_DIR bin-debug)
else()
    set(INSTALL_BIN_DIR bin)
endif()

install(TARGETS dir2md_cli
    RUNTIME DESTINATION ${INSTALL_BIN_DIR}
)