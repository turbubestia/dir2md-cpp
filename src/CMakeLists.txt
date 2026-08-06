
add_subdirectory(backend)
add_subdirectory(frontend)
add_subdirectory(cli)

# Post-install: deploy Qt runtime once for all executables
include(deploy-qt)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
deploy_qt_targets("${CMAKE_INSTALL_PREFIX}/bin-debug" dir2md_frontend dir2md_cli)
else()
deploy_qt_targets("${CMAKE_INSTALL_PREFIX}/bin" dir2md_frontend dir2md_cli)
endif()