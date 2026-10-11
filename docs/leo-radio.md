<p align="right"><a href="leo-radio.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# LEO Radio integration — 2.0.2

The third badge mini-app adapts [leo0183/leo-radio](https://github.com/leo0183/leo-radio), commit `e28b8adafb2ca95e386e8b6e7db0db042cf09523`. It shares the badge Wi-Fi, audio device, game lifecycle and five-color theme. Muyu and Voice Keychain remain available; the 24 packs / 726 selected clips and all flash partition addresses are unchanged.

## Use

Open badge home → OK → Mini Apps → City Radio. With saved Wi-Fi connected, it discovers stations by network location and starts playback. The list combines local discoveries with 57 built-in curated MP3 streams, removing duplicate stream IDs. The curated list remains available when local discovery fails. This is internet radio: the FM scale is a visual dial, not an FM receiver.

| Control | Action |
| --- | --- |
| Up / Down | Previous / next station |
| OK | Pause / resume |
| Long Up | Open radio settings |
| Long Down | Stop playback |
| Long OK | Settings sub-pages return one level directly; at the root choose Mini Apps, Badge Home or Continue when the destinations differ |

The exit chooser explains that leaving stops playback; opening or cancelling it does not stop the stream. A direct Home-only launch exits without duplicate destinations. See [contextual return rules](setup-navigation.md).

Settings include volume (0–100%, steps of 5), automatic location or seven preset cities, a 15/30/60/90-minute stop timer. Up/down selects, OK confirms. The timer stops radio playback without powering off the badge. Volume, selected city and station are remembered in the independent `badge_radio_v1` NVS namespace; the timer resets on exit. Set Wi-Fi through the existing USB or hotspot configuration page. No separate radio account or setup page is required.

The background, panels, dial, amplitude bars, title and text follow the active badge theme, including custom palettes. All mini-apps reuse the exact home header: AI Passport, a connected Wi-Fi icon, battery percentage and divider. Wi-Fi configuration is available only in the badge system settings. The animated bars show actual decoded PCM amplitude, not frequency bands. Unknown station frequencies display NET rather than an invented frequency.

## Integration and memory

- `radio_app.cc` is the game adapter. UI objects and the refresh timer are owned under the LVGL lock; worker status uses a synchronized snapshot without touching LVGL.
- One 10 KB worker performs discovery, HTTP reads, MP3 decode, mono downmix and audio output. Cancellation closes streaming resources before the screen is destroyed. A stop timeout retains the page for another exit attempt.
- Only the MP3 decoder from pinned `espressif/esp_audio_codec` 2.5.0 is registered. The PCM buffer is 4,608 bytes (one maximum stereo MP3 frame); input is 1,024 bytes. Unsupported formats fail cleanly.
- Directory responses are split into bounded 4 KB objects and parsed one at a time. Response bytes and request duration are bounded; oversized entries are skipped. No complete directory JSON tree remains in memory.
- Muyu's 4,480-byte tone and Voice Keychain's 3,420-byte packet/PCM buffers are allocated only while their games need them, then released after workers stop. This avoids keeping inactive game buffers resident on the no-PSRAM ESP32-C3.
- The original standalone Wi-Fi manager, power controller and video Easter egg are not started. The badge continues to own network setup, sleep and navigation.

## Sources and limitations

The upstream MIT notice is preserved in `assets/radio/LICENSE.txt`; see `assets/radio/SOURCE.txt`. The decoder license is Espressif Modified MIT, restricted to Espressif products; its full notice is retained in `assets/radio/ESP_AUDIO_CODEC_LICENSE.txt`. Station directory and IP location use third-party internet services. Automatic location is approximate; station addresses and availability may change. Supported streams are directly playable MP3, not HLS/AAC. Public directory titles may be simplified to fit the included font. No broadcast audio is bundled or recorded in the source or firmware.

## Validation and installation

`./tools/validate.sh --static` covers frequency parsing, mono conversion, timer boundaries, nested navigation, fragmented/oversized JSON entries and existing badge/voice tests. `python3 tools/render_radio_host.py` renders the actual production LVGL UI in all four preset palettes and all five views, including 100 create/destroy cycles under the same 40 KB LVGL pool.

The opt-in `-D BADGE_RADIO_DEVICE_PROBE=ON` build injects bench navigation only. **Normal production builds must set `-D BADGE_RADIO_DEVICE_PROBE=OFF`**, particularly after reusing a probe build directory. The distributed image has this option disabled.

Use segmented bootloader/partition/application writes to update a 1.9.1 device. Sound partitions did not change. The merged image is for blank devices; writing it over an existing badge overwrites gaps containing user data. Keep private backups outside distributed artifacts.

## Historical 2.0.0 installed-device results, 2026-09-15

| Field | Result |
| --- | --- |
| Build | PASS — native Windows ESP-IDF 5.5.3 production build, probe OFF, merged-image and all five segment checks |
| Host tests | PASS — full static gate in WSL, radio logic/JSON/control tests and badge/726-clip regression; actual LVGL renders |
| Device tests | PASS — COM6 segmented installation with device hashes, normal badge-home startup with three games; city directory, 48 kHz stereo MP3 decode/downmix/output, pause/resume, station change and repeated returns; Muyu/Voice Keychain startup and exit |
| Unverified | Human listening/physical-key acceptance, all third-party stations and cities, long-duration timer and battery/endurance tests |

The city lookup returned two playable stations. Station-name resume was exercised across boots. Radio returned to 72,700 / 72,276 / 72,084 free heap bytes over three exits; after visiting Muyu and Voice Keychain, free heap was 72,460 bytes. These are short-run total-heap samples, not a long-term leak or endurance guarantee. Minimum observed radio worker stack headroom was 2,468 bytes; Voice Keychain reported 9,328 bytes. The radio UI host test reported 15,680 free bytes (15,616 before) and 21,520 peak usage in its 40 KB LVGL pool after 100 lifecycle cycles.

The production application is **2,066,960 bytes**, with **816,624 bytes** free in the 2,883,584-byte application partition. It is 315,344 bytes larger than 1.9.1. The merged image remains **8,192,000 bytes**, including partition gaps and padded sound regions; it is not the actual occupied application size. Sound payload remains 4,270,480 bytes and both sound images match 1.9.1 byte for byte.

Both profile regions were read back and matched their private pre-install backups byte for byte. USB readback confirmed all five profiles, active card and Wi-Fi settings unchanged, and the saved Wi-Fi connected. Normal boot reported buttons, battery, storage, profile service and USB configuration ready. The serial port was closed after verification. The distributed source and image do not contain private device backups or broadcast recordings.

Merged-image SHA-256: `a7d41d6698636940b75a2f0ffa70bec1462cf2e954d9b3b3e8a93ca67d7ce5cd`.

## 2.0.1 — Mini Apps, shared header and responsive controls

The terminal entry is now **Mini Apps**. Radio settings contain only volume, city and stop timer; Wi-Fi configuration stays in the badge system. Home, Muyu, Voice Keychain and Radio all attach `badge_header.c`, with identical title, Wi-Fi icon, battery and divider geometry. The shell continues polling network status while an app is active.

Up/down short presses act on the new BSP release event, without waiting for single/double-click classification. Duplicate click/double events are ignored after physical edges; releasing a long press does not change the station. An eight-tap stream test and six setting moves exercised these paths on the board.

Radio UI skips unchanged values and redraws only affected regions. The host renderer measured 0 pixels for an unchanged frame, 6,360/76,800 for a meter update and 10,176/76,800 for a settings selection. The connected MP3 read timeout is 100 ms with nonfatal EAGAIN handling, so cancellation checks no longer wait for the former 2.5-second read timeout. Establishing a new connection still depends on the radio server and network.

Host tests compare the first 29 pixel rows of all four screens across four palettes, disconnected/connected Wi-Fi and unknown/0/100% battery. All comparisons passed. 100 radio UI cycles returned 16,032 free bytes (16,032 before), with 21,368 peak bytes in the 40 KB pool. Board-injected release events updated selection state in 1–59 ms; the six settings moves took 26–36 ms. These timings cover event processing/state changes, not physical-button debounce or measured display scanout. Human button feel, audio quality and long-duration operation remain acceptance checks.

### 2.0.1 production installation

Build: PASS — production probe OFF, application 2,068,432 bytes, merged image 8,192,000 bytes, all image/partition checks.

Host tests: PASS — complete repository/host gate, release-event boundaries and duplicate suppression, four-screen header pixel comparisons, partial redraw checks and 100 UI cycles.

Device tests: PASS — live 48 kHz stereo MP3 playback/downmix, eight short station moves, delayed click/double suppression, long-press release suppression, three-item settings wrap and return home. COM6 production image hashes and startup passed; all five profile banks match the pre-update backup byte for byte. USB confirms the active profile and Wi-Fi settings unchanged, with Wi-Fi connected. Serial closed after verification.

Unverified: human button feel, acoustic quality and long-duration operation. The 1–59 ms timing is the injected release-to-selection-state interval. The application partition has 815,152 free bytes; audio payload and partitions are unchanged.

Production merged-image SHA-256: `fe5106f950d1148dd3daeac395f64ddb829410305786395a9251f13318eeaa5d`.

## 2.0.2 — Curated stations and menu font coverage

The directory keeps 57 curated MP3 endpoints alongside up to 10 local results. HTTP/HTTPS and Qingting/Qtfm aliases with the same live ID are deduplicated. A two-result local discovery no longer replaces the full curated list; a failed discovery preserves all curated entries.

Candidates came from a [public station directory](https://github.com/gaotianliuyun/gao/blob/master/radio.txt) and the upstream presets; per-entry provenance is recorded in `assets/radio/STATIONS.txt`. On 2026-09-15, roughly 24 KB samples were fetched from 80 candidates. 57 passed download, MP3 identification and PCM decoding; the others were excluded. Names follow the directory. Short-sample checks do not establish programme identity or continuous availability. Broadcast samples are not distributed in firmware or source.

Curated records remain in flash. The ESP32-C3 runtime directory occupies 4,252 bytes (4,264 in the 64-bit host test) for 10 local records and up to 64 preset indices; the worker stack remains 10 KB. Host tests cover merging, alias deduplication, lookalike hostnames, capacity, wraparound and recovery from empty discovery.

The second app description uses the generated `VOICE_SUMMARY`; its generator's three Chinese glyphs were absent from the original font scan. The font generator now includes that text source. Coverage tests check the actual generated summary and every curated station name. Actual LVGL rendering confirms the complete 24-category / 726-clip caption.

### 2.0.2 installation validation

Build: PASS — production probe OFF; application 2,093,568 bytes, 790,016 bytes free in its partition, merged image 8,192,000 bytes. Sound images and partition layout are unchanged.

Host tests: PASS — complete static gate, font coverage, directory boundaries, four-palette header pixel comparisons, partial redraw and 100 UI cycles; 57 downloaded stream samples decoded successfully.

Device tests: PASS — COM6 merged list contained 61 stations. Huaiji Music, Qingdao Economy and Huizhou Music decoded/output at 44.1/32/48 kHz respectively. Eight short press selections took 1–57 ms; three-setting navigation and return home passed. Segmented production flashing, hashes and normal startup passed. All five profile banks match the private backup byte for byte; active card and Wi-Fi settings are retained. Serial closed after verification.

Unverified: all 57 stations have not been individually auditioned on the board. Programme identity, long-term availability, sound quality, physical-button feel and endurance remain unchecked. Timings measure injected release-to-selection-state latency, excluding physical debounce and display scanout.

The target linker map places the 57 presets in 23,826 bytes of read-only flash and the directory in 4,252 bytes of RAM, 72 bytes above the old ten-record array. Free heap after exit was 71,760 bytes; minimum worker stack headroom was 2,476 bytes.

Production merged-image SHA-256: `e15401e8e3e2126f359286f4abf1f3524c860588fa14d63234af8eb3adf46998`.

## 2.0.3 — complete battery percentage

The compact 10 px font previously assigned percent a 6 px advance even though its ink occupied 10 px. Right-aligned battery text could therefore clip the last glyph. The generator now reserves the full percent width. The shared battery field is 32 px wide with the same right edge; the Wi-Fi icon moves 4 px left to preserve spacing. All home and mini-app headers inherit the fix.

Host validation compares 0%, 9%, 10%, 99% and 100% against a wide, unclipped reference label, including four palettes and connected/disconnected Wi-Fi. It also checks full glyph bounds, identical headers and 100 UI lifecycle cycles. Unknown battery remains `--`. This covers actual LVGL pixels, not a physical screen photograph.

2.0.3 validation: Build PASS; Host tests PASS; Device tests PASS for segmented installation, normal startup, byte-identical five-profile banks and preserved active profile/Wi-Fi. Unverified: physical screen visual acceptance. Application size remains 2,093,568 bytes. Serial released after verification.

The on-screen English title is **CITY RADIO** as of 2.6.4; upstream attribution and stored station settings are unchanged.
