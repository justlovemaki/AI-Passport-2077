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

Settings include volume (0–100%, steps of 5), automatic location or seven preset cities, a 15/30/60/90-minute stop timer. Up/down selects, OK confirms. The timer stops radio playback without powering off the badge. Volume, selected city and station are remembered in the independent `badge_radio_v1` NVS namespace; the timer resets on exit. Volume changes take effect immediately, but settings writes wait for a stream boundary such as pause, station change or exit, rather than interrupting PCM delivery. Abrupt power loss before that boundary can lose the latest unsaved adjustment. Set Wi-Fi through the existing USB or hotspot configuration page. No separate radio account or setup page is required.

The background, panels, dial, amplitude bars, title and text follow the active badge theme, including custom palettes. All mini-apps reuse the exact home header: AI Passport, a connected Wi-Fi icon, battery percentage and divider. Wi-Fi configuration is available only in the badge system settings. The animated bars show actual decoded PCM amplitude, not frequency bands. Unknown station frequencies display NET rather than an invented frequency.

## Integration and memory

- `radio_app.cc` is the game adapter. UI objects and the refresh timer are owned under the LVGL lock; worker status uses a synchronized snapshot without touching LVGL.
- Three cooperating tasks separate deadlines: a 10 KiB-stack priority-5 owner performs discovery, MP3 decode, mono downmix and meter calculations; a priority-4 HTTP reader has a 6 KiB stack and 8 KiB compressed-byte queue; a priority-6 PCM sink has a 4 KiB stack and 8 KiB PCM queue. The sink alone sets codec format/volume and writes PCM while alive. Decoding, HTTP waits, battery polling and meter calculations do not run in the sink. Cancellation joins both children before releasing their buffers, HTTP or callback context; the owner stops before the screen is destroyed. A stop timeout retains the page for another exit attempt.
- Initial compressed prefill targets 2 KiB, capped at 500 ms, rather than imposing a multi-second startup delay on every channel. The 8 KiB compressed capacity represents about 1.0 second at 64 kbit/s or 0.5 second at 128 kbit/s; it is deliberately bounded to leave receive-stack memory. Longer delivery stalls can still buffer. The PCM sink initially waits for 4 KiB or at most 100 ms, then feeds bounded 320-sample blocks. Neither queue re-arms a full-buffer wait after transient starvation. Ordinary HTTP body reads default to bounded 256-byte requests, including after app exit/re-entry; 1024-byte reads remain an explicit diagnostic comparison. The buffering indicator appears after at least 100 ms without real PCM output. `WIFI_PS_NONE` is requested during stream opening/playback and released at the stream boundary.
- Only the MP3 decoder from pinned `espressif/esp_audio_codec` 2.5.0 is registered. The decode buffer remains 4,608 bytes (one maximum stereo MP3 frame), with a 4 KiB input window. Unconsumed compressed bytes survive subsequent reads, including cached output without input consumption. Unsupported formats, invalid PCM alignment or a full no-progress input window fail cleanly. Native sample rate, 16-bit precision and mono downmix are retained. Integer five-millisecond gain ramps apply only at start, starvation/resumption and stop; ordinary samples outside these envelopes are unchanged. No filter hides decoder noise.
- `radio_buffer_budget.h` owns the application buffer budget: 8 KiB compressed, 8 KiB PCM, 4 KiB decoder input and 4,608-byte decode output, totaling 24.5 KiB before control objects and the 20 KiB of task stacks. Decoder, TLS, lwIP and display allocations are additional. A compile-time 25 KiB application-buffer limit guards against silently consuming receiver headroom. The 32 KiB compressed-buffer trial left only about 4-8 KiB free after lazy MP3/sink allocation and was withdrawn; do not budget from pre-decode heap alone. This profile returns 24 KiB relative to that trial while retaining the PCM sink. All stream allocations are released after child tasks stop, and allocation failure aborts cleanly. BSP I2S/DMA settings and other audio applications are unchanged; heap, largest block, sink stack and long-duration stability still require board acceptance.
- Enqueuing media refreshes audio activity inside the long-running stream loop. A station change must not classify a codec that was just playing as 15 seconds idle merely because the outer power tick had not run.
- Directory responses are split into bounded 4 KB objects and parsed one at a time. Response bytes and request duration are bounded; oversized entries are skipped. No complete directory JSON tree remains in memory.
- Muyu's 4,480-byte tone and Voice Keychain's 3,420-byte packet/PCM buffers are allocated only while their games need them, then released after workers stop. This avoids keeping inactive game buffers resident on the no-PSRAM ESP32-C3.
- The original standalone Wi-Fi manager, power controller and video Easter egg are not started. The badge continues to own network setup, sleep and navigation.

