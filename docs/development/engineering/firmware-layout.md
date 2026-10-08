<p align="right">
  <a href="firmware-layout.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Firmware Layout

This repository is a minimal base for user-defined firmware targeting an
ESP32-C3 with 8 MB Flash. Its default does not reserve product-specific
identity, OTA, or unused data partitions.

## Profile-enabled derivative layout

This branch's 2.6.19 firmware uses the following intentional custom layout. NVS and PHY stay at their original addresses. The template baseline described below remains a reference.

| Partition | Offset | Size |
| --- | --- | --- |
| nvs | 0x9000 | 0x6000 |
| phy_init | 0xF000 | 0x1000 |
| factory | 0x10000 | 0x3C0000 |
| voice_data | 0x3D0000 | 0x2C0000 |
| badge_slots | 0x690000 | 0xC0000 |
| voice_tail | 0x750000 | 0x80000 |
| badge_user | 0x7D0000 | 0x30000 |

`badge_user` contains two 0x18000-byte banks. Each bank has a 32-byte CRC-protected header, up to 2048 JSON bytes after the header, a 61,568-byte identity panel at bank offset 0x1000, and a 12,672-byte avatar immediately after it. Header commit is last; sequence plus payload/header CRCs select the newest complete bank at boot. Segmented firmware flashing preserves this partition and NVS. Do not erase the whole flash when updating.

`badge_slots` holds four additional profiles, each with two 0x18000-byte banks. Slot 1 stays at the unchanged `badge_user` address. Format 2 also stores brand (13,024 bytes), logo (2,048 bytes), and QR (4,608 bytes), totaling 93,920 payload bytes per bank. The active slot is stored in the separate `badge_cards` NVS namespace. Each profile has its own sequence and commit marker; interrupted writes preserve its previous bank. Before migrating an installed device, back up NVS and profile regions and ensure the added partition does not overlap existing application or user data.


2.6.8 reclaims 128 KiB of voice headroom for the application. Only `voice_data` moves; NVS, PHY and both profile partition offsets/sizes remain unchanged. All 726 clips still contain 4,270,480 payload bytes, with 50,800 bytes of bundle headroom. Format 4 profiles contain 91,648 payload bytes and retain two 96 KiB banks per badge. Migration must rewrite both voice images, not merely the application or partition table. Back up the full flash and validate its layout, write and verify segmented resources, commit the new table last, and verify NVS/PHY and all profile bytes before resetting.

## Default layout

The default partition table contains exactly:

| Partition | Type/subtype | Offset | Size | Purpose |
| --- | --- | ---: | ---: | --- |
| `nvs` | data/NVS | `0x9000` | `0x6000` | ESP-IDF and application key-value storage |
| `phy_init` | data/PHY | `0xF000` | `0x1000` | PHY initialization data |
| `factory` | app/factory | `0x10000` | `0x7F0000` | The single application image; all remaining Flash |

The default has no OTA slots. This is a starting point, not a restriction on
user firmware.

## Custom layouts

Users may edit `partitions.csv` to resize, move, add, or remove partitions for
their application. A custom table may use OTA slots, filesystem/resource
partitions, or other application-specific data. Keep the 8 MB device boundary,
avoid overlaps, and make sure the application image is flashed at the start of
an app partition large enough to contain it. When a derivative changes its
layout, update that project's documentation and flashing instructions.

## Enforced validation

Run:

```bash
./tools/validate.sh --firmware
```

The check builds in an isolated directory, creates the merged image, reads the
configured image offsets from `flash_args`, validates the partition-table MD5,
partition bounds, unique labels, and non-overlap, then ensures the application
offset matches an app partition large enough to contain it. It intentionally
does not require the default partition list. CI runs the same gate.

Upload only `build/FoloToy-AI-Passport-full.bin`; the similarly named app-only
`build/FoloToy-AI-Passport.bin` does not contain the bootloader or partition
table.

## Flashing and stored data

The verified merged image is written from `0x0`. Because the merged file pads
the gaps between images, flashing it can reset the NVS and PHY data regions.
Use the merged image for blank-device provisioning or an intentional complete
refresh. During normal development, use segmented `idf.py flash` when existing
NVS state should be preserved. `idf.py erase-flash` erases all user data.

Voice resources use a contiguous logical stream split around the profile partitions, without filesystem formatting. `tools/flash_badge.py` backs up and validates before segmented installation. Merged-image padding also covers profiles 2–5; it must not be used for preserving updates.

2.6.19 moves 896 KiB from voice capacity into the application after the approved selection: factory capacity is 0x3C0000 (3.75 MiB); voice head starts at 0x3D0000 with size 0x2C0000, and the tail stays at 0x750000 / 0x80000. The 695 partition-backed clips contain 3,293,101 payload bytes, leaving 110,675 bytes after the 4,096-byte header. NVS, PHY, all profile/image/QR regions and the bootloader retain their addresses. Migration requires a verified full 8 MiB backup, writing and verifying the application and both repacked voice images, committing the partition table last, then verifying protected data. An app-only update is insufficient.

The later High-Energy BGM pack addition keeps this partition map unchanged. Its 14 clips occupy 494,158 bytes embedded in the factory application, while the original 695 clips and both voice partition images remain byte-identical. The generated catalog therefore exposes 25 packs and 709 clips across two storage backends. Installed devices need the matching application; rewriting voice partitions is unnecessary when upgrading from the 695-clip layout.
