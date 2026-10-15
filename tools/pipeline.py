#!/usr/bin/env python3
"""Unified Release, Backup, Restore & Flash Pipeline for AI Passport (ESP32-C3)

Release workflow:
    # 1. Bump source + both READMEs and build (run exactly once):
    python tools/pipeline.py --bump patch

    # 2. Run the repository gate, then use tools/flash_badge.py with a private
    #    backup directory outside the repository for preserving installation.

Other operations:
    python tools/pipeline.py --backup-only --backup-dir <private-dir>
    python tools/pipeline.py --restore-only --backup-dir <private-dir>

`--flash` remains an app-only incremental development shortcut. It is not the
release/preserving acceptance flow and does not replace the complete gate.
"""

import argparse
import datetime
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
HEADER_FILE = REPO_ROOT / "main" / "badge_profile.h"
README_FILE = REPO_ROOT / "README.md"
README_ZH_FILE = REPO_ROOT / "README.zh_CN.md"
VERSION_RE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
VERSION_TARGETS = (
    (HEADER_FILE, re.compile(r'(#define\s+BADGE_VERSION\s+")[^"]+(")')),
    (README_FILE, re.compile(r'(> Firmware Version: \*\*v)[^*]+(\*\*)')),
    (README_ZH_FILE, re.compile(r'(> 固件版本：\*\*v)[^*]+(\*\*)')),
)
BUILD_DIR = REPO_ROOT / "build"
FULL_BIN = BUILD_DIR / "FoloToy-AI-Passport-full.bin"
APP_BIN = BUILD_DIR / "FoloToy-AI-Passport.bin"

# Key User Partitions defined in partitions.csv:
# nvs: 0x9000 (0x6000 bytes) -> Wi-Fi credentials, xiaozhi uuid & preferences
# badge_slots: 0x690000 (0xc0000 bytes) -> configured badges & profile cards
# badge_user: 0x7d0000 (0x30000 bytes) -> QR codes and user custom files
USER_PARTITIONS = [
    ("nvs", 0x9000, 0x6000),
    ("badge_slots", 0x690000, 0xC0000),
    ("badge_user", 0x7D0000, 0x30000),
]


def _target_version(path: Path, pattern: re.Pattern[str]) -> str:
    try:
        content = path.read_bytes().decode("utf-8")
    except OSError as error:
        raise ValueError(f"Could not read version target {path.relative_to(REPO_ROOT)}") from error
    matches = list(pattern.finditer(content))
    if len(matches) != 1:
        raise ValueError(f"Expected exactly one version marker in {path.relative_to(REPO_ROOT)}")
    # The marker patterns place the semantic version between capture groups.
    value = content[matches[0].end(1):matches[0].start(2)]
    if not VERSION_RE.fullmatch(value):
        raise ValueError(f"Invalid semantic version {value!r} in {path.relative_to(REPO_ROOT)}")
    return value


def get_current_version() -> str:
    versions = [(path, _target_version(path, pattern)) for path, pattern in VERSION_TARGETS]
    current = versions[0][1]
    mismatched = [f"{path.relative_to(REPO_ROOT)}={version}" for path, version in versions if version != current]
    if mismatched:
        raise ValueError("Version targets disagree: " + ", ".join(mismatched))
    return current


def bump_version_string(ver: str, part: str) -> str:
    if not VERSION_RE.fullmatch(ver) or part not in ("patch", "minor", "major"):
        raise ValueError("Version bump requires X.Y.Z and patch, minor or major")
    nums = [int(x) for x in ver.split(".")]
    if part == "major":
        nums[0] += 1
        nums[1] = 0
        nums[2] = 0
    elif part == "minor":
        nums[1] += 1
        nums[2] = 0
    elif part == "patch":
        nums[2] += 1
    return ".".join(str(x) for x in nums)


def update_version(new_ver: str):
    if not VERSION_RE.fullmatch(new_ver):
        raise ValueError("--version must use numeric X.Y.Z format")
    current = get_current_version()  # Fail closed before changing an inconsistent tree.
    originals: dict[Path, bytes] = {}
    replacements: dict[Path, bytes] = {}
    for path, pattern in VERSION_TARGETS:
        raw = path.read_bytes()
        content = raw.decode("utf-8")
        replaced, count = pattern.subn(rf"\g<1>{new_ver}\g<2>", content)
        if count != 1:
            raise ValueError(f"Failed to replace one version marker in {path.relative_to(REPO_ROOT)}")
        originals[path] = raw
        replacements[path] = replaced.encode("utf-8")

    # Prepare every same-directory temporary before replacing anything. Replace
    # the source-of-truth header last; a failure rolls already-replaced peers back.
    temporary: dict[Path, Path] = {}
    replaced_paths: list[Path] = []
    try:
        for path, content in replacements.items():
            temp = path.with_name(path.name + ".version-tmp")
            temp.write_bytes(content)
            temporary[path] = temp
        ordered = [README_FILE, README_ZH_FILE, HEADER_FILE]
        for path in ordered:
            temporary[path].replace(path)
            replaced_paths.append(path)
    except OSError:
        for path in replaced_paths:
            path.write_bytes(originals[path])
        raise
    finally:
        for temp in temporary.values():
            temp.unlink(missing_ok=True)

    print(f"[Version] Updated {current} -> {new_ver}: source + English/Chinese README")


def verify_release_metadata(expected: str) -> None:
    actual = get_current_version()
    if actual != expected:
        raise ValueError(f"Synchronized version check failed: expected {expected}, got {actual}")
    result = subprocess.run([sys.executable, str(REPO_ROOT / "tools" / "check_repo.py")])
    if result.returncode != 0:
        raise SystemExit(result.returncode)
    print(f"[Version] Repository metadata verified at {actual}")


def run_idf_command(pwsh_script: str):
    env_setup = (
        "[Environment]::SetEnvironmentVariable('MSYSTEM', ''); "
        "Set-Location D:\\esp\\esp-idf; . .\\export.ps1; "
        f"Set-Location {REPO_ROOT}; "
    )
    cmd = ["powershell", "-ExecutionPolicy", "Bypass", "-Command", env_setup + pwsh_script]
    res = subprocess.run(cmd)
    if res.returncode != 0:
        sys.exit(res.returncode)


def build_and_merge():
    print("[Build] Compiling firmware and generating verified 0x0 binary...")
    run_idf_command(
        "idf.py build; idf.py merge-bin -o FoloToy-AI-Passport-full.bin; python tools/verify_firmware.py build"
    )
    if not FULL_BIN.exists() or not APP_BIN.exists():
        raise FileNotFoundError("Expected output binaries not found after build.")
    print(f"[Build] Artifacts ready: App {APP_BIN.stat().st_size}B | Full {FULL_BIN.stat().st_size}B")


def find_serial_port(preferred_port: str = None) -> str:
    if preferred_port:
        return preferred_port
    try:
        from serial.tools import list_ports
        ports = list(list_ports.comports())
        for p in ports:
            if "Bluetooth" not in p.description and "COM1" not in p.device:
                return p.device
    except ImportError:
        pass
    return None


def backup_user_data(port: str, baud: int, backup_dir: Path):
    backup_dir.mkdir(parents=True, exist_ok=True)
    print(f"[Backup] Backing up user data from {port} to: {backup_dir}")
    manifest = {}
    for name, offset, size in USER_PARTITIONS:
        file_path = backup_dir / f"{name}.bin"
        cmd = [
            "esptool",
            "--port",
            port,
            "--chip",
            "esp32c3",
            "-b",
            str(baud),
            "--after",
            "no_reset",
            "read-flash",
            hex(offset),
            hex(size),
            str(file_path),
        ]
        res = subprocess.run(cmd)
        if res.returncode != 0 or not file_path.exists():
            print(f"ERROR: Failed to backup partition {name} at {hex(offset)}", file=sys.stderr)
            sys.exit(1)
        data = file_path.read_bytes()
        sha256 = hashlib.sha256(data).hexdigest()
        manifest[name] = {"offset": hex(offset), "size": hex(size), "sha256": sha256, "file": file_path.name}
        print(f"  ✓ {name} ({size // 1024} KB) backed up. SHA256: {sha256[:12]}...")

    manifest_file = backup_dir / "manifest.json"
    manifest_file.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"[Backup] Successfully completed. Manifest written to {manifest_file.name}")


def restore_user_data(port: str, baud: int, backup_dir: Path):
    manifest_file = backup_dir / "manifest.json"
    if not manifest_file.exists():
        print(f"ERROR: Cannot restore; manifest not found in {backup_dir}", file=sys.stderr)
        sys.exit(1)

    manifest = json.loads(manifest_file.read_text(encoding="utf-8"))
    print(f"[Restore] Restoring user data to {port} from: {backup_dir}")

    restore_args = []
    for name, info in manifest.items():
        bin_path = backup_dir / info["file"]
        if not bin_path.exists():
            print(f"ERROR: Missing backup file: {bin_path}", file=sys.stderr)
            sys.exit(1)
        actual_sha = hashlib.sha256(bin_path.read_bytes()).hexdigest()
        if actual_sha != info["sha256"]:
            print(f"ERROR: SHA256 mismatch for {name}! Restore aborted.", file=sys.stderr)
            sys.exit(1)
        restore_args.extend([info["offset"], str(bin_path)])

    cmd = [
        "esptool",
        "--port",
        port,
        "--chip",
        "esp32c3",
        "-b",
        str(baud),
        "write-flash",
        *restore_args,
    ]
    res = subprocess.run(cmd)
    if res.returncode != 0:
        print("ERROR: Flash restore failed.", file=sys.stderr)
        sys.exit(1)
    print("[Restore] User data successfully restored & verified.")


def flash_device(port: str, baud: int, erase_all: bool = False):
    if erase_all:
        print("[Flash] WARNING: Full 0x0 flash requested. NVS and user profiles will be erased!")
        cmd = [
            "esptool",
            "--port",
            port,
            "--chip",
            "esp32c3",
            "-b",
            str(baud),
            "write-flash",
            "0x0",
            str(FULL_BIN),
        ]
    else:
        print(f"[Flash] Flashing application (preserving in-place NVS, Wi-Fi & badge profiles)...")
        print(f"        Target: 0x10000 -> {APP_BIN.name} ({APP_BIN.stat().st_size} bytes)")
        cmd = [
            "esptool",
            "--port",
            port,
            "--chip",
            "esp32c3",
            "-b",
            str(baud),
            "write-flash",
            "0x10000",
            str(APP_BIN),
        ]
    res = subprocess.run(cmd)
    if res.returncode != 0:
        sys.exit(res.returncode)
    print("[Flash] Flashing complete and device restarted.")


def main():
    parser = argparse.ArgumentParser(description="AI Passport Automated Build, Backup, Restore & Flash Pipeline")
    parser.add_argument("--bump", choices=["patch", "minor", "major"], default=None, help="Auto-bump version component")
    parser.add_argument("--version", type=str, default=None, help="Specify exact version (e.g. 2.6.58)")
    parser.add_argument("--flash", action="store_true", help="Development-only app flash; release flow uses tools/flash_badge.py after validation")
    parser.add_argument("--erase-all", action="store_true", help="DANGEROUS: Flash full 0x0 image and reset to factory defaults")
    parser.add_argument("--backup-dir", type=Path, default=None, help="Directory for backup/restore (defaults to ./backups/YYYYMMDD-HHMMSS)")
    parser.add_argument("--backup", action="store_true", help="Explicitly backup user partitions before flashing")
    parser.add_argument("--restore", action="store_true", help="Explicitly restore user partitions after flashing")
    parser.add_argument("--backup-only", action="store_true", help="Only backup user data from device and exit")
    parser.add_argument("--restore-only", action="store_true", help="Only restore user data from backup-dir and exit")
    parser.add_argument("--port", type=str, default=None, help="Serial port (auto-detected if omitted)")
    parser.add_argument("--baud", type=int, default=460800, help="Serial baud rate (default: 460800)")

    args = parser.parse_args()

    # Determine default backup dir if requested
    backup_dir = args.backup_dir
    if (args.backup or args.restore or args.backup_only or args.restore_only) and not backup_dir:
        stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        backup_dir = REPO_ROOT / "backups" / stamp

    # Standalone Restore Only
    if args.restore_only:
        port = find_serial_port(args.port)
        if not port:
            print("ERROR: Device not detected! Please ensure ESP32-C3 is plugged in.", file=sys.stderr)
            sys.exit(1)
        restore_user_data(port, args.baud, backup_dir)
        return

    # Standalone Backup Only
    if args.backup_only:
        port = find_serial_port(args.port)
        if not port:
            print("ERROR: Device not detected! Please ensure ESP32-C3 is plugged in.", file=sys.stderr)
            sys.exit(1)
        backup_user_data(port, args.baud, backup_dir)
        return

    # 1. Version Update
    cur_ver = get_current_version()
    new_ver = cur_ver
    if args.version:
        new_ver = args.version
        update_version(new_ver)
    elif args.bump:
        new_ver = bump_version_string(cur_ver, args.bump)
        update_version(new_ver)
    else:
        print(f"[Version] Current version is: {cur_ver}")
    verify_release_metadata(new_ver)

    # 2. Build
    build_and_merge()

    # 3. Flashing flow
    need_port = args.flash or args.erase_all or args.backup or args.restore
    if need_port:
        port = find_serial_port(args.port)
        if not port:
            print("ERROR: Device not detected! Please ensure ESP32-C3 is plugged into USB.", file=sys.stderr)
            sys.exit(1)

        # Pre-flash backup
        if args.backup:
            backup_user_data(port, args.baud, backup_dir)

        # Flash
        if args.flash or args.erase_all:
            flash_device(port, args.baud, erase_all=args.erase_all)

        # Post-flash restore
        if args.restore:
            restore_user_data(port, args.baud, backup_dir)


if __name__ == "__main__":
    main()
