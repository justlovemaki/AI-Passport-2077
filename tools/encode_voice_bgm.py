#!/usr/bin/env python3
"""Encode user-supplied BGM as bounded raw Opus packets for Voice Keychain."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = ROOT / "bgm"
ASSETS = ROOT / "assets/audio/voice-keychain"
PACK_NAME = "高燃BGM"
LEGACY_PACK_NAMES = {"AI剧BGM", "漫剧音乐", "音乐", PACK_NAME}
PACK_DIR = "dir24"
BITRATE = "18k"
FRAME_MS = 20
AUDIO_FILTER = "lowpass=f=7200,loudnorm=I=-16:TP=-5:LRA=11"


def ogg_packets(path: Path) -> list[bytes]:
    """Return complete Ogg packets, excluding OpusHead and OpusTags."""
    data = path.read_bytes()
    pos = 0
    packets: list[bytes] = []
    pending = bytearray()
    while pos < len(data):
        if pos + 27 > len(data) or data[pos:pos + 4] != b"OggS":
            raise ValueError(f"Invalid Ogg page at byte {pos}")
        if data[pos + 4] != 0:
            raise ValueError("Unsupported Ogg version")
        segments = data[pos + 26]
        table_start = pos + 27
        body_start = table_start + segments
        if body_start > len(data):
            raise ValueError("Truncated Ogg segment table")
        lacing = data[table_start:body_start]
        body_end = body_start + sum(lacing)
        if body_end > len(data):
            raise ValueError("Truncated Ogg page body")
        cursor = body_start
        for size in lacing:
            pending.extend(data[cursor:cursor + size])
            cursor += size
            if size < 255:
                packets.append(bytes(pending))
                pending.clear()
        pos = body_end
    if pending or len(packets) < 3 or packets[0][:8] != b"OpusHead" or packets[1][:8] != b"OpusTags":
        raise ValueError("Invalid or incomplete Ogg Opus stream")
    return packets[2:]


def display_name(path: Path) -> str:
    match = re.fullmatch(r"\d+_(.+?)_高潮15秒", path.stem)
    if not match:
        raise ValueError(f"Unexpected BGM filename: {path.name}")
    return match.group(1).split("_", 1)[0]


def encode(source: Path) -> None:
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise RuntimeError("ffmpeg with libopus support is required")
    sources = sorted(source.glob("*.mp3"), key=lambda p: p.name)
    if not sources:
        raise ValueError(f"No MP3 files found under {source}")

    output_dir = ASSETS / PACK_DIR
    output_dir.mkdir(parents=True, exist_ok=True)
    for old in output_dir.glob("*.opus"):
        old.unlink()

    files = []
    with tempfile.TemporaryDirectory(prefix="voice-bgm-") as tmp:
        tmp_dir = Path(tmp)
        for index, input_path in enumerate(sources):
            ogg = tmp_dir / f"{index:02d}.ogg"
            command = [
                ffmpeg, "-v", "error", "-y", "-i", str(input_path),
                "-map_metadata", "-1", "-vn", "-af", AUDIO_FILTER,
                "-ac", "1", "-ar", "16000", "-c:a", "libopus",
                "-application", "audio", "-b:a", BITRATE,
                "-vbr", "off", "-frame_duration", str(FRAME_MS), str(ogg),
            ]
            subprocess.run(command, check=True)
            encoded_packets = ogg_packets(ogg)
            if not encoded_packets or any(not packet or len(packet) > 1500 for packet in encoded_packets):
                raise ValueError(f"Invalid encoded packet in {input_path.name}")
            raw = b"".join(struct.pack("<H", len(packet)) + packet for packet in encoded_packets)
            output = output_dir / f"clip{index:02d}.opus"
            output.write_bytes(raw)
            samples = len(encoded_packets) * (16000 * FRAME_MS // 1000)
            files.append({
                "name": display_name(input_path),
                "src": input_path.name,
                "source_sha256": hashlib.sha256(input_path.read_bytes()).hexdigest(),
                "path": f"{PACK_DIR}/{output.name}",
                "samples": samples,
                "duration": round(samples / 16000, 2),
                "compressed_bytes": len(raw),
            })
            print(f"{input_path.name} -> {output.name}: {len(raw)} bytes / {samples / 16000:.2f}s")

    index_path = ASSETS / "voice_index.json"
    catalog = json.loads(index_path.read_text(encoding="utf-8"))
    catalog = [pack for pack in catalog if pack.get("dir") not in LEGACY_PACK_NAMES]
    catalog.insert(0, {
        "dir": PACK_NAME,
        "dir_id": 0,
        "dir_ascii": PACK_DIR,
        "storage": "app",
        "encoding": {
            "codec": "Opus",
            "sample_rate": 16000,
            "channels": 1,
            "bitrate": BITRATE,
            "vbr": False,
            "frame_ms": FRAME_MS,
            "application": "audio",
            "lowpass_hz": 7200,
            "loudness_lufs": -16,
            "true_peak_db": -5,
        },
        "files": files,
    })
    for dir_id, pack in enumerate(catalog):
        pack["dir_id"] = dir_id
    index_path.write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Updated {index_path}: {len(files)} clips / {sum(item['compressed_bytes'] for item in files)} bytes")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", nargs="?", type=Path, default=DEFAULT_SOURCE)
    args = parser.parse_args()
    encode(args.source.resolve())


if __name__ == "__main__":
    main()
