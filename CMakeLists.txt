cmake_minimum_required(VERSION 3.25)
project(dr_music VERSION 0.2.0 LANGUAGES C CXX RC)
set(CMAKE_VS_GLOBALS "VcpkgEnabled=false;VcpkgEnableManifest=false")
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
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(zlib URL https://zlib.net/fossils/zlib-1.4.tar.gz
  URL_HASH SHA256=02dd159783677a749c04adbdd02b55b8a2bcc7e054f51b76b549199a916ad584
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE OVERRIDE_FIND_PACKAGE)
FetchContent_MakeAvailable(zlib)
add_library(ZLIB::ZLIB ALIAS zlibstatic)
set(WITH_ZLIB ON CACHE BOOL "" FORCE)
FetchContent_Declare(taglib URL https://taglib.org/releases/taglib-2.3.2.tar.gz
  URL_HASH SHA256=3ca2d8afaa7f1cf7f6ed10e511ebc368bfacd6dcaa3dbfa690b89e502e8963dc
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(taglib)
# OpenCC (Apache-2.0) folds simplified/traditional Chinese for library search; same setup as
# ../dr-dict/CMakeLists.txt. Its build compiles the .ocd2 dictionaries from text sources (Python 3).
FetchContent_Declare(opencc URL https://github.com/BYVoid/OpenCC/archive/refs/tags/ver.1.4.2.tar.gz
  URL_HASH SHA256=8e5f5cf7fe195bd9b9be851adc9738c1ef7dc5c24441dd5878a56db4087a9a70
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
set(BUILD_PYTHON OFF CACHE BOOL "" FORCE)
set(ENABLE_GTEST OFF CACHE BOOL "" FORCE)
set(ENABLE_BENCHMARK OFF CACHE BOOL "" FORCE)
set(BUILD_OPENCC_JIEBA_PLUGIN OFF CACHE BOOL "" FORCE)
set(OPENCC_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(opencc)
# Only the traditional→simplified chains are needed; configs and dictionaries sit side by side in
# bin/<config>/opencc because a config names its dictionaries relative to its own folder.
set(DR_OPENCC_DIR "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/opencc")
set(DR_OPENCC_COPY "")
foreach(config t2s tw2s tw2sp hk2s)
  list(APPEND DR_OPENCC_COPY "${opencc_SOURCE_DIR}/data/config/${config}.json")
endforeach()
add_custom_target(dr-music-opencc-data
  COMMAND ${CMAKE_COMMAND} -E make_directory "${DR_OPENCC_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${DR_OPENCC_COPY} "${DR_OPENCC_DIR}"
  COMMAND ${CMAKE_COMMAND} "-DSOURCE=${opencc_BINARY_DIR}/data" "-DDEST=${DR_OPENCC_DIR}" -P "${PROJECT_SOURCE_DIR}/cmake/copy_ocd2.cmake"
  COMMENT "Copying OpenCC configs and dictionaries"
  VERBATIM)
add_dependencies(dr-music-opencc-data Dictionaries)
# Same pinned SDK and static loader pattern as ../dr-dict/app/CMakeLists.txt.
FetchContent_Declare(webview2 URL https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.2903.40 DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(webview2)
add_library(dr_music_core STATIC native/common.cpp native/library.cpp native/media.cpp native/cover.cpp native/fonts.cpp native/chinese.cpp)
target_include_directories(dr_music_core PUBLIC native "${taglib_SOURCE_DIR}/taglib" "${taglib_SOURCE_DIR}/taglib/toolkit" "${taglib_BINARY_DIR}" "${taglib_BINARY_DIR}/taglib")
target_link_libraries(dr_music_core PUBLIC nlohmann_json::nlohmann_json tag bcrypt ole32 shlwapi windowscodecs dwrite)
target_link_libraries(dr_music_core PRIVATE OpenCC::OpenCC)
add_library(dr_music_shell STATIC native/window.cpp)
target_include_directories(dr_music_shell PUBLIC native)
target_link_libraries(dr_music_shell PUBLIC d2d1 dwrite dwmapi user32 gdi32)
add_executable(dr-music WIN32 native/main.cpp native/application.cpp native/webview.cpp native/system_media.cpp native/app.rc)
target_link_libraries(dr-music PRIVATE windowsapp)
include(cmake/Playback.cmake)
target_link_libraries(dr-music PRIVATE dr_music_playback)
dr_copy_audio_runtime(dr-music)
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
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${zlib_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:dr-music>/licenses/native/zlib-LICENSE"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${taglib_SOURCE_DIR}/3rdparty/utfcpp/LICENSE" "$<TARGET_FILE_DIR:dr-music>/licenses/native/utfcpp-LICENSE"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${webview2_SOURCE_DIR}/LICENSE.txt" "$<TARGET_FILE_DIR:dr-music>/licenses/native/WebView2-LICENSE.txt"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${miniaudio_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:dr-music>/licenses/native/miniaudio-LICENSE"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${opencc_SOURCE_DIR}/LICENSE" "$<TARGET_FILE_DIR:dr-music>/licenses/native/OpenCC-LICENSE"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${opencc_SOURCE_DIR}/deps/marisa-0.3.1/COPYING.md" "$<TARGET_FILE_DIR:dr-music>/licenses/native/marisa-COPYING.md"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${opencc_SOURCE_DIR}/deps/darts-clone-0.32h/COPYING.md" "$<TARGET_FILE_DIR:dr-music>/licenses/native/darts-clone-COPYING.md"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${DR_FFMPEG_ROOT}/share/ffmpeg" "$<TARGET_FILE_DIR:dr-music>/licenses/native/ffmpeg"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DR_FFMPEG_SOURCE_ARCHIVE}" "$<TARGET_FILE_DIR:dr-music>/licenses/native/ffmpeg/ffmpeg-8.1.2-source.tar.gz"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/cmake/ports/ffmpeg" "$<TARGET_FILE_DIR:dr-music>/licenses/native/ffmpeg/build-recipe/port"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/cmake/triplets/x64-dr-music.cmake" "${PROJECT_SOURCE_DIR}/vcpkg.json" "${PROJECT_SOURCE_DIR}/cmake/ports/README.md" "${PROJECT_SOURCE_DIR}/cmake/ports/vcpkg-LICENSE.txt" "$<TARGET_FILE_DIR:dr-music>/licenses/native/ffmpeg/build-recipe"
  DEPENDS "${CMAKE_BINARY_DIR}/frontend.stamp" VERBATIM)
add_dependencies(dr-music dr-music-assets dr-music-opencc-data)
if(BUILD_TESTING)
  find_program(DR_NODE NAMES node REQUIRED)
  find_program(DR_TEST_FFMPEG NAMES ffmpeg REQUIRED)
  add_executable(dr-music-playback-tests tests/playback_tests.cpp)
  target_link_libraries(dr-music-playback-tests PRIVATE dr_music_playback)
  dr_copy_audio_runtime(dr-music-playback-tests)
  add_test(NAME native-playback-decode-and-transport COMMAND "${DR_NODE}" "${PROJECT_SOURCE_DIR}/tests/playback_fixtures.mjs" "$<TARGET_FILE:dr-music-playback-tests>" "${DR_TEST_FFMPEG}")
  add_executable(dr-music-tests tests/native_tests.cpp)
  target_link_libraries(dr-music-tests PRIVATE dr_music_core dr_music_shell)
  target_compile_definitions(dr-music-tests PRIVATE "DR_TEST_OPENCC_DIR=L\"${DR_OPENCC_DIR}\"")
  add_dependencies(dr-music-tests dr-music-opencc-data)
  add_test(NAME native-library-media-and-caption COMMAND dr-music-tests)
  add_executable(dr-music-metadata-tests tests/metadata_tests.cpp)
  target_link_libraries(dr-music-metadata-tests PRIVATE dr_music_core)
  add_test(NAME native-metadata-and-covers COMMAND dr-music-metadata-tests)
  add_test(NAME frontend-lyrics-and-queue COMMAND "${DR_NPM}" test)
  set_tests_properties(frontend-lyrics-and-queue PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
endif()
install(TARGETS dr-music RUNTIME DESTINATION . COMPONENT Runtime)
foreach(component avformat avcodec swresample avutil)
  install(FILES "$<TARGET_FILE:dr_${component}>" DESTINATION . COMPONENT Runtime)
endforeach()
install(DIRECTORY "${PROJECT_SOURCE_DIR}/dist/" DESTINATION web COMPONENT Runtime)
install(FILES LICENSE THIRD_PARTY_NOTICES.md README.md DESTINATION . COMPONENT Runtime)
install(DIRECTORY "$<TARGET_FILE_DIR:dr-music>/licenses/" DESTINATION licenses COMPONENT Runtime)
install(DIRECTORY docs/upstream DESTINATION docs COMPONENT Runtime)
install(DIRECTORY "${DR_OPENCC_DIR}" DESTINATION . COMPONENT Runtime)
