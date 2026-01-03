include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateManual.cmake)

set(APP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(APP_RESOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources")

set(APP_SOURCE
    "${APP_SOURCE_DIR}/Main.cpp"
    "${APP_RESOURCE_DIR}/qrc/resources.qrc")

set(APP_TRANSLATIONS_DIR "${APP_RESOURCE_DIR}/translations")
set(APP_TRANSLATION_TS_FILES
    "${APP_TRANSLATIONS_DIR}/brumschtick_en.ts"
    "${APP_TRANSLATIONS_DIR}/brumschtick_fr.ts"
    "${APP_TRANSLATIONS_DIR}/brumschtick_pl.ts"
    "${APP_TRANSLATIONS_DIR}/brumschtick_de.ts"
    "${APP_TRANSLATIONS_DIR}/brumschtick_es.ts"
    "${APP_TRANSLATIONS_DIR}/brumschtick_it.ts"
    "${APP_TRANSLATIONS_DIR}/brumschtick_pt_BR.ts"
    "${APP_TRANSLATIONS_DIR}/brumschtick_ru.ts"
    "${APP_TRANSLATIONS_DIR}/brumschtick_ja.ts"
    "${APP_TRANSLATIONS_DIR}/brumschtick_ko.ts"
    "${APP_TRANSLATIONS_DIR}/brumschtick_zh_CN.ts"
    "${APP_TRANSLATIONS_DIR}/brumschtick_zh_TW.ts"
    "${APP_TRANSLATIONS_DIR}/brumschtick_cs.ts"
    "${APP_TRANSLATIONS_DIR}/brumschtick_nl.ts"
    "${APP_TRANSLATIONS_DIR}/brumschtick_sv.ts"
    "${APP_TRANSLATIONS_DIR}/brumschtick_nb.ts"
    "${APP_TRANSLATIONS_DIR}/brumschtick_da.ts"
    "${APP_TRANSLATIONS_DIR}/brumschtick_fi.ts"
    "${APP_TRANSLATIONS_DIR}/brumschtick_tr.ts"
    "${APP_TRANSLATIONS_DIR}/brumschtick_uk.ts")

qt6_add_translation(APP_TRANSLATION_QM_FILES ${APP_TRANSLATION_TS_FILES})
set(APP_SOURCE ${APP_SOURCE} ${APP_TRANSLATION_QM_FILES})

# OS X app bundle configuration, must happen before the executable is added
if(APPLE)
    # Configure icons
    set(MACOSX_ICON_FILES "${APP_RESOURCE_DIR}/mac/icons/AppIcon.icns" "${APP_RESOURCE_DIR}/mac/icons/DocIcon.icns")
    set(APP_SOURCE ${APP_SOURCE} ${MACOSX_ICON_FILES})
    set_source_files_properties(${MACOSX_ICON_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

    # Configure button bitmaps etc.
    file(GLOB_RECURSE MACOSX_IMAGE_FILES "${APP_RESOURCE_DIR}/graphics/images/*")
    set(APP_SOURCE ${APP_SOURCE} ${MACOSX_IMAGE_FILES})
    set_source_files_properties(${MACOSX_IMAGE_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/images)

    # Configure fonts
    file(GLOB_RECURSE MACOSX_FONT_FILES "${APP_RESOURCE_DIR}/fonts/*.*")
    set(APP_SOURCE ${APP_SOURCE} ${MACOSX_FONT_FILES})
    set_source_files_properties(${MACOSX_FONT_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/fonts)

    # Configure translations
    set_source_files_properties(${APP_TRANSLATION_QM_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/translations)

    # Configure default resources
    # Collect all default resources
    file(GLOB_RECURSE MACOSX_DEFAULT_FILES RELATIVE ${APP_RESOURCE_DIR}/defaults "${APP_RESOURCE_DIR}/defaults/*")

    # Set correct locations for default resources
    foreach(DEFAULT_FILE ${MACOSX_DEFAULT_FILES})
        get_filename_component(DEFAULT_FILE_DIR "${DEFAULT_FILE}" DIRECTORY)
        set(DEFAULT_FILE "${APP_RESOURCE_DIR}/defaults/${DEFAULT_FILE}")

        set(APP_SOURCE ${APP_SOURCE} ${DEFAULT_FILE})
        set_source_files_properties(${DEFAULT_FILE} PROPERTIES  MACOSX_PACKAGE_LOCATION Resources/defaults/${DEFAULT_FILE_DIR})
    endforeach()

    # Configure game resources
    # Collect all game resources
    file(GLOB_RECURSE MACOSX_GAME_FILES RELATIVE ${APP_RESOURCE_DIR}/games "${APP_RESOURCE_DIR}/games/*")

    # Set correct locations for game files
    foreach(GAME_FILE ${MACOSX_GAME_FILES})
        get_filename_component(GAME_FILE_DIR "${GAME_FILE}" DIRECTORY)
        set(GAME_FILE "${APP_RESOURCE_DIR}/games/${GAME_FILE}")

        set(APP_SOURCE ${APP_SOURCE} ${GAME_FILE})
        set_source_files_properties(${GAME_FILE} PROPERTIES  MACOSX_PACKAGE_LOCATION Resources/games/${GAME_FILE_DIR} HEADER_FILE_ONLY ON)
    endforeach()

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        # Collect all untested game resources
        file(GLOB_RECURSE MACOSX_GAME_TESTING_FILES RELATIVE ${APP_RESOURCE_DIR}/games-testing "${APP_RESOURCE_DIR}/games-testing/*")

        # Set correct locations for untested game files
        foreach(GAME_FILE ${MACOSX_GAME_TESTING_FILES})
            get_filename_component(GAME_FILE_DIR "${GAME_FILE}" DIRECTORY)
            set(GAME_FILE "${APP_RESOURCE_DIR}/games-testing/${GAME_FILE}")

            set(APP_SOURCE ${APP_SOURCE} ${GAME_FILE})
            set_source_files_properties(${GAME_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/games/${GAME_FILE_DIR} HEADER_FILE_ONLY ON)
        endforeach()
    endif()

    # Configure shaders
    # Collect all shaders
    file(GLOB_RECURSE MACOSX_SHADER_FILES
            "${APP_RESOURCE_DIR}/shader/*.fragsh"
            "${APP_RESOURCE_DIR}/shader/*.vertsh")
    set_source_files_properties(${MACOSX_SHADER_FILES} PROPERTIES  MACOSX_PACKAGE_LOCATION Resources/shader)
    set(APP_SOURCE ${APP_SOURCE} ${MACOSX_SHADER_FILES})

    # Configure Qt stylesheets
    file (GLOB_RECURSE MACOSX_STYLESHEET_FILES
            "${APP_RESOURCE_DIR}/stylesheets/*.qss")
    set_source_files_properties(${MACOSX_STYLESHEET_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/stylesheets)
    set(APP_SOURCE ${APP_SOURCE} ${MACOSX_STYLESHEET_FILES})

    # Configure update scripts
    set_source_files_properties(${LIB_UPDATE_SCRIPT} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/update)
    set(APP_SOURCE ${APP_SOURCE} ${LIB_UPDATE_SCRIPT})

    # Configure manual files
    set_source_files_properties(${INDEX_OUTPUT_PATH} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/manual)
    set_source_files_properties(${DOC_MANUAL_TARGET_FILES_ABSOLUTE} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/manual)
    set_source_files_properties(${DOC_MANUAL_SHORTCUTS_JS_TARGET_ABSOLUTE} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/manual)
    set_source_files_properties(${DOC_MANUAL_TARGET_IMAGE_FILES_ABSOLUTE} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/manual/images)
endif()

# Set up resource compilation for Windows
if(WIN32)
    # Create the windows resource file
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/BrumSchtick.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/BrumSchtick.rc" @ONLY)

    if(COMPILER_IS_MSVC)
        set(APP_SOURCE ${APP_SOURCE} "${CMAKE_CURRENT_BINARY_DIR}/BrumSchtick.rc")
    elseif(MINGW)
        set(CMAKE_RC_COMPILER_INIT windres)
        enable_language(RC)
        set(CMAKE_RC_COMPILE_OBJECT "<CMAKE_RC_COMPILER> <FLAGS> <DEFINES> -i <SOURCE> -o <OBJECT>")
    endif()
endif()

# Set CPACK_PACKAGE_VERSION which is used by CPack.
# Strip the "v" prefix off GIT_DESCRIBE (e.g. "v2020.1-RC1") to produce a CPACK_PACKAGE_VERSION of "2020.1-RC1"
# Debian wants the first character to be a number
string(REGEX REPLACE "^v(.*)" "\\1" CPACK_PACKAGE_VERSION "${GIT_DESCRIBE}")
get_build_platform(APP_PLATFORM_NAME)

# Create the executable target and link with libraries
add_executable(BrumSchtick WIN32 MACOSX_BUNDLE)
target_sources(BrumSchtick PRIVATE ${APP_SOURCE} ${INDEX_OUTPUT_PATH} ${DOC_MANUAL_TARGET_FILES_ABSOLUTE} ${DOC_MANUAL_SHORTCUTS_JS_TARGET_ABSOLUTE} ${DOC_MANUAL_TARGET_IMAGE_FILES_ABSOLUTE})

add_custom_target(BrumSchtickTranslations DEPENDS ${APP_TRANSLATION_QM_FILES})
add_dependencies(BrumSchtick BrumSchtickTranslations)

target_link_libraries(BrumSchtick PRIVATE CompilerConfig)
target_link_libraries(BrumSchtick PRIVATE common)

# Xcode "new build system" does't allow custom commands to be depended on by two unrelated targets
# In this case, ${INDEX_OUTPUT_PATH} is depended on by BrumSchtick and GenerateManual
# so we must make BrumSchtick depend on GenerateManual (see #3608)
add_dependencies(BrumSchtick GenerateManual)

set_target_properties(BrumSchtick PROPERTIES AUTORCC TRUE)

if(APPLE)
    # Configure variables that are substituted into the plist
    # Set CFBundleName, which controls the application menu label
    set_target_properties(BrumSchtick PROPERTIES MACOSX_BUNDLE_BUNDLE_NAME "BrümSchtick")
    # Set CFBundleShortVersionString to "2.0.0". This is displayed in the Finder and Spotlight.
    set_target_properties(BrumSchtick PROPERTIES MACOSX_BUNDLE_SHORT_VERSION_STRING "${CPACK_PACKAGE_VERSION}")
    # Set CFBundleVersion to the git describe output. Apple docs say it should be "three non-negative, period-separated integers with the first integer being greater than zero"
    set_target_properties(BrumSchtick PROPERTIES MACOSX_BUNDLE_BUNDLE_VERSION "${GIT_DESCRIBE}")

    # Set NSHumanReadableCopyright.
    string(TIMESTAMP TB_MACOSX_BUNDLE_COPYRIGHT "Copyright © %Y Kristian Duske. All rights reserved.")
    set_target_properties(BrumSchtick PROPERTIES MACOSX_BUNDLE_COPYRIGHT "${TB_MACOSX_BUNDLE_COPYRIGHT}")

    # Set the path to the plist template
    set_target_properties(BrumSchtick PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${APP_RESOURCE_DIR}/mac/BrumSchtick-Info.plist")

    # If requested, run macdeployqt which copies the Qt frameworks into the
    # BrumSchtick.app bundle and modifies the BrumSchtick executable to refer to these
    # private copies of the framework instead of the ones in /usr/local. This is slow so
    # only do it if requested.
    if(TB_RUN_MACDEPLOYQT)
        message(STATUS "macdeployqt requested")

        # Get macdeployqt path (hack)
        get_target_property(TB_QMAKE_PATH Qt6::qmake IMPORTED_LOCATION)
        string(REPLACE "qmake" "macdeployqt" TB_MACDEPLOYQT_PATH "${TB_QMAKE_PATH}")
        message(STATUS "macdeployqt found: ${TB_MACDEPLOYQT_PATH}")

        if(TB_SIGN_MAC_BUNDLE)
            # Have macdeployqt codesign the app bundle. See BUILDING.md for more details on
            # how to set up the signing identity.
            add_custom_command(TARGET BrumSchtick POST_BUILD
                COMMAND "${TB_MACDEPLOYQT_PATH}" "$<TARGET_BUNDLE_DIR:BrumSchtick>"
                        "-codesign=${TB_SIGN_IDENTITY}"
                        "-sign-for-notarization=${TB_SIGN_IDENTITY}"
                        "-hardened-runtime"
                        "-verbose=2"
                COMMAND "codesign"
                        "--verify"
                        "--verbose=3"
                        "--deep"
                        "--strict"
                        "$<TARGET_BUNDLE_DIR:BrumSchtick>"
                VERBATIM)
        else()
            add_custom_command(TARGET BrumSchtick POST_BUILD
                COMMAND "${TB_MACDEPLOYQT_PATH}" "$<TARGET_BUNDLE_DIR:BrumSchtick>")
        endif()
    endif()
endif()

# Copy some Windows-specific resources
if(WIN32)
    # Copy icons to target dir
    add_custom_command(TARGET BrumSchtick PRE_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${APP_RESOURCE_DIR}/win32/icons/AppIcon.ico"    "${CMAKE_CURRENT_BINARY_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy "${APP_RESOURCE_DIR}/win32/icons/DocIcon.ico"    "${CMAKE_CURRENT_BINARY_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy "${APP_RESOURCE_DIR}/win32/icons/WindowIcon.ico" "${CMAKE_CURRENT_BINARY_DIR}")

    # Copy DLLs to app directory
    add_custom_command(TARGET BrumSchtick POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:assimp::assimp>" "$<TARGET_FILE_DIR:BrumSchtick>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:freeimage::FreeImage>" "$<TARGET_FILE_DIR:BrumSchtick>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:freetype>" "$<TARGET_FILE_DIR:BrumSchtick>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:tinyxml2::tinyxml2>" "$<TARGET_FILE_DIR:BrumSchtick>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:miniz::miniz>" "$<TARGET_FILE_DIR:BrumSchtick>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:GLEW::GLEW>" "$<TARGET_FILE_DIR:BrumSchtick>")

    # Run windeployqt which copies the Qt dlls into the deployable folder, along with necessary runtime dlls by the compiler
    # Only copy translations on release builds, as deploying them takes significantly longer
    if(NOT TB_SKIP_WINDEPLOYQT)
        message(STATUS "windeployqt requested")

        # Get windeployqt path (hack)
        get_target_property(TB_QMAKE_PATH Qt6::qmake IMPORTED_LOCATION)
        string(REPLACE "qmake" "windeployqt" TB_WINDEPLOYQT_PATH "${TB_QMAKE_PATH}")
        message(STATUS "windeployqt found: ${TB_WINDEPLOYQT_PATH}")

        add_custom_command(TARGET BrumSchtick POST_BUILD
                COMMAND "${TB_WINDEPLOYQT_PATH}"
                        --no-compiler-runtime
                        $<$<CONFIG:Debug>:--no-translations>
                        "$<TARGET_FILE_DIR:BrumSchtick>")
    else()
        message(STATUS "windeployqt skipped")
    endif()
endif()

# Generate a small stripped PDB for release builds so we get stack traces with symbols
if(COMPILER_IS_MSVC)
    set_target_properties(BrumSchtick PROPERTIES LINK_FLAGS_RELEASE "/DEBUG /PDBSTRIPPED:app/BrumSchtick-stripped.pdb /PDBALTPATH:BrumSchtick-stripped.pdb")
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux|FreeBSD")
    # Make executable name conventional lowercase on linux
    string(TOLOWER BrumSchtick APP_TARGET_NAME_LOWER)
    set_target_properties(BrumSchtick PROPERTIES OUTPUT_NAME ${APP_TARGET_NAME_LOWER})
endif()

# Set up the resources and DLLs for the executable
set(APP_RESOURCE_DEST_DIR "$<TARGET_FILE_DIR:BrumSchtick>")

if(WIN32 OR ${CMAKE_SYSTEM_NAME} MATCHES "Linux|FreeBSD")
    # Copy button images to resources directory
    add_custom_command(TARGET BrumSchtick POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${APP_RESOURCE_DIR}/graphics/images" "${APP_RESOURCE_DEST_DIR}/images")

    # Copy fonts to resources directory
    add_custom_command(TARGET BrumSchtick POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${APP_RESOURCE_DIR}/fonts" "${APP_RESOURCE_DEST_DIR}/fonts")

    # Copy default resources to resources directory
    add_custom_command(TARGET BrumSchtick POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${APP_RESOURCE_DIR}/defaults/" "${APP_RESOURCE_DEST_DIR}/defaults")

    # Copy game files to resources directory
    add_custom_command(TARGET BrumSchtick POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${APP_RESOURCE_DIR}/games/" "${APP_RESOURCE_DEST_DIR}/games")

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        # Copy untested game files to resources directory
        add_custom_command(TARGET BrumSchtick POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory "${APP_RESOURCE_DIR}/games-testing/" "${APP_RESOURCE_DEST_DIR}/games")
    endif()

    # Copy shader files to resources directory
    add_custom_command(TARGET BrumSchtick POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${APP_RESOURCE_DIR}/shader" "${APP_RESOURCE_DEST_DIR}/shader")

    # Copy Qt stylesheet files to resources directory
    add_custom_command(TARGET BrumSchtick POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_RESOURCE_DEST_DIR}/stylesheets/"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${APP_RESOURCE_DIR}/stylesheets" "${APP_RESOURCE_DEST_DIR}/stylesheets")

    # Copy translations to resources directory
    add_custom_command(TARGET BrumSchtick POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_RESOURCE_DEST_DIR}/translations"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${APP_TRANSLATION_QM_FILES} "${APP_RESOURCE_DEST_DIR}/translations")

    # Copy update scripts to resources directory
    add_custom_command(TARGET BrumSchtick POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_RESOURCE_DEST_DIR}/update/"
            COMMAND ${CMAKE_COMMAND} -E copy "${LIB_UPDATE_SCRIPT}" "${APP_RESOURCE_DEST_DIR}/update")

    # Copy manual files to resource directory
    add_custom_command(TARGET BrumSchtick POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_RESOURCE_DEST_DIR}/manual/"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_RESOURCE_DEST_DIR}/manual/images"
            COMMAND ${CMAKE_COMMAND} -E copy "${INDEX_OUTPUT_PATH}" "${APP_RESOURCE_DEST_DIR}/manual/"
            COMMAND ${CMAKE_COMMAND} -E copy "${DOC_MANUAL_SHORTCUTS_JS_TARGET_ABSOLUTE}" "${APP_RESOURCE_DEST_DIR}/manual/")

    foreach(MANUAL_FILE ${DOC_MANUAL_TARGET_FILES_ABSOLUTE})
        add_custom_command(TARGET BrumSchtick POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy ${MANUAL_FILE} "${APP_RESOURCE_DEST_DIR}/manual/")
    endforeach(MANUAL_FILE)

    foreach(MANUAL_IMAGE_FILE ${DOC_MANUAL_TARGET_IMAGE_FILES_ABSOLUTE})
        add_custom_command(TARGET BrumSchtick POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy ${MANUAL_IMAGE_FILE} "${APP_RESOURCE_DEST_DIR}/manual/images/")
    endforeach(MANUAL_IMAGE_FILE)
endif()

# Common CPack configuration
set(APP_PACKAGE_FILE_NAME "BrumSchtick-${APP_PLATFORM_NAME}-${CMAKE_SYSTEM_PROCESSOR}-${GIT_DESCRIBE}-${CMAKE_BUILD_TYPE}")
set(CPACK_PACKAGE_FILE_NAME ${APP_PACKAGE_FILE_NAME})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "BrümSchtick Level Editor")
set(CPACK_PACKAGE_VENDOR "BrümSchtick")
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    # We want stack trace symbols even in our release builds, so don't strip
    set(CPACK_STRIP_FILES FALSE)
else()
    set(CPACK_STRIP_FILES FALSE)
endif()

# Platform specific CPack configuration
if(WIN32)
    # Copy PDB files (msvc debug symbols)
    if(COMPILER_IS_MSVC)
        if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
            install(FILES
                    "$<TARGET_FILE_DIR:BrumSchtick>/BrumSchtick.pdb"
                    DESTINATION . COMPONENT BrumSchtick)
        elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
            install(FILES
                    "$<TARGET_FILE_DIR:BrumSchtick>/BrumSchtick-stripped.pdb"
                    DESTINATION . COMPONENT BrumSchtick)
        endif()
    endif()

    install(TARGETS BrumSchtick RUNTIME DESTINATION . COMPONENT BrumSchtick)
    install(CODE [[
        # The regexes are a workaround for https://gitlab.kitware.com/cmake/cmake/-/issues/22431
        file(GET_RUNTIME_DEPENDENCIES
            RESOLVED_DEPENDENCIES_VAR RESOLVED_DLLS
            PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*" "hvsifiletrust.dll"
            POST_EXCLUDE_REGEXES ".*[Ss][Yy][Ss][Tt][Ee][Mm]32.*"
            EXECUTABLES "$<TARGET_FILE:BrumSchtick>")

        message("Packaging .DLL's:")
        foreach(DLL ${RESOLVED_DLLS})
            message("${DLL}")
            file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}" FILES "${DLL}")
        endforeach()
    ]])

    # Qt plugins etc.
    install(DIRECTORY
            "$<TARGET_FILE_DIR:BrumSchtick>/platforms"
            "$<TARGET_FILE_DIR:BrumSchtick>/styles"
            "$<TARGET_FILE_DIR:BrumSchtick>/tls"
            DESTINATION . COMPONENT BrumSchtick)

    
    #Manual
    install(FILES ${INDEX_OUTPUT_PATH} DESTINATION manual COMPONENT BrumSchtick)
    install(FILES ${DOC_MANUAL_SHORTCUTS_JS_TARGET_ABSOLUTE} DESTINATION manual COMPONENT BrumSchtick)
    install(FILES
            ${DOC_MANUAL_TARGET_FILES_ABSOLUTE}
            DESTINATION manual COMPONENT BrumSchtick)
    install(FILES
            ${DOC_MANUAL_TARGET_IMAGE_FILES_ABSOLUTE}
            DESTINATION manual/images COMPONENT BrumSchtick)
    install(DIRECTORY
            "${APP_DIR}/resources/defaults"
            "${APP_DIR}/resources/fonts"
            "${APP_DIR}/resources/games"
            "${APP_DIR}/resources/graphics/images"
            "${APP_DIR}/resources/shader"
            "${APP_DIR}/resources/stylesheets"
            DESTINATION . COMPONENT BrumSchtick)
    install(FILES
            ${APP_TRANSLATION_QM_FILES}
            DESTINATION translations COMPONENT BrumSchtick)
    install(FILES
            ${LIB_UPDATE_SCRIPT}
            DESTINATION update COMPONENT BrumSchtick)
    set(CPACK_GENERATOR "ZIP")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY FALSE)

    set(CPACK_PACKAGE_FILE_EXT "zip")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateChecksum.bat.in" "${CMAKE_BINARY_DIR}/generate_checksum.bat" @ONLY)
elseif(APPLE)
    install(TARGETS BrumSchtick BUNDLE DESTINATION . COMPONENT BrumSchtick)

    set(CPACK_PACKAGE_FILE_EXT "zip")
    set(CPACK_GENERATOR "ZIP")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY FALSE)

    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/SignMacOsArchive.sh.in" "${CMAKE_CURRENT_BINARY_DIR}/sign_macos_archive.sh" @ONLY)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateChecksum.sh.in" "${CMAKE_CURRENT_BINARY_DIR}/generate_checksum.sh" @ONLY)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

    set(LINUX_RESOURCE_LOCATION "share/BrumSchtick")
    set(LINUX_TARGET_RESOURCE_DIRECTORY ${CPACK_PACKAGING_INSTALL_PREFIX}/${LINUX_RESOURCE_LOCATION})

    # add mime type definitions
    install(FILES "${APP_DIR}/resources/linux/brumschtick.xml" DESTINATION "share/mime/packages/" COMPONENT BrumSchtick)

    # configure install scripts
    configure_file(${APP_DIR}/resources/linux/postinst ${CMAKE_CURRENT_BINARY_DIR}/linux/postinst @ONLY)
    configure_file(${APP_DIR}/resources/linux/prerm ${CMAKE_CURRENT_BINARY_DIR}/linux/prerm @ONLY)
    configure_file(${APP_DIR}/resources/linux/postrm ${CMAKE_CURRENT_BINARY_DIR}/linux/postrm @ONLY)

    # add files
    install(TARGETS BrumSchtick RUNTIME DESTINATION bin COMPONENT BrumSchtick)
    install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fonts"           DESTINATION ${LINUX_RESOURCE_LOCATION} COMPONENT BrumSchtick)
    install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/defaults"        DESTINATION ${LINUX_RESOURCE_LOCATION} COMPONENT BrumSchtick)
    install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/games"           DESTINATION ${LINUX_RESOURCE_LOCATION} COMPONENT BrumSchtick)
    install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/manual"          DESTINATION ${LINUX_RESOURCE_LOCATION} COMPONENT BrumSchtick)
    install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/images"          DESTINATION ${LINUX_RESOURCE_LOCATION} COMPONENT BrumSchtick)
    install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shader"          DESTINATION ${LINUX_RESOURCE_LOCATION} COMPONENT BrumSchtick)
    install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/stylesheets"     DESTINATION ${LINUX_RESOURCE_LOCATION} COMPONENT BrumSchtick)
    install(FILES ${APP_TRANSLATION_QM_FILES}                       DESTINATION ${LINUX_RESOURCE_LOCATION}/translations COMPONENT BrumSchtick)
    install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/update"          DESTINATION ${LINUX_RESOURCE_LOCATION} COMPONENT BrumSchtick)
    install(FILES "${CMAKE_SOURCE_DIR}/LICENSE.txt"                 DESTINATION ${LINUX_RESOURCE_LOCATION} COMPONENT BrumSchtick)
    install(FILES "${APP_DIR}/resources/linux/copyright"            DESTINATION ${LINUX_RESOURCE_LOCATION} COMPONENT BrumSchtick)

    # configure AppImage via an external CPack generator
    set(CPACK_PACKAGE_FILE_EXT "zip")
    set(TB_LINUXDEPLOY_PATH "../linuxdeploy-x86_64.AppImage")
    get_target_property(TB_QMAKE_PATH Qt6::qmake IMPORTED_LOCATION)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/AppImageGenerator.cmake.in" "${CMAKE_BINARY_DIR}/AppImageGenerator.cmake" @ONLY)

    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateChecksum.sh.in" "${CMAKE_CURRENT_BINARY_DIR}/generate_checksum.sh" @ONLY)
    set(CPACK_EXTERNAL_PACKAGE_SCRIPT "${CMAKE_BINARY_DIR}/AppImageGenerator.cmake")
    set(CPACK_EXTERNAL_ENABLE_STAGING True)
    set(CPACK_GENERATOR "External")
endif()

include(CPack)

