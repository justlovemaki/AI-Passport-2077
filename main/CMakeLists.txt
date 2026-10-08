# Keep the embedded hotspot page and standalone editor identical.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "../configure/page.html" "../configure/profile-core.js" "../configure/app.js"
    "../configure/vendor/jsQR.js" "../configure/vendor/qrcodegen.js" "../configure/vendor/jsQR.LICENSE"
    "../tools/build_config_page.py" "../assets/fonts/Orbitron-latin.woff2" "../assets/fonts/Caveat-latin.woff2")
execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../tools/build_config_page.py"
    RESULT_VARIABLE config_page_result)
if(NOT config_page_result EQUAL 0)
    message(FATAL_ERROR "Offline configuration page generation failed")
endif()
idf_build_get_property(voice_build_dir BUILD_DIR)
file(GLOB_RECURSE voice_assets "../assets/audio/voice-keychain/*.opus")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "../tools/build_voice_pack.py" "../assets/audio/voice-keychain/voice_index.json" ${voice_assets})
execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../tools/build_voice_pack.py" "${voice_build_dir}"
    RESULT_VARIABLE voice_pack_result)
if(NOT voice_pack_result EQUAL 0)
    message(FATAL_ERROR "Voice bundle generation failed")
endif()
set(BADGE_WAKE_MODEL_DIR "${CMAKE_CURRENT_LIST_DIR}/../assets/xiaozhi/wake/wn9s_nihaoxiaozhi" CACHE PATH "Compatible, trained WakeNet9s model directory")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "../tools/build_wake_model.py" "${BADGE_WAKE_MODEL_DIR}/_MODEL_INFO_"
    "${BADGE_WAKE_MODEL_DIR}/wn9_data" "${BADGE_WAKE_MODEL_DIR}/wn9_index")
execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../tools/build_wake_model.py" "${voice_build_dir}" --source "${BADGE_WAKE_MODEL_DIR}" RESULT_VARIABLE wake_result)
if(NOT wake_result EQUAL 0)
    message(FATAL_ERROR "Wake model packaging failed")
endif()
idf_component_register(
    SRCS "main.c" "badge_power.c" "badge_navigation.c" "badge_ui.c" "badge_theme.c" "badge_header.c" "game_registry.c"
         "muyu_app.c" "muyu_logic.c" "muyu_ui.c"
         "shengbei_app.c" "muse_app.cc" "muse_style.c"
         "yao_core.c" "yao_text_data.c" "yao_service.c" "yao_time.c" "yao_location.c" "yao_geo.c" "yao_app.c" "yao_ui.c"
         "xiaozhi_wake.c" "xiaozhi_app.cc" "xiaozhi_tools.c" "badge_control.c" "badge_reminder.c" "badge_alarms.c" "badge_alarm_service.c" "profile_edit.c" "profile_text.c" "badge_alert.c" "radio_tool_catalog.cc" "xiaozhi_wire.c" "xiaozhi_buffer.c" "xiaozhi_ui.c" "xiaozhi_style.c" "xiaozhi_face_assets.c" "xiaozhi_text.c" "font_xiaozhi_14.c"
         "radio_app.cc" "radio_player.cc" "radio_catalog.cc" "radio_directory.cc" "radio_presets.cc" "radio_http.cc" "radio_logic.cc" "radio_controls.c" "radio_status.cc" "radio_ui.cc" "font_radio_14.c"
         "voice_app.c" "voice_navigation.c" "voice_stream.c" "voice_ui.c" "font_voice_14.c" "${voice_build_dir}/voice_catalog.c"
         "font_ui_14.c" "font_muyu_14.c" "font_muyu_22.c"
         "font_badge_10.c" "font_badge_28.c"
         "profile_format.c" "profile_store.c" "profile_usb.c" "profile_protocol.c" "badge_network.c" "badge_network_rules.c"
    INCLUDE_DIRS "." "${voice_build_dir}"
    REQUIRES lwip esp-sr bsp passport_muse esp_pm esp_audio_codec esp_http_client esp_hw_support esp_timer nvs_flash esp_partition esp_driver_usb_serial_jtag json mbedtls esp_wifi esp_netif esp_event esp_http_server mqtt tcp_transport
    EMBED_FILES "assets/configure.html.gz" "${voice_build_dir}/wake_model.bin"
)

if(BADGE_YAO_DEVICE_PROBE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BADGE_YAO_DEVICE_PROBE=1)
endif()
if(BADGE_ASSISTANT_DEVICE_PROBE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BADGE_ASSISTANT_DEVICE_PROBE=1)
endif()

if(BADGE_XIAOZHI_STABILITY_PROBE)
    if(NOT EXISTS "${CMAKE_BINARY_DIR}/xiaozhi_probe_pcm.h")
        message(FATAL_ERROR "Generate synthetic xiaozhi_probe_pcm.h before the stability probe")
    endif()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BADGE_XIAOZHI_STABILITY_PROBE=1)
endif()
if(BADGE_XIAOZHI_REUSE_PROBE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BADGE_XIAOZHI_REUSE_PROBE=1)
endif()
if(BADGE_WAKE_DEVICE_PROBE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BADGE_WAKE_DEVICE_PROBE=1)
endif()
if(BADGE_CONTROL_DEVICE_PROBE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BADGE_CONTROL_DEVICE_PROBE=1)
endif()
if(BADGE_RADIO_DEVICE_PROBE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BADGE_RADIO_DEVICE_PROBE=1)
endif()
if(BADGE_XIAOZHI_DEVICE_PROBE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BADGE_XIAOZHI_DEVICE_PROBE=1)
endif()
if(BADGE_XIAOZHI_DEVICE_PROBE AND BADGE_XIAOZHI_VERIFY_ALL_VOICES)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BADGE_XIAOZHI_VERIFY_ALL_VOICES=1)
endif()
if(BADGE_XIAOZHI_DEVICE_PROBE AND BADGE_XIAOZHI_POWER_PROBE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BADGE_XIAOZHI_POWER_PROBE=1)
endif()
if(BADGE_XIAOZHI_DEVICE_PROBE AND BADGE_XIAOZHI_PLAYBACK_PROBE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BADGE_XIAOZHI_PLAYBACK_PROBE=1)
endif()
if(BADGE_XIAOZHI_DEVICE_PROBE AND BADGE_XIAOZHI_CONVERSATION_PROBE)
    if(NOT EXISTS "${CMAKE_BINARY_DIR}/xiaozhi_probe_pcm.h")
        message(FATAL_ERROR "Generate build/xiaozhi_probe_pcm.h with tools/make_xiaozhi_probe_pcm.py first")
    endif()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BADGE_XIAOZHI_CONVERSATION_PROBE=1)
endif()

if(BADGE_TOOL_CLOUD_PROBE)
    if(NOT EXISTS "${CMAKE_BINARY_DIR}/cloud_probe_pcm.h")
        message(FATAL_ERROR "Generate cloud_probe_pcm.h with tools/make_tool_cloud_probe.py before enabling the cloud probe")
    endif()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BADGE_TOOL_CLOUD_PROBE=1)
endif()

if(BADGE_XIAOZHI_CONNECT_PROBE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BADGE_XIAOZHI_CONNECT_PROBE=1)
endif()

if(BADGE_BSP_POWER_PROBE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE BADGE_BSP_POWER_PROBE=1)
endif()
