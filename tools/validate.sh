#!/usr/bin/env bash
set -euo pipefail

mode="${1:---all}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 [--all|--static|--firmware]" >&2
}

run_static_checks() {
    local actionlint_bin
    local test_dir

    python3 tools/check_repo.py
    python3 tests/test_wake_model.py
    python3 tests/test_upgrade_package.py
    python3 tests/test_pipeline.py

    actionlint_bin="${ACTIONLINT_BIN:-}"
    if [[ -z "${actionlint_bin}" ]]; then
        actionlint_bin="$(command -v actionlint || true)"
    fi
    if [[ -z "${actionlint_bin}" || ! -x "${actionlint_bin}" ]]; then
        actionlint_bin="$(./tools/install-actionlint.sh)"
    fi
    "${actionlint_bin}" -color .github/workflows/*.yml

    test_dir="$(mktemp -d /tmp/ai-passport-host-tests.XXXXXX)"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -DBADGE_WAKE_HOST_TEST -Itests/power_stubs -Itests -Imain tests/test_xiaozhi_wake_runtime.c main/xiaozhi_wake.c -o "${test_dir}/test_xiaozhi_wake_runtime"
    "${test_dir}/test_xiaozhi_wake_runtime"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_ui_pixel_math.c main/ui_pixel_math.c \
        -o "${test_dir}/test_ui_pixel_math"
    "${test_dir}/test_ui_pixel_math"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_demo_navigation.c main/demo_navigation.c \
        -o "${test_dir}/test_demo_navigation"
    "${test_dir}/test_demo_navigation"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_badge_volume.c -o "${test_dir}/test_badge_volume"
    "${test_dir}/test_badge_volume"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_muyu_logic.c main/muyu_logic.c \
        -o "${test_dir}/test_muyu_logic"
    "${test_dir}/test_muyu_logic"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/muyu_stubs -Itests/profile_stubs -Imain \
        tests/test_muyu_volume_store.c main/muyu_volume_store.c main/muyu_logic.c \
        -o "${test_dir}/test_muyu_volume_store"
    "${test_dir}/test_muyu_volume_store"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain tests/test_yao_core.c main/yao_core.c main/yao_text_data.c -o "${test_dir}/test_yao_core"
    "${test_dir}/test_yao_core"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain tests/test_yao_time.c main/yao_time.c -lm -o "${test_dir}/test_yao_time"
    "${test_dir}/test_yao_time"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -DBADGE_CONTROL_HOST_TEST -Imain -Itests/yao_stubs tests/test_yao_location.c main/yao_location.c main/yao_time.c -lm -o "${test_dir}/test_yao_location"
    "${test_dir}/test_yao_location"
    node tests/test_yao_config.cjs
    node tests/test_muse_config_ui.cjs
    node tests/test_config_backup_ui.cjs
    node tests/test_config_backup_flow.cjs
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_badge_navigation.c main/badge_navigation.c \
        -o "${test_dir}/test_badge_navigation"
    "${test_dir}/test_badge_navigation"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_badge_return_input.c -o "${test_dir}/test_badge_return_input"
    "${test_dir}/test_badge_return_input"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain -Itests/profile_stubs \
        tests/test_profile_store.c main/profile_store.c main/profile_format.c \
        -o "${test_dir}/test_profile_store"
    "${test_dir}/test_profile_store"
    cc -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_badge_network_rules.c main/badge_network_rules.c \
        -o "${test_dir}/test_badge_network_rules"
    "${test_dir}/test_badge_network_rules"
    "${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -Imain -Itests \
        tests/test_badge_network_runtime.c main/badge_network_rules.c \
        -o "${test_dir}/test_badge_network_runtime"
    "${test_dir}/test_badge_network_runtime"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_display_rounding.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_display_rounding"
    "${test_dir}/test_bsp_display_rounding"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_capture_stream.c -o "${test_dir}/test_bsp_capture_stream"
    "${test_dir}/test_bsp_capture_stream"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_es8311_sleep_check.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_es8311_sleep_check"
    "${test_dir}/test_bsp_es8311_sleep_check"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/audio_stubs -Icomponents/bsp/include -Icomponents/bsp/src tests/test_bsp_audio_recovery.c components/bsp/src/bsp_es8311_sleep_check.c -o "${test_dir}/test_bsp_audio_recovery"
    "${test_dir}/test_bsp_audio_recovery"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/bsp_stubs -Icomponents/bsp/include -Icomponents/bsp/src tests/test_bsp_lvgl_init.c components/bsp/src/bsp_display_rounding.c -o "${test_dir}/test_bsp_lvgl_init"
    "${test_dir}/test_bsp_lvgl_init"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/bsp_stubs -Icomponents/bsp/include tests/test_bsp_button_recovery.c -o "${test_dir}/test_bsp_button_recovery"
    "${test_dir}/test_bsp_button_recovery"


    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain -c main/radio_controls.c -o "${test_dir}/radio_controls.o"
    "${CXX:-c++}" -std=c++17 -Wall -Wextra -Imain tests/test_radio.cc main/radio_logic.cc "${test_dir}/radio_controls.o" -o "${test_dir}/test_radio"
    "${test_dir}/test_radio"
    "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread -Imain \
        tests/test_radio_stream.cc -o "${test_dir}/test_radio_stream"
    "${test_dir}/test_radio_stream"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain -c \
        main/radio_diagnostics.c -o "${test_dir}/radio_diagnostics.o"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_radio_diagnostics.c "${test_dir}/radio_diagnostics.o" -o "${test_dir}/test_radio_diagnostics"
    "${test_dir}/test_radio_diagnostics"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_radio_probe.c main/radio_probe.c -o "${test_dir}/test_radio_probe"
    "${test_dir}/test_radio_probe"
    "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread -DRADIO_PCM_HOST_TEST -Imain -Itests \
        tests/test_radio_pcm_output.cc main/radio_pcm_output.cc "${test_dir}/radio_diagnostics.o" \
        -o "${test_dir}/test_radio_pcm_output"
    "${test_dir}/test_radio_pcm_output"
    "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Imain -Itests/yao_stubs \
        tests/test_radio_status.cc main/radio_status.cc -o "${test_dir}/test_radio_status"
    "${test_dir}/test_radio_status"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain tests/test_badge_power.c -o "${test_dir}/test_badge_power"
    "${test_dir}/test_badge_power"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain -Itests/power_stubs tests/test_badge_power_runtime.c main/badge_power.c -o "${test_dir}/test_badge_power_runtime"
    "${test_dir}/test_badge_power_runtime"
    "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Imain tests/test_radio_directory.cc main/radio_directory.cc main/radio_presets.cc -o "${test_dir}/test_radio_directory"
    "${test_dir}/test_radio_directory"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain tests/test_xiaozhi.c main/xiaozhi_wire.c -o "${test_dir}/test_xiaozhi"
    "${test_dir}/test_xiaozhi"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain tests/test_xiaozhi_text.c main/xiaozhi_text.c -o "${test_dir}/test_xiaozhi_text"
    "${test_dir}/test_xiaozhi_text"
    python3 tools/build_voice_pack.py "${test_dir}"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain -I"${test_dir}" \
        tests/test_voice.c main/voice_navigation.c main/voice_stream.c "${test_dir}/voice_catalog.c" \
        -o "${test_dir}/test_voice"
    "${test_dir}/test_voice"
    python3 tests/test_font_resources.py
    python3 tests/test_face_symmetry.py
    python3 tests/test_yao_request.py
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain tests/test_face_clock.c -o "${test_dir}/test_face_clock"
    "${test_dir}/test_face_clock"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain tests/test_face_variants.c -o "${test_dir}/test_face_variants"
    "${test_dir}/test_face_variants"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain tests/test_face_transition.c -o "${test_dir}/test_face_transition"
    "${test_dir}/test_face_transition"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain tests/test_face_rle.c -o "${test_dir}/test_face_rle"
    "${test_dir}/test_face_rle"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain -Itests/style_stubs tests/test_xiaozhi_style.c main/xiaozhi_style.c -o "${test_dir}/test_xiaozhi_style"
    "${test_dir}/test_xiaozhi_style"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain tests/test_xiaozhi_buffer.c main/xiaozhi_buffer.c -o "${test_dir}/test_xiaozhi_buffer"
    "${test_dir}/test_xiaozhi_buffer"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain tests/test_badge_reminder.c main/badge_reminder.c -o "${test_dir}/test_badge_reminder"
    "${test_dir}/test_badge_reminder"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain tests/test_badge_alarms.c main/badge_alarms.c -o "${test_dir}/test_badge_alarms"
    "${test_dir}/test_badge_alarms"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain tests/test_profile_text.c main/profile_text.c -o "${test_dir}/test_profile_text"
    "${test_dir}/test_profile_text"
    python3 tests/test_menu_font.py "${test_dir}"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/passport_muse/sdk -Icomponents/passport_muse/include \
        tests/test_muse_voice.c components/passport_muse/voice_helpers.c \
        -o "${test_dir}/test_muse_voice"
    "${test_dir}/test_muse_voice"
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_voice_pack.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_flash_badge.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_deep_sleep_contract.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_check_repo.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_verify_firmware.py
    rm -rf "${test_dir}"
    echo "Host tests: PASS"
}

run_firmware_checks() (
    local validation_build_dir

    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ERROR: idf.py is not available; activate ESP-IDF 5.5.3 first." >&2
        return 1
    fi

    validation_build_dir="$(mktemp -d /tmp/ai-passport-firmware.XXXXXX)"
    trap 'case "${validation_build_dir}" in /tmp/ai-passport-firmware.*) rm -rf -- "${validation_build_dir}" ;; esac' EXIT

    SDKCONFIG_DEFAULTS="${repo_root}/sdkconfig.defaults" \
        idf.py -B "${validation_build_dir}" \
        -D "SDKCONFIG=${validation_build_dir}/sdkconfig" build
    idf.py -B "${validation_build_dir}" merge-bin \
        -o "${validation_build_dir}/FoloToy-AI-Passport-full.bin"
    python3 tools/verify_firmware.py "${validation_build_dir}"
    # Publish one internally consistent, gate-verified artifact set. The
    # preserving installer consumes segmented files; leaving stale root-build
    # segments beside a fresh merged image can flash a different configuration.
    mkdir -p "${repo_root}/build/bootloader" "${repo_root}/build/partition_table"
    install -m 0644 "${validation_build_dir}/bootloader/bootloader.bin" \
        "${repo_root}/build/bootloader/bootloader.bin"
    install -m 0644 "${validation_build_dir}/partition_table/partition-table.bin" \
        "${repo_root}/build/partition_table/partition-table.bin"
    for name in FoloToy-AI-Passport.bin FoloToy-AI-Passport-full.bin \
                voice_data.bin voice_tail.bin flash_args flasher_args.json voice_catalog.h; do
        install -m 0644 "${validation_build_dir}/${name}" "${repo_root}/build/${name}"
    done
    # Re-run verification against the published set, not only its temporary
    # source. An interrupted copy remains fail-closed for tools/flash_badge.py.
    python3 tools/verify_firmware.py "${repo_root}/build"
    echo "Firmware build: PASS"
)

cd "${repo_root}"
case "${mode}" in
    --all)
        run_static_checks
        run_firmware_checks
        ;;
    --static)
        run_static_checks
        ;;
    --firmware)
        run_firmware_checks
        ;;
    *)
        usage
        exit 2
        ;;
esac

