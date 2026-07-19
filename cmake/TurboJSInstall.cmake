# Install, package-config, and exported-target rules.
include_guard(GLOBAL)

if(TURBOJS_ENABLE_INSTALL)
    install(DIRECTORY include/turbojs/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/turbojs
        FILES_MATCHING PATTERN "*.h")

    set(TURBOJS_INSTALL_TARGETS turbojs turbojs_jit turbojsc turbojs_cli)
    if(TARGET turbojs_runtime)
        list(APPEND TURBOJS_INSTALL_TARGETS turbojs_runtime)
    endif()

    install(TARGETS ${TURBOJS_INSTALL_TARGETS}
        EXPORT TurboJSConfig
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

    install(EXPORT TurboJSConfig
        NAMESPACE TurboJS::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TurboJS)

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/TurboJSConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/TurboJSConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TurboJS)

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/turbojs.pc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/turbojs.pc"
        @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/turbojs.pc"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

    install(FILES LICENSE NOTICE.md
        DESTINATION ${CMAKE_INSTALL_DOCDIR})
    install(DIRECTORY docs/
        DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()
