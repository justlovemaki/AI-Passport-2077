cmake_minimum_required(VERSION 3.16)
# Runtime Opus is shared by Voice Keychain and XiaoZhi through esp_audio_codec.
# The vendored reference decoder remains available to offline bundle tools.
set(EXCLUDE_COMPONENTS opus)
# Keep ESP-IDF app metadata aligned with the UI/protocol version source.
file(READ "${CMAKE_CURRENT_LIST_DIR}/main/badge_profile.h" BADGE_PROFILE_HEADER)
string(REGEX MATCH "#define[ \t]+BADGE_VERSION[ \t]+\"([^\"]+)\"" _ "${BADGE_PROFILE_HEADER}")
if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "BADGE_VERSION is missing from main/badge_profile.h")
endif()
set(PROJECT_VER "${CMAKE_MATCH_1}")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(FoloToy-AI-Passport)

# Existing sdkconfig values override defaults. Do not accidentally ship an
# old debug configuration with the real-time audio workload.
option(BADGE_ALLOW_DEBUG_BUILD "Explicitly allow an unoptimized diagnostic build" OFF)
if(NOT CONFIG_COMPILER_OPTIMIZATION_SIZE AND NOT BADGE_ALLOW_DEBUG_BUILD)
    message(FATAL_ERROR "Badge release requires CONFIG_COMPILER_OPTIMIZATION_SIZE=y in the active sdkconfig. For intentional debugging only, set BADGE_ALLOW_DEBUG_BUILD=ON.")
endif()

# Segmented flashing skips NVS and both existing profile partitions.
idf_build_get_property(voice_build_dir BUILD_DIR)
esptool_py_flash_to_partition(flash "voice_data" "${voice_build_dir}/voice_data.bin")
esptool_py_flash_to_partition(flash "voice_tail" "${voice_build_dir}/voice_tail.bin")
