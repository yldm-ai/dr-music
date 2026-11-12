find_program(DR_VCPKG NAMES vcpkg.exe vcpkg REQUIRED)
set(DR_FFMPEG_INSTALL "${PROJECT_SOURCE_DIR}/build/playback-deps" CACHE PATH "Isolated vcpkg audio dependency directory")
execute_process(COMMAND "${DR_VCPKG}" install --triplet=x64-dr-music
  "--overlay-triplets=${PROJECT_SOURCE_DIR}/cmake/triplets" "--overlay-ports=${PROJECT_SOURCE_DIR}/cmake/ports"
  "--x-install-root=${DR_FFMPEG_INSTALL}" WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" COMMAND_ERROR_IS_FATAL ANY)
set(DR_FFMPEG_ROOT "${DR_FFMPEG_INSTALL}/x64-dr-music")
# Ship the exact upstream source archive together with the applied port patches
# and build recipe alongside the dynamically linked LGPL libraries.
set(DR_FFMPEG_SOURCE_ARCHIVE "${DR_FFMPEG_INSTALL}/ffmpeg-8.1.2-source.tar.gz")
set(DR_FFMPEG_SOURCE_HASH c72f4062aecc16d8b2b1e8678d5efe3af4cfaa0cc7c0997052248f9e499e60c2463acf07877cf3b78b246ce3e8078cb043e8d97e90a6b50d06af32ff7369a788)
if(NOT EXISTS "${DR_FFMPEG_SOURCE_ARCHIVE}")
  file(DOWNLOAD https://github.com/ffmpeg/ffmpeg/archive/n8.1.2.tar.gz "${DR_FFMPEG_SOURCE_ARCHIVE}"
    EXPECTED_HASH "SHA512=${DR_FFMPEG_SOURCE_HASH}" TLS_VERIFY ON)
endif()
file(SHA512 "${DR_FFMPEG_SOURCE_ARCHIVE}" source_hash)
if(NOT source_hash STREQUAL DR_FFMPEG_SOURCE_HASH)
  message(FATAL_ERROR "FFmpeg source archive checksum mismatch")
endif()
foreach(component avformat avcodec swresample avutil)
  add_library(dr_${component} SHARED IMPORTED)
  file(GLOB component_dll "${DR_FFMPEG_ROOT}/bin/${component}-*.dll")
  set_target_properties(dr_${component} PROPERTIES IMPORTED_IMPLIB "${DR_FFMPEG_ROOT}/lib/${component}.lib"
    IMPORTED_LOCATION "${component_dll}" INTERFACE_INCLUDE_DIRECTORIES "${DR_FFMPEG_ROOT}/include")
endforeach()
FetchContent_Declare(miniaudio URL https://github.com/mackron/miniaudio/archive/refs/tags/0.11.26.tar.gz
  URL_HASH SHA256=838ba55a21c9142adf7b76bdc4351e5cd8b7ae128f46b25ec25f94ae9969c600
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR dr-music-no-cmake)
FetchContent_MakeAvailable(miniaudio)
add_library(dr_music_playback STATIC native/decoder.cpp native/playback.cpp)
target_include_directories(dr_music_playback PRIVATE "${miniaudio_SOURCE_DIR}")
target_link_libraries(dr_music_playback PUBLIC dr_music_core dr_avformat dr_avcodec dr_swresample dr_avutil PRIVATE winmm avrt)
function(dr_copy_audio_runtime target)
  foreach(component avformat avcodec swresample avutil)
    add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:dr_${component}>" "$<TARGET_FILE_DIR:${target}>" VERBATIM)
  endforeach()
endfunction()
