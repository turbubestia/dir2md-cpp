# deploy_qt_targets(<install_dir> <target1> [target2] ...)
#
# Creates a post-install step that runs windeployqt once for all given targets.
# Only activates on Windows where windeployqt is available.
#
# The deployment is performed a single time (not per-target) during `cmake --install`.
#
# Example:
#   deploy_qt_targets("${CMAKE_INSTALL_PREFIX}/bin" dir2md_frontend dir2md_cli)

function(deploy_qt_targets install_dir)
    if(NOT WIN32)
        return()
    endif()

    # Build list of installed executable paths (they live in install_dir after install)
    set(installed_exe_paths "")
    foreach(target IN LISTS ARGN)
        get_target_property(target_type ${target} TYPE)
        if(NOT target_type STREQUAL "EXECUTABLE")
            message(WARNING "deploy_qt_targets: '${target}' is not an EXECUTABLE, skipping.")
            continue()
        endif()
        list(APPEND installed_exe_paths
            "${install_dir}/${target}${CMAKE_EXECUTABLE_SUFFIX}"
        )
    endforeach()

    if(NOT installed_exe_paths)
        return()
    endif()

    # Find windeployqt via Qt6
    find_program(WINDEPLOYQT_EXECUTABLE NAMES windeployqt PATHS "${Qt6_DIR}/../../../bin" NO_DEFAULT_PATH)
    if(NOT WINDEPLOYQT_EXECUTABLE)
        # Fallback: search in PATH
        find_program(WINDEPLOYQT_EXECUTABLE NAMES windeployqt)
    endif()

    if(NOT WINDEPLOYQT_EXECUTABLE)
        message(WARNING "windeployqt not found. Qt runtime will NOT be deployed automatically.")
        return()
    endif()

    # Build the windeployqt command arguments
    set(windeployqt_args "--dir" "${install_dir}")

    # Check if any target links to Qt Quick (needs --qmldir)
    foreach(target IN LISTS ARGN)
        get_target_property(link_libs ${target} LINK_LIBRARIES)
        if(link_libs MATCHES "Qt6::Quick")
            get_target_property(target_source_dir ${target} SOURCE_DIR)
            list(APPEND windeployqt_args "--qmldir" "${target_source_dir}")
            break()
        endif()
    endforeach()

    # Add all installed executable paths to the command
    foreach(exe_path IN LISTS installed_exe_paths)
        list(APPEND windeployqt_args "${exe_path}")
    endforeach()

    # Create a custom install script that runs windeployqt
    # IMPORTANT: execute_process(COMMAND ...) expects space-separated arguments,
    # not CMake's semicolon-separated lists. We write each argument on its own line
    # so CMake parses them as separate list elements.
    set(deploy_script "${CMAKE_CURRENT_BINARY_DIR}/deploy_qt.cmake")
    file(WRITE "${deploy_script}"
        "# Auto-generated Qt deployment script\n"
        "message(STATUS \"Deploying Qt runtime to ${install_dir}\")\n"
        "execute_process(\n"
        "    COMMAND\n"
    )
    # Write each argument as a separate line so CMake treats them as individual list items
    file(APPEND "${deploy_script}" "        \"${WINDEPLOYQT_EXECUTABLE}\"\n")
    foreach(arg IN LISTS windeployqt_args)
        file(APPEND "${deploy_script}" "        \"${arg}\"\n")
    endforeach()
    file(APPEND "${deploy_script}"
        "    RESULT_VARIABLE deploy_result\n"
        "    OUTPUT_VARIABLE deploy_output\n"
        "    ERROR_VARIABLE deploy_error\n"
        ")\n"
        "if(NOT deploy_result EQUAL 0)\n"
        "    message(WARNING \"windeployqt failed with code \${deploy_result}\")\n"
        "    message(WARNING \"\${deploy_output}\")\n"
        "    message(WARNING \"\${deploy_error}\")\n"
        "else()\n"
        "    message(STATUS \"Qt deployment complete\")\n"
        "endif()\n"
    )

    install(CODE "include(\"${deploy_script}\")")
endfunction()
