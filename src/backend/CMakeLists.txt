qt_add_library(dir2md_backend STATIC MANUAL_FINALIZATION)

add_subdirectory(core)

qt_finalize_target(dir2md_backend)

target_include_directories(dir2md_backend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(dir2md_backend PUBLIC
    Qt6::Core
)

set_target_properties(dir2md_backend PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Debug-only test-path override: only active when DIR2MD_DEBUG_TEST_PATH cache variable is set.
if(DIR2MD_DEBUG_TEST_PATH)
    target_compile_definitions(dir2md_backend PUBLIC DIR2MD_DEBUG_TEST_PATH)
endif()