## Sources and limitations

The upstream MIT notice is preserved in `assets/radio/LICENSE.txt`; see `assets/radio/SOURCE.txt`. The decoder license is Espressif Modified MIT, restricted to Espressif products; its full notice is retained in `assets/radio/ESP_AUDIO_CODEC_LICENSE.txt`. Station directory and IP location use third-party internet services. Automatic location is approximate; station addresses and availability may change. Supported streams are directly playable MP3, not HLS/AAC. Public directory titles may be simplified to fit the included font. No broadcast audio is bundled or recorded in the source or firmware.

## Validation and installation

`./tools/validate.sh --static` covers frequency parsing, mono conversion, timer boundaries, nested navigation, fragmented/oversized JSON entries, concurrent queue integrity/backpressure/wrap, bounded compressed/PCM reserve models, unconsumed decoder input, and the real PCM sink with a mocked BSP/RTOS. Sink tests cover single-threaded codec ownership, delayed production, signed envelope limits, bit-identical steady PCM, short EOF drain, starvation/recovery, volume, format/task/write failures and cancellation/join. The status bridge test checks the reported buffering/playing state against the real status owner, and the power-owner test covers media activity during a long stream without outer power ticks. Host simulation is not an acoustic or target timing guarantee. Stream-end diagnostics report rebuffer events and the longest gap between one completed real-media PCM write and the start of the next. These counters are diagnostic evidence, not acoustic acceptance. A read-only protocol request `{"id":1,"op":"radio_info"}` returns version-4 playback counters through the normal USB/hotspot transport, independent of serial log delivery; maintaining the counters does not allocate memory. It does not wake the display, modify settings or expose station URLs. `session` changes per stream attempt; `active`, `httpBytes`, `httpReads`, `httpTimeouts`, `maxHttpUs`, `queuedBytes`, `pcmFrames`, `pcmBytes`, `sampleRate`, `channels`, `rebufferEvents`, `maxFeedGapUs` and `maxDecodeUs` describe that session. Version 2 adds `pcmQueuedBytes`, `pcmUnderruns`, `minFreeHeap` (sampled minimum; zero before observation) and `writerStackFree` (minimum remaining bytes, published on sink exit). Version 3 also reports current `heapFree`, `heapLargest` and the actual status-owner `playbackState` (0 stopped, 1 connecting, 2 buffering, 3 playing, 4 reconnecting, 5 error), distinguishing real starvation from stale display text. `queuedBytes` describes compressed reserve; `pcmQueuedBytes` describes decoded reserve. `pcmFrames` counts successful real-media BSP write blocks, not MP3 frames; fades/silence are excluded from PCM byte/block and media-feed-gap counters. Neither counter measures speaker output. Counters remain available after pause; fields are independent atomic observations rather than one transactional snapshot. `error` is 0 for no reported error, 1 HTTP open/status, 2 HTTP read, 3 eight-second HTTP idle, 4 allocation, 5 decoder, 6 format, 7 PCM output, 8 stalled input window or 9 startup timeout. The response also reports Wi-Fi connectivity and actual modem-sleep mode. Version 4 adds `probeMode` (0 playback, 1 priming, 2 receive-only drain), actual `readBytes`, `probeDrainedBytes`, `waitingForSpace`, `maxSpaceWaitUs`, `timeoutSocketReady`, `socketReadyChecks`, `peerFamily`, packed host-order `peerIp4`, optional IPv4 `peerAddress`, and `tls`. A readable socket immediately after a timeout can simply mean a packet arrived in between; it is not by itself proof of a client bug. Compare successful readiness-check counts, repeated observations, memory and peer addresses. On-device checks must include 32/44.1/48 kHz streams, weak Wi-Fi, volume adjustment, pause/resume, rapid station changes, repeated exits, stack/heap margins and listening for crackles at low and normal volume. `python3 tools/render_radio_host.py` renders the actual production LVGL UI in all four preset palettes and all five views, including 100 create/destroy cycles under the same 40 KB LVGL pool.

