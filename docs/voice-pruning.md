<p align="right"><a href="voice-pruning.zh_CN.md">简体中文</a> · <strong>English</strong></p>

## Current selection — 2.6.18

The user retained review IDs 4, 7, 8, 14, 19, 24, 25 and 29 from the 39 long-clip listening previews. Only the other 31 reviewed clips were removed; all shorter upstream clips remain. That selected library has 24 packs and 695 clips totaling 3,293,101 bytes; all retained files are byte-identical. The current catalog additionally includes a 14-clip High-Energy BGM pack embedded in the application, for 25 packs and 709 clips overall. The two voice partitions still contain only the selected upstream library and retain 110,675 bytes after the 4,096-byte header. `assets/audio/voice-keychain/long-selection.json` records the selection and original hashes. The following 1.9.1 section is historical.

# Audio selection — 1.9.1

The user approved keeping one representative in each of the 46 reviewed similar-title groups. Prefer the unnumbered base title, otherwise the lowest numeric suffix; semantic groups use the explicit representative in `assets/audio/voice-keychain/selection.json`. No acoustic-equivalence or sound-quality claim is made. The 92 removed recordings and original catalog are privately backed up outside the deliverables. All retained recordings are byte-identical, without re-encoding. Pack order and source file paths remain stable; clip counts and generated offsets are rebuilt.

- Before: 24 packs, 818 clips, 4,904,252 audio bytes.
- After: 24 packs, 726 clips, 4,270,480 audio bytes.
- Saved: 633,772 bytes (618.918 KiB).
- App image: 1,751,616 bytes; app capacity: 2,883,584 bytes (2.75 MiB).
- App space remaining: 1,131,968 bytes (1.080 MiB).
- Voice capacity: 4,456,448 bytes (4.25 MiB), including a 4,096-byte header; remaining padding: 181,872 bytes (177.609 KiB).

## Layout and update

| Partition | Offset | Size |
| --- | --- | --- |
| nvs | 0x9000 | 0x6000 |
| phy_init | 0xF000 | 0x1000 |
| factory | 0x10000 | 0x2C0000 |
| voice_data | 0x2D0000 | 0x3C0000 |
| badge_slots | 0x690000 | 0xC0000 |
| voice_tail | 0x750000 | 0x80000 |
| badge_user | 0x7D0000 | 0x30000 |

The application gains 768 KiB by combining savings with existing voice padding. NVS, PHY and all five profile banks retain their addresses and sizes. The resource reader uses generated partition sizes; UI counts come from the generated catalog. The display font is regenerated from retained titles.

The merged image remains 8,192,000 bytes because it retains the same highest resource address and fills address gaps. Its size is not a measure of occupied program space. Never use it for an update that must preserve user data.

Use `python tools/flash_badge.py --port COM6 --backup-dir /private/new-directory` after building. It verifies the new images, backs up all 8 MiB, accepts recognized 1.8/1.9 or 1.9.1 layouts, writes the five segments, then reads both profile regions back byte for byte. An application-only flash is insufficient because both voice data and its mapping changed. The original 1.7.1 migration remains subject to its existing validation.

## Validation

- Build: PASS — native ESP-IDF 5.5.3 build, all five image mappings and both voice CRCs verified.
- Host tests: PASS — complete static gate and three configuration-page Node suites; all 726 clips decoded (119,735 packets / 38,315,200 samples). Actual LVGL renders show the updated count. 100 voice/Muyu/return-menu cycles: 17,000 free bytes vs 16,984 before, peak 22,680 bytes in a 40 KiB UI pool.
- Device tests: PASS — full private 8 MiB backup; five segments written with device hash verification; both profile regions read back byte-for-byte unchanged. Post-boot USB readback confirms all five profiles, active badge and saved Wi-Fi settings unchanged, and Wi-Fi connected. Serial port released. The boot-log capture missed early startup lines; successful post-boot protocol responses establish that the application is running.
- Unverified: physical button/audio quality acceptance. No acoustic comparison was used to select representatives.

The static gate used WSL; firmware build/merge/verification used native Windows ESP-IDF. This is not a single POSIX full-gate invocation.

2.6.19 subsequently reallocates 896 KiB of the reclaimed voice capacity to the application. Voice capacity becomes 3.25 MiB with 110,675 bytes free; payload and clip selection stay unchanged. The table above belongs to the historical 1.9.1 release. The current layout is in [firmware layout](development/engineering/firmware-layout.md).
