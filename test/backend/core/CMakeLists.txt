qt_standard_project_setup()

qt6_add_executable(backend_core_test
    main.cpp
    fzy_test.hpp
    fzy_test.cpp
    # test_expected.hpp
    # test_expected.cpp
    # test_schema_parser.hpp
    # test_schema_parser.cpp
    # test_prompt_sanitization.hpp
    # test_prompt_sanitization.cpp
    # test_model_client.hpp
    # test_model_client.cpp
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
    SOURCES fzy_test.cpp
    PREFIX backend.core.fzy_test
)

# qtest_add_test(
#     TARGET backend_core_test
#     SOURCES test_expected.cpp
#     PREFIX backend.core.test_expected
# )

# qtest_add_test(
#     TARGET backend_core_test
#     SOURCES test_schema_parser.cpp
#     PREFIX backend.core.test_schema_parser
# )

# qtest_add_test(
#     TARGET backend_core_test
#     SOURCES test_prompt_sanitization.cpp
#     PREFIX backend.core.test_prompt_sanitization
# )

# qtest_add_test(
#     TARGET backend_core_test
#     SOURCES test_model_client.cpp
#     PREFIX backend.core.test_model_client
# )