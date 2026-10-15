<p align="right">
  <a href="pipeline-guide.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# AI Agent Guide: Version Update, Firmware Build, and Flashing Pipeline

This document defines the unified pipeline for firmware version bumping, compilation, 0x0 merged binary generation, layout verification, and device flashing.

## 1. Automated Pipeline Tool: `tools/pipeline.py`

The repository provides `tools/pipeline.py` for synchronized version updates, ordinary builds and explicit development utilities. A release/preserving delivery deliberately combines it with the repository gate and `tools/flash_badge.py`; no single build command is treated as device acceptance.

### Release workflow

1. **Bump exactly once, synchronize all public version locations and build**:
   ```bash
   python tools/pipeline.py --bump patch
   ```
   Use `--bump minor`, `--bump major`, or `--version X.Y.Z` only when intentionally selected. Re-running an ordinary build must omit all version arguments.

2. **Run the complete repository gate**:
   ```bash
   ./tools/validate.sh
   ```
   This is separate from the pipeline build. It requires an activated ESP-IDF 5.5.3 environment and reports host tests separately from firmware compilation.

3. **Perform a preserving installation with a full private backup**:
   ```bash
   python tools/flash_badge.py --port <PORT> \
     --backup-dir <private-directory-outside-the-repository> --build build
   ```
   The installer validates the existing layout, reads the complete 8 MiB flash before any write, writes and verifies segmented firmware, commits the partition table last, then verifies NVS/PHY and all profile banks before reset.

### Other operations

- **Incremental development-only application flash** (not a release or preserving acceptance flow):
  ```bash
  python tools/pipeline.py --flash
  ```
- **Legacy selected-partition backup/restore utilities** require an explicit private directory:
  ```bash
  python tools/pipeline.py --backup-only --backup-dir <private-directory>
  python tools/pipeline.py --restore-only --backup-dir <private-directory>
  ```

## 2. Core Execution Stages

### Stage 1: Update Version

`--bump` and `--version` update these three locations together:

- `main/badge_profile.h`: `#define BADGE_VERSION "X.Y.Z"`
- `README.md`: public English firmware version
- `README.zh_CN.md`: matching Simplified Chinese firmware version

Only numeric `X.Y.Z` is accepted. The command fails before writing when the existing three values disagree or a marker is missing/duplicated. It prepares all replacements before committing them, writes the source-of-truth header last, verifies synchronization, and runs repository checks. Ordinary invocation without `--bump`/`--version` only reads the current version and never increments it.

### Stage 2: Activate ESP-IDF 5.5.3 Environment
- Toolchain path: `D:\esp\esp-idf`
- Activation in Windows PowerShell:
  ```powershell
  [Environment]::SetEnvironmentVariable('MSYSTEM', '')
  Set-Location D:\esp\esp-idf
  . .\export.ps1
  Set-Location <repo_root>
  ```

### Stage 3: Build & Merge Binary (0x0)
- Compile application: `idf.py build`
- Merge full image: `idf.py merge-bin -o FoloToy-AI-Passport-full.bin`
- Verify firmware layout: `python tools/verify_firmware.py build`

This build does not replace `./tools/validate.sh`; run the complete gate before delivery. The firmware gate republishes the merged and segmented files from the same isolated verified build into `build/`, so the later preserving installer cannot consume stale pipeline segments.

### Stage 4: Flash Device (Preserve User Data vs Full Erase)

> **⚠️ Critical Safety Rule**:
> - **Incremental / Feature Update**: **DO NOT write 0x0 full image directly!** Writing at 0x0 overwrites `0x9000` (NVS, Wi-Fi credentials) and `0x690000`/`0x7d0000` (custom profiles & QR codes).
> - **Preserving release installation (recommended)**: use `tools/flash_badge.py` after the complete gate. It backs up all 8 MiB and verifies protected regions as described above.
- **Application-only write**: writing only `0x10000` avoids the known user partitions and is retained as a development shortcut, but it has no complete pre-write backup or post-write protected-region acceptance by itself.

- **Release/preserving installation**:
  ```bash
  python tools/flash_badge.py --port <PORT> \
    --backup-dir <private-directory-outside-the-repository> --build build
  ```
- **Development-only application write**:
  ```bash
  python tools/pipeline.py --flash
  ```

- **Factory Reset Flash (Erases all data)**:
  Use only when initializing a blank chip or requested by the user:
  ```bash
  python tools/pipeline.py --erase-all
  ```

## 3. Troubleshooting

- **Port Disappears After Flashing**: Press the UP (wake) key or replug the USB cable to re-enumerate the USB CDC/JTAG serial port.
- **Compressed Size vs Flash Size**: `esptool` compresses the 8 MB flash payload during transport. The decompressed 8 MB image is written completely.
