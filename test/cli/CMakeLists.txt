# Register tests with CTest + VS Code Test Explorer integration.
include(${CMAKE_SOURCE_DIR}/cmake/qtest_add_test.cmake)

qtest_add_test(SOURCE cli_workflow_test.cpp PREFIX cli)

# qtest_add_test links Qt6::Test + dir2md_backend; the CLI tests additionally
# need the workflow/helper logic, which lives in the CLI static library.
target_link_libraries(cli_cli_workflow_test PRIVATE dir2md_cli_static)
