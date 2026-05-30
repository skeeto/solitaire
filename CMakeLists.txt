cmake_minimum_required(VERSION 3.24)
project(SawayamaSolitaire CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

include(FetchContent)

# --- SDL3: fetched over HTTPS with a pinned SHA256, linked statically ---
set(SDL_SHARED   OFF CACHE BOOL "" FORCE)
set(SDL_STATIC   ON  CACHE BOOL "" FORCE)
set(SDL_TEST     OFF CACHE BOOL "" FORCE)
set(SDL_TESTS    OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL  OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL3
    URL      https://github.com/libsdl-org/SDL/releases/download/release-3.4.8/SDL3-3.4.8.tar.gz
    URL_HASH SHA256=e9fff7467fb60f037e6708da18b25560649e4c63edc2a69bb871b960d9cbfbba
    DOWNLOAD_EXTRACT_TIMESTAMP ON
)
FetchContent_MakeAvailable(SDL3)

add_executable(solitaire
    src/main.cpp
    src/game.cpp
    src/render.cpp
    src/audio.cpp
    src/storage.cpp
    src/stb_impl.cpp
)
target_include_directories(solitaire PRIVATE third_party)
target_link_libraries(solitaire PRIVATE SDL3::SDL3-static)

if(EMSCRIPTEN)
    # Emscripten only links statically; emit index.html via our PWA shell.
    set_target_properties(solitaire PROPERTIES OUTPUT_NAME index SUFFIX ".html")
    target_link_options(solitaire PRIVATE
        "-sALLOW_MEMORY_GROWTH=1"
        "-sENVIRONMENT=web"
        "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
    )
    # Place the PWA support files next to the generated index.{html,js,wasm},
    # and stamp the service worker's cache name with a hash of the wasm so each
    # deploy invalidates the previous cache.
    add_custom_command(TARGET solitaire POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/web/manifest.webmanifest
            ${CMAKE_CURRENT_SOURCE_DIR}/web/icon-192.png
            ${CMAKE_CURRENT_SOURCE_DIR}/web/icon-512.png
            $<TARGET_FILE_DIR:solitaire>
        COMMAND ${CMAKE_COMMAND}
            -DSW_IN=${CMAKE_CURRENT_SOURCE_DIR}/web/sw.js
            -DWASM=$<TARGET_FILE_DIR:solitaire>/index.wasm
            -DOUT=$<TARGET_FILE_DIR:solitaire>/sw.js
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/stamp_sw.cmake
        VERBATIM
    )

    # Install the complete, deployable site to CMAKE_INSTALL_PREFIX (e.g. a
    # gh-pages worktree). Note sw.js comes from the build dir (the stamped copy).
    install(FILES
        $<TARGET_FILE:solitaire>                       # index.html
        $<TARGET_FILE_DIR:solitaire>/index.js
        $<TARGET_FILE_DIR:solitaire>/index.wasm
        $<TARGET_FILE_DIR:solitaire>/sw.js
        ${CMAKE_CURRENT_SOURCE_DIR}/web/manifest.webmanifest
        ${CMAKE_CURRENT_SOURCE_DIR}/web/icon-192.png
        ${CMAKE_CURRENT_SOURCE_DIR}/web/icon-512.png
        DESTINATION .
    )
endif()

# --- Native window icon and platform packaging ---
if(WIN32)
    set(ICON_ICO_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.ico")
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/app.rc.in ${CMAKE_BINARY_DIR}/app.rc @ONLY)
    target_sources(solitaire PRIVATE ${CMAKE_BINARY_DIR}/app.rc)
    set_target_properties(solitaire PROPERTIES WIN32_EXECUTABLE TRUE)  # GUI subsystem (no console)
endif()

if(APPLE AND NOT EMSCRIPTEN)
    set_target_properties(solitaire PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Sawayama Solitaire"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.wellons.sawayamasolitaire"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.0"
        MACOSX_BUNDLE_ICON_FILE "icon.icns"
    )
    target_sources(solitaire PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.icns)
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
endif()
