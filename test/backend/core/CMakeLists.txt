
# Register tests with CTest + VS Code Test Explorer integration.
include(${CMAKE_SOURCE_DIR}/cmake/qtest_add_test.cmake)

qtest_add_test(SOURCE fzy_test.cpp PREFIX backend.core)
qtest_add_test(SOURCE setting_manager_test.cpp PREFIX backend.core)
qtest_add_test(SOURCE test_expected.cpp PREFIX backend.core)
qtest_add_test(SOURCE test_prompt_sanitization.cpp PREFIX backend.core)
qtest_add_test(SOURCE test_model_client.cpp PREFIX backend.core)
qtest_add_test(SOURCE test_schema_parser.cpp PREFIX backend.core)
qtest_add_test(SOURCE thinking_stripper_test.cpp PREFIX backend.core)
qtest_add_test(SOURCE pdf_renderer_test.cpp PREFIX backend.core)
