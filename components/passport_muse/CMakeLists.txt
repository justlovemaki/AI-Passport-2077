idf_component_register(
    SRCS "passport_muse.cc" "config_store.c" "compat.c" "voice_helpers.c"
         "sdk/ble_server.c" "sdk/link_pairing.c"
         "sdk/pairing_signer_policy.c" "sdk/pairing_transcript.c"
         "sdk/vm_api.c" "sdk/noise_control.cpp" "sdk/stack_monitor.c" "sdk/http_proxy.c"
         "sdk/muse_chat_link.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "compat" "sdk" "${CMAKE_CURRENT_BINARY_DIR}"
    REQUIRES bsp json nvs_flash
    PRIV_REQUIRES bt esp_wifi esp_timer esp-tls esp_http_client mbedtls
                  noise_core esp_app_format esp_driver_gpio efuse)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)

# Every distributed build starts without a maintainer's network configuration.
# A local device build may opt into a LAN proxy without committing that
# machine-specific address to the source tree. Explicit -D values win; the
# environment variables keep the existing local build workflow reproducible.
if(PASSPORT_PUBLIC_RELEASE)
    set(MUSE_DEFAULT_PROXY_HOST "")
    set(MUSE_DEFAULT_PROXY_PORT 0)
endif()
if(NOT DEFINED MUSE_DEFAULT_PROXY_HOST)
    set(MUSE_DEFAULT_PROXY_HOST "$ENV{MUSE_HTTP_PROXY_HOST}")
endif()
if(NOT DEFINED MUSE_DEFAULT_PROXY_PORT)
    if(DEFINED ENV{MUSE_HTTP_PROXY_PORT} AND NOT "$ENV{MUSE_HTTP_PROXY_PORT}" STREQUAL "")
        set(MUSE_DEFAULT_PROXY_PORT "$ENV{MUSE_HTTP_PROXY_PORT}")
    else()
        set(MUSE_DEFAULT_PROXY_PORT 0)
    endif()
endif()
if(NOT MUSE_DEFAULT_PROXY_HOST MATCHES "^[0-9.]*$")
    message(FATAL_ERROR "MUSE_DEFAULT_PROXY_HOST must be an IPv4 address or empty")
endif()
if(NOT MUSE_DEFAULT_PROXY_PORT MATCHES "^[0-9]+$")
    message(FATAL_ERROR "MUSE_DEFAULT_PROXY_PORT must be numeric")
endif()
configure_file("sdk/muse_proxy_defaults.h.in" "muse_proxy_defaults.h" @ONLY)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    CONFIG_MUSE_ENABLED=1 CONFIG_HOMEHUB_TUNNEL=0
    CONFIG_HOMEHUB_SHOW_AGENT_NAME=1 CONFIG_HOMEHUB_DISPLAY_COMMANDS=0
    CONFIG_HOMEHUB_PAIRING_AUTH_EPOCH=0)