### Bounded USB receive controls

These are active diagnostics, **not** read-only queries or a claim that buffering is fixed. They accept no URL and operate only on the selected station in an initialized, connected, paused radio app after stream cleanup:

- `{"id":2,"op":"radio_receive_probe","seconds":20,"readBytes":1024}` queues one receive control; `seconds` must be 5-20 and `readBytes` 256 or 1024. The owner primes eight MP3 frames and the real codec/PCM sink allocation without enqueuing media, then retains those allocations while the same priority-4 HTTP reader drains/discards incoming data without ongoing decode, output or application-queue backpressure. The timed interval begins after priming. Check measured heap, because later lazy allocations and TCP occupancy can still differ. The UI shows `RX TEST`; no decoded programme is played or recorded. Completion stays paused. Network loss, timer expiry, input that changes the stream generation, and app exit cancel the control.
- `{"id":3,"op":"radio_read_size","readBytes":256}` selects a volatile read size for the next ordinary stream, only while paused/idle with no probe reserved. The only supported sizes are 256 and 1024; the normal default is now 256, and exit/re-entry restores it. This permits an A/B receive-granularity trial without changing credentials, saved settings or shared Wi-Fi configuration.

Both commands are USB-only and non-blocking requests; invalid or busy requests fail without starting another task or stream. `radio_info` exposes `probeAllowed`, `probeBusy` and `nextReadBytes`. A queued request does not guarantee execution if navigation/network state changes before the owner takes it. Probe admission/reservation, cancellation, queue discard and an allocated-but-unfed PCM sink are covered by host tests. These controls isolate hypotheses; neither result alone establishes an external-network fault.

### Evidence behind the deadline separation

Before the independent sink, 44.1 kHz sessions reported decode peaks near 13.1 ms and media-feed gaps near 37.6 ms, even though sampled compressed queues often had data. The shared four-by-160-frame DMA ring holds at most about 14.5 ms at 44.1 kHz, with less headroom available after a write. Other sampled intervals had no HTTP progress for several seconds, with 1.26 s and 3.03 s media gaps. Wi-Fi remained associated with `WIFI_PS_NONE`. These observations support separating decode from PCM feeding, but a later 32 KiB compressed-reserve trial showed only 4-8 KiB free heap. HTTP timeouts in a memory-starved receiver do not establish an external-network fault. The bounded 8 KiB profile restores receive headroom and short startup gating; it still needs on-device comparison and does not prove that every crackle or stall has been eliminated. A later reduced-budget device run retained at least about 30 KiB free heap with a 25 KiB largest block, yet still recorded empty compressed/PCM queues and multi-second read timeouts. A direct desktop control using the same station URL, a 5,760-byte socket receive buffer, an 8 KiB application queue and media-rate consumption ran for 20 seconds without starvation, with maximum read time about 265 ms. This narrows investigation to device-side reception/flow/load or a differing peer/path, rather than just repeating the earlier heap hypothesis. The bounded device receive control and recorded peer address exist to distinguish those cases.

Both target receive-only controls (1024-byte and 256-byte requests) completed about 20 seconds on the same recorded peer, each receiving 225,386 bytes including the source's initial burst; every sampled one-second interval made progress. Subsequent ordinary playback with the volatile 256-byte selection ran about 117 seconds with zero rebuffer events and zero PCM underruns, maximum media-feed gap 3,575 us and sampled minimum free heap 30,500 bytes. This was actual playback, not a hidden buffering label or the silent receive control. Extended observation of the same run reached about 886 seconds with one rebuffer/underrun, a maximum media gap of 539,825 us and a sampled heap minimum of 26,556 bytes. Thus the profile substantially reduced the frequent stalls but did not pass a strict long-duration zero-buffering criterion. It motivates making 256 the build default while retaining the low-memory buffers and separate sink. The time-sequential trial does not establish the exact underlying TCP/Wi-Fi mechanism, all-station availability or long-duration stability. Older installed images still reset to 1024 until updated; a volatile selection alone does not persist across exit/reboot. Keep raw traces private; do not claim buffering or acoustic acceptance until target comparison confirms it.

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
