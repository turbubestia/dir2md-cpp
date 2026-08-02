qt6_add_executable(backend_core_test
    main.cpp
    setting_manager_test.hpp
    setting_manager_test.cpp
)

target_include_directories(backend_core_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/backend
)

target_link_libraries(backend_core_test PRIVATE
    Qt6::Test
    dir2md_backend
)

set_target_properties(backend_core_test PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)


# Register tests with CTest + VS Code Test Explorer integration.
include(${CMAKE_SOURCE_DIR}/cmake/qtest_add_test.cmake)
qtest_add_test(
    TARGET backend_core_test
    SOURCES setting_manager_test.cpp
    PREFIX backend.core
)
