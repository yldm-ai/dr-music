cmake_minimum_required(VERSION 3.25)
project(dr_music VERSION 0.2.0 LANGUAGES C CXX RC)
if(NOT WIN32)
  message(FATAL_ERROR "DR Music native host currently requires Windows")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
include(FetchContent)
include(CTest)
add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
if(MSVC)
  add_compile_options(/utf-8 /W4 /permissive- /EHsc)
endif()
FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(json)
set(BUILD_BINDINGS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(WITH_ZLIB OFF CACHE BOOL "" FORCE)
FetchContent_Declare(taglib URL https://taglib.org/releases/taglib-2.1.1.tar.gz DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(taglib)
# Same pinned SDK and static loader pattern as ../dr-dict/app/CMakeLists.txt.
FetchContent_Declare(webview2 URL https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.2903.40 DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(webview2)
add_library(dr_music_core STATIC native/common.cpp native/library.cpp native/media.cpp)
target_include_directories(dr_music_core PUBLIC native "${taglib_SOURCE_DIR}/taglib" "${taglib_SOURCE_DIR}/taglib/toolkit" "${taglib_BINARY_DIR}" "${taglib_BINARY_DIR}/taglib")
target_link_libraries(dr_music_core PUBLIC nlohmann_json::nlohmann_json tag bcrypt ole32 shlwapi)
add_library(dr_music_shell STATIC native/window.cpp)
target_include_directories(dr_music_shell PUBLIC native)
target_link_libraries(dr_music_shell PUBLIC d2d1 dwrite dwmapi user32 gdi32)
add_executable(dr-music WIN32 native/main.cpp native/application.cpp native/webview.cpp native/app.rc)
target_include_directories(dr-music PRIVATE "${webview2_SOURCE_DIR}/build/native/include")
target_link_libraries(dr-music PRIVATE dr_music_core dr_music_shell "${webview2_SOURCE_DIR}/build/native/x64/WebView2LoaderStatic.lib" shell32 comdlg32 version dcomp)
target_link_options(dr-music PRIVATE /MANIFEST:NO)
set_target_properties(dr-music PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/Release")
find_program(DR_NPM NAMES npm.cmd npm REQUIRED)
file(GLOB_RECURSE DR_WEB_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/*.ts" "${PROJECT_SOURCE_DIR}/src/*.tsx" "${PROJECT_SOURCE_DIR}/src/*.css")
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/frontend.stamp"
  COMMAND "${DR_NPM}" run build
  COMMAND "${DR_NPM}" run notices
  COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_BINARY_DIR}/frontend.stamp"
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  DEPENDS ${DR_WEB_SOURCES} index.html package.json package-lock.json tsconfig.json vite.config.ts scripts/licenses.mjs
  VERBATIM)
add_custom_target(dr-music-assets
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/dist" "$<TARGET_FILE_DIR:dr-music>/web"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" "$<TARGET_FILE_DIR:dr-music>"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/licenses" "$<TARGET_FILE_DIR:dr-music>/licenses"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/docs/upstream" "$<TARGET_FILE_DIR:dr-music>/docs/upstream"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:dr-music>/licenses/native"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${json_SOURCE_DIR}/LICENSE.MIT" "$<TARGET_FILE_DIR:dr-music>/licenses/native/json-LICENSE.MIT"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${taglib_SOURCE_DIR}/COPYING.LGPL" "${taglib_SOURCE_DIR}/COPYING.MPL" "$<TARGET_FILE_DIR:dr-music>/licenses/native"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${taglib_SOURCE_DIR}/3rdparty/utfcpp/LICENSE" "$<TARGET_FILE_DIR:dr-music>/licenses/native/utfcpp-LICENSE"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${webview2_SOURCE_DIR}/LICENSE.txt" "$<TARGET_FILE_DIR:dr-music>/licenses/native/WebView2-LICENSE.txt"
  DEPENDS "${CMAKE_BINARY_DIR}/frontend.stamp" VERBATIM)
add_dependencies(dr-music dr-music-assets)
if(BUILD_TESTING)
  add_executable(dr-music-tests tests/native_tests.cpp)
  target_link_libraries(dr-music-tests PRIVATE dr_music_core dr_music_shell)
  add_test(NAME native-library-media-and-caption COMMAND dr-music-tests)
  add_test(NAME frontend-lyrics-and-queue COMMAND "${DR_NPM}" test)
  set_tests_properties(frontend-lyrics-and-queue PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
endif()
install(TARGETS dr-music RUNTIME DESTINATION . COMPONENT Runtime)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/dist/" DESTINATION web COMPONENT Runtime)
install(FILES LICENSE THIRD_PARTY_NOTICES.md README.md DESTINATION . COMPONENT Runtime)
install(DIRECTORY "$<TARGET_FILE_DIR:dr-music>/licenses/" DESTINATION licenses COMPONENT Runtime)
install(DIRECTORY docs/upstream DESTINATION docs COMPONENT Runtime)
