cmake_minimum_required(VERSION 3.24)

project(LightBrowser VERSION 3.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "LightBrowser requires Windows.")
endif()

if(NOT DEFINED WEBVIEW2_DIR)
    message(FATAL_ERROR "WEBVIEW2_DIR was not supplied.")
endif()

set(WEBVIEW2_INCLUDE_DIR "\${WEBVIEW2_DIR}/build/native/include")
set(WEBVIEW2_LIB_DIR "\${WEBVIEW2_DIR}/build/native/x64")
set(WEBVIEW2_LOADER_DLL "\${WEBVIEW2_DIR}/build/native/x64/WebView2Loader.dll")

if(NOT EXISTS "\${WEBVIEW2_INCLUDE_DIR}/WebView2.h")
    message(FATAL_ERROR "WebView2.h was not found in \${WEBVIEW2_INCLUDE_DIR}")
endif()

if(NOT EXISTS "\${WEBVIEW2_LIB_DIR}/WebView2LoaderStatic.lib")
    message(FATAL_ERROR "WebView2LoaderStatic.lib was not found in \${WEBVIEW2_LIB_DIR}")
endif()

add_executable(LightBrowser WIN32 src/main.cpp src/LightBrowser.manifest)

target_include_directories(LightBrowser PRIVATE "\${WEBVIEW2_INCLUDE_DIR}")

target_compile_definitions(LightBrowser PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)

target_compile_options(LightBrowser PRIVATE
    /W4 /EHsc /sdl /guard:cf
)

target_link_options(LightBrowser PRIVATE
    /DYNAMICBASE /NXCOMPAT /GUARD:CF /CETCOMPAT
)

target_link_libraries(LightBrowser PRIVATE
    "\${WEBVIEW2_LIB_DIR}/WebView2LoaderStatic.lib"
    user32 gdi32 ole32 oleaut32 shlwapi comctl32 shell32
)

set_target_properties(LightBrowser PROPERTIES OUTPUT_NAME "LightBrowser")

add_custom_command(
    TARGET LightBrowser POST_BUILD
    COMMAND \${CMAKE_COMMAND} -E copy_if_different
        "\${WEBVIEW2_LOADER_DLL}"
        "$<TARGET_FILE_DIR:LightBrowser>/WebView2Loader.dll"
)
