
# Register tests with CTest + VS Code Test Explorer integration.
include(${CMAKE_SOURCE_DIR}/cmake/qtest_add_test.cmake)

qtest_add_test(SOURCE expected_test.cpp PREFIX backend.core)
qtest_add_test(SOURCE fzy_test.cpp PREFIX backend.core)
qtest_add_test(SOURCE model_client_test.cpp PREFIX backend.core)
qtest_add_test(SOURCE model_prompt_sanitization_test.cpp PREFIX backend.core)
qtest_add_test(SOURCE model_schema_parser_test.cpp PREFIX backend.core)
qtest_add_test(SOURCE pdf_renderer_test.cpp PREFIX backend.core)
qtest_add_test(SOURCE setting_manager_test.cpp PREFIX backend.core)
qtest_add_test(SOURCE thinking_stripper_test.cpp PREFIX backend.core)

# The model client tests drive a real client against the reusable in-process
# mock server. qtest_add_test takes a single SOURCE, so compile the mock into
# that target after the helper call (same pattern as test/cli/CMakeLists.txt).
# The mock needs no link dependencies beyond Qt6::Test + dir2md_backend.
# target_sources(backend_core_test_model_client PRIVATE mock_model_server.cpp)