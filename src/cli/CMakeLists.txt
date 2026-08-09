qt_add_executable(dir2md_cli main.cpp)

target_link_libraries(dir2md_cli PRIVATE
    dir2md_backend
)

set_target_properties(dir2md_cli PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

install(TARGETS dir2md_cli RUNTIME DESTINATION bin )