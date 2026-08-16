# Static library holding the workflow + shared helper logic. It is linked by
# both the CLI executable and the in-process CLI tests, so the parsing and
# resolution behavior can be unit-tested without spawning a process.
qt_add_library(dir2md_cli_static STATIC
    cli_common.hpp
    cli_common.cpp
    chat_workflow.hpp
    chat_workflow.cpp
    ocr_workflow.hpp
    ocr_workflow.cpp
)

target_include_directories(dir2md_cli_static PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(dir2md_cli_static PUBLIC
    dir2md_backend
)

set_target_properties(dir2md_cli_static PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Thin executable: only main.cpp, links the static library.
qt_add_executable(dir2md_cli
    main.cpp
)

target_link_libraries(dir2md_cli PRIVATE
    dir2md_cli_static
)

set_target_properties(dir2md_cli PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

install(TARGETS dir2md_cli RUNTIME DESTINATION bin )