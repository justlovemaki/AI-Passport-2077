<p align="right"><a href="voice-keychain.zh_CN.md">简体中文</a> · <strong>English</strong></p>
> This is the original 1.8.0 integration/test record. For current counts and layout, see [1.9.1 audio selection](voice-pruning.md); current [return navigation](setup-navigation.md) returns internal views directly and keeps useful exit choices at the pack-list root.


# Voice Keychain integration — 1.8.0

The second badge game adapts [Shinku-Chen's feature/voice-keychain branch](https://github.com/Shinku-Chen/ai-passport/tree/feature/voice-keychain), commit `71c45cabc1b2f3b73b4929d71cafd926ccbb11f5`. It includes all 24 packs and 818 clips without re-encoding. The existing badge shell and Wooden Fish remain available.

## Controls and theme

- Game library → Voice Keychain. Up/down selects; OK enters or plays. A new request replaces current playback.
- Long OK returns one level; from the pack list it exits to the game library.
- Long Up opens volume. Up increases and Down decreases by 5%; OK returns. Long Down stops playback.
- Default volume is 40%, stored in the independent `voice_key_v1` NVS namespace. Changes save after a three-second interval when the player is idle, and at exit. Damaged NVS is never erased.
- Background, panels, accent, text and muted text follow the active badge palette, including live profile updates. Selected long titles scroll; unselected long titles use an ellipsis. Header battery text remains compact and has no BAT prefix.

## Architecture

`game_registry.c` adds `voice-keychain`. An optional `back` callback handles nested views; the existing lifecycle owns entering, starting, stopping and deleting each game. The upstream standalone boot path, USB service and auto-sleep controller are not started.

`voice_navigation.c` is a hardware-independent menu model. `voice_ui.c` uses the shared palette and six reusable rows. UI and timer access require the LVGL lock. A single worker handles streaming, fixed-point Opus decoding, PCM output, battery reads and NVS. A one-entry queue plus request generation cancels stale playback; no unbounded jobs or per-packet allocations are used. The worker has a 16 KB stack. Exit cancels playback and joins the worker before deleting resources and the UI timer; timeout retains the page for retry.

Audio uses 16 kHz mono Opus, framed as little-endian u16 packet length followed by the packet. Packet length is bounded at 1500 bytes and decoded output at 960 samples. Two raw resource partitions form a logical stream, with bounded reads across their boundary. There is no filesystem mount or automatic format.

| Region | Offset | Size |
| --- | --- | --- |
| Application | 0x10000 | 0x200000 |
| Voice data | 0x210000 | 0x480000 |
| Profiles 2–5, preserved | 0x690000 | 0xC0000 |
| Voice tail | 0x750000 | 0x80000 |
| Profile 1, preserved | 0x7D0000 | 0x30000 |

NVS and PHY retain their original addresses. The pack generator checks paths, lengths, packet boundaries and capacity, then writes a CRC-protected header, catalog and two images. The payload is 4,904,252 bytes. `verify_firmware.py` verifies all five flash images and calls the product-specific voice resource verifier. Flash resource sizes total 5 MB; unused bytes are padded with 0xFF.

## Build and preserving installation

Build with ESP-IDF 5.5.3 as described in the root README. CMake generates audio images/catalog automatically from the checked-in assets. The generated Noto Sans SC font is included; regenerating it requires Pillow and the OFL source font. Vendored libopus retains its COPYING file; media provenance is recorded separately in `assets/audio/voice-keychain/SOURCE.txt`.

```bash
python tools/flash_badge.py --port COM6 --backup-dir /private/new-backup-directory
```

The installer verifies the build, creates a private full-device backup in 1 MB chunks with device MD5 checks and up to three read attempts per chunk, checks existing user-data addresses and free space, writes only the five selected images, then compares both profile regions byte for byte. Backup files contain private data and must stay outside source and distributable directories. Release the USB configuration connection first. Do not write the merged image over an existing badge: padding covers NVS and profiles 2–5. It is for blank-device provisioning.

## Verification, 2026-09-15

| Field | Result |
| --- | --- |
| Build | PASS — native Windows ESP-IDF build, application fits 2 MB, merged image and resource checks |
| Host tests | PASS — navigation across all 818 selections, malformed/truncated packets and boundary reads, deterministic bundle CRC, existing repository/host gate, four palette renders and 100 voice/Muyu UI round trips |
| Device tests | PASS — segmented 1.8.0 installation, all profile bytes preserved, normal startup with two games, active badge and Wi-Fi settings preserved, Wi-Fi connected |
| Unverified | Physical keys, actual playback/sound quality, device decoder stack margin, repeated hardware game exits, long-term operation |

The vendored fixed-point decoder decoded all 138,458 packets (44,306,560 samples) on the host. Actual LVGL 9.5.0 rendering under a 40 KB pool reported peak usage 20,448 bytes and 17,072 free bytes after 100 voice/Muyu UI round trips, versus 16,944 before. This checks UI allocation behavior, not total ESP32 heap or device audio timing. The static gate ran in WSL; native Windows performed the equivalent firmware build/merge/verification. No single POSIX full-gate run is claimed.

### Residual bytes in an old application tail

Some devices retain bytes in unused space from earlier images. Installation rejects these by default. After inspecting the private backup, `--reuse-unused-tail` permits a recognized 1.7.1 badge migration only: partition layout, version marker, segment bounds, checksum and SHA-256 must match, and the image must end before the new sound region. Unknown or damaged/oversized applications are rejected. This device's old image was verified at 1,511,664 bytes, ending at 0x1810F0; both voice regions are outside its active image and profile storage.

### Installed-device result

The application is 1,750,832 bytes; the merged image is 8,192,000 bytes. All five written images passed device hash verification. After completing the private full-device backup, the existing application, partition table and profile banks were verified against it before segmented writing. Both profile regions were read back byte for byte without changes. Startup reports two games with buttons, battery and storage initialized; profile and USB services return ESP_OK. USB readback confirms all five profiles, the active badge and Wi-Fi settings are unchanged; Wi-Fi is connected. The serial port was closed afterward.

Merged-image SHA-256: `5daff14195cb9c16249ca8a9eedcc0d6adbef0f9280b0ba9192406c800f06fc7`.

## Shared codec in 2.1.0

Voice Keychain and XiaoZhi now use the same Espressif `esp_audio_codec` Opus decoder. Clip data, packet bounds and volume persistence are unchanged. The vendored libopus decoder is retained for offline tools and excluded from the firmware link. See [XiaoZhi validation](xiaozhi.md).
