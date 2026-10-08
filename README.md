<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Arasaka Personnel Badge · AI Passport

> Hardware Target: FoloToy AI Passport (ESP32-C3 · 8 MB Flash · 240 × 320 ST7789 display · 3 physical buttons · ES8311 audio · CW2017 fuel gauge)  
> Firmware Version: **v2.6.53**

A Cyberpunk/Arasaka corporate-themed wearable badge firmware. Integrates **6 themed mini-apps**, **full XiaoZhi AI voice interaction with 19 MCP device control tools**, **252-pose vector emotion avatars**, **5 independent local badge profile slots**, and **offline Web serial/hotspot configuration**.

Transitions and selections provide a single 160 ms scanning-line response with zero continuous flashing or artificial latency. All fonts, vector glyphs, and layouts are tailored for the 240 × 320 portrait display.

---

## Architecture & Features

```text
┌─────────────────────────────────────────────────────────────┐
│                         Badge Home                          │
│   · Corporate/custom branding   · Geometric dynamic emblem  │
│   · Real-time clock & battery   · Wi-Fi connection status   │
│   · Avatar & ID clearance       · 1/5 slot indicator        │
└──────────────┬───────────────────┬───────────────────┬──────┘
               │ (Short press UP)   │ (Long press UP)   │ (Short press OK)
               ▼                   ▼                   ▼
       ┌───────────────┐   ┌───────────────┐   ┌───────────────┐
       │   Mini Apps   │   │   XiaoZhi AI  │   │   Terminal    │
       └───────┬───────┘   └───────────────┘   └───────┬───────┘
               │                                       │
     ┌─────────┴─────────┐                   ┌─────────┴─────────┐
     │ 1. Zen Muyu       │                   │ · Mini Apps       │
     │ 2. Voice Keychain │                   │ · Profile Config  │
     │ 3. City Radio     │                   │ · My QR           │
     │ 4. Cyber Yao      │                   │ · System Settings │
     │ 5. Holy Cup       │                   │ · Return to Badge │
     │ 6. Muse           │                   └───────────────────┘
     └───────────────────┘                   └───────────────────┘
```

### 1. Six Themed Built-in Mini-Apps

All mini-apps are managed under the unified system lifecycle and adapt to the active badge's 5-color palette (background, panel, accent, primary text, muted text):

| Mini-App | ID | Description & Highlights | Key Controls |
| --- | --- | --- | --- |
| **Zen Muyu** | `zen-muyu` | Vector wooden-fish striking animation and sound, manual or auto-strike mode, 4 rhythm settings (30/60/80/120 bpm). Progress persists in NVS across reboots. | **OK**: Strike<br>**UP**: Toggle Auto/Manual<br>**DOWN**: Cycle tempo (30/60/80/120 bpm)<br>**Long UP**: Pause & save<br>**Long DOWN**: Cycle volume (0/20/40/60/80%) |
| **Voice Keychain** | `voice-keychain` | 24 categories and 695 soundboard clips. Lightweight stream decoder with zero backlog on new playback; long titles scroll smoothly. | **UP/DOWN**: Navigate categories/clips<br>**OK**: Enter / Play<br>**Long UP**: Volume setting<br>**Long DOWN**: Stop playback |
| **City Radio** | `leo-radio` | Internet radio player streaming curated city stations over Wi-Fi, featuring channel resume and audio recovery. | **UP/DOWN**: Switch station<br>**OK**: Play / Pause<br>**Long UP**: Radio / network settings |
| **Cyber Yao** | `cyber-yao` | I Ching divination based on CyberYAO. Throw 3 coins six times to cast hexagrams from bottom to top; scroll judgements and line texts; automatic true solar time calibration via IP geolocation; one-key XiaoZhi interpretation handoff. | **OK**: Throw coins / Request XiaoZhi reading<br>**UP/DOWN**: Scroll judgements & line text<br>**Long DOWN**: Recast / start over |
| **Holy Cup** | `holy-cup` | Traditional decision-making oracle (Jiaobei). Cast two crescent wooden blocks for Shengbei (positive / proceed), Xiaobei (laughing / re-ask), or Yinbei (negative / defer). Rendered with vector shading and counter. | **OK**: Cast again<br>**Long OK**: Return to app list |
| **Muse** | `muse` | Muse Gadget integration with runtime SDK-token/proxy configuration, phone pairing, push-to-talk voice notes, paged text replies, and an independently selected style built from the existing XiaoZhi vector-face elements. Credentials stay in local NVS; no token is compiled into firmware. | **Hold UP**: Record<br>**Release UP**: Send<br>**DOWN**: Cancel local wait<br>**OK**: Confirm/retry/next page<br>**Long DOWN**: Reset Muse pairing |

> **Global Navigation Chooser**: Holding **OK** on any sub-page or mini-app opens a 3-way navigation chooser: **Previous Page / Home / Continue**, avoiding accidental exits or lost progress.

---

### 2. XiaoZhi Voice AI Assistant

Access XiaoZhi voice conversation directly by **holding UP on Badge Home** or via **Terminal → System → XiaoZhi AI**:

- **Streaming Semi-Duplex Conversation**: Automatic endpoint detection (VAD); replies play back immediately via Opus stream with interrupt support on OK.
- **252 Vector Emotion Poses (Face Clock)**:
  - 4 selectable styles: **Cute Character**, **Male Portrait (bald with soft reflection)**, **Female Portrait (bob hair & clips)**, **King of the Road**.
  - 21 emotion states per style, each with 3 dedicated pose variations (63 poses/style, 252 total).
  - Mouth shapes react to real-time PCM audio playback levels. Assets use row-indexed lossless RLE decoding for minimal RAM consumption.
  - In chat: **Long UP** cycles avatar style; **Long DOWN** jumps to XiaoZhi settings.
- **19 MCP Device Control Tools**:
  XiaoZhi understands natural language and invokes on-device functions directly:
  1. `self.get_device_status`: Query battery, Wi-Fi, brightness, and configured badge profiles.
  2. `self.apps.open`: Launch an app (`zen-muyu`, `leo-radio`, `voice-keychain`, `cyber-yao`, `muse`).
  3. `self.audio.search` / `self.audio.play` / `self.audio.play_random`: Search or play specific/random audio clips.
  4. `self.badge.show_qr`: Display active personal QR code immediately.
  5. `self.badge.switch`: Switch active badge profile by slot number (1–5) or name.
  6. `self.display.set_brightness`: Adjust screen backlight brightness (20%–100%).
  7. `self.xiaozhi.set_volume`: Adjust XiaoZhi speech reply volume independently (0–100%).
  8. `self.radio.search` / `self.radio.play`: Search and stream online radio stations.
  9. `self.reminder.set` / `get` / `cancel`: Manage up to 8 persistent alarms/reminders (countdown, clock time, dates, weekday repetitions).
  10. `self.yao.cast` / `self.yao.get`: Voice casting and structured I Ching interpretation query.
  11. `self.badge.get_profile` / `update_profile`: Read or edit badge text fields (name, department, title) via voice.
  12. `self.system.action`: Return home (`home`) or enter deep sleep (`shutdown`).

---

### 3. Five Local Badge Profiles & Web Tooling

Store **5 independent identity profiles** directly in flash, each keeping its own name, title, department, ID, avatar photo, brand logo, and 5-color theme.

- **Quick Switching**: Hold **OK** on Badge Home to open the profile switcher; selected profile persists across power cycles.
- **Web Serial Configuration (`configure/index.html`)**:
  - **Direct USB Web Serial**: Open the offline configuration page in Chrome or Edge, connect USB, and configure without installing drivers, software, or uploading data to any cloud.
  - **Phone Hotspot Setup**: When away from a PC, open **System → Network** and press OK to turn on the configuration hotspot. Connect with a smartphone and browse `http://192.168.4.1` for the full setup UI.
  - **Smart Cropping**: Upload photos and QR codes up to 10 MB; images are cropped to 72 × 88 avatars and sharp monochrome QR bitmaps automatically.
  - **Muse Setup**: Save an owner-provided Muse SDK token and optional LAN HTTP proxy without returning the token to the browser. Changing the token clears old Muse pairing; changing only the proxy preserves it.

---

## Button Navigation Summary

| Screen | Gesture | Action |
| --- | --- | --- |
| **Badge Home** | Short UP | Open Mini-Apps list |
| | Long UP | Launch XiaoZhi AI Conversation |
| | Short DOWN | Show Personal QR code instantly |
| | Short OK | Open Terminal menu |
| | Long OK | Open Badge Profile switcher (1–5) |
| **Terminal Menu** | UP / DOWN | Select item (Mini-Apps, Profile, QR, Settings, Home) |
| | Short OK | Enter selected page |
| | Long OK | Open Return Chooser (Previous / Home / Continue) |
| **Personal QR** | DOWN / OK | Dismiss QR and return |
| **XiaoZhi AI** | Short OK | Start / Pause / Interrupt conversation |
| | UP / DOWN | Adjust XiaoZhi volume (±10%) |
| | Long UP | Cycle & save avatar face style |
| | Long DOWN | Open XiaoZhi AI settings |
| **Sub-pages** | Long OK | Open Return Chooser (Previous / Home / Continue) |

---

## Flash Partition Layout

Configured for ESP32-C3 (8 MB Flash, no PSRAM):

- `factory` (Application binary): `3.75 MiB` (0x3C0000 bytes at 0x10000)
- `voice_data` (Voice pack head): `2.75 MiB` (at 0x3D0000)
- `voice_tail` (Voice pack tail): `512 KiB` (at 0x750000)
- `badge_slots` (Profiles 2–5): `768 KiB` (at 0x690000)
- `badge_user` (Profile 1): `192 KiB` (at 0x7D0000)
- `nvs` / `otadata` / `phy_init`: standard system partitions.

Each profile uses dual A/B 96 KB banks with CRC validation, keeping data safe across firmware updates and unexpected power loss.

---

## Build & Installation

### Prerequisites
- **ESP-IDF**: v5.5.3 (environment activated)
- **Tools**: Python 3, Git, CMake, Ninja

### Verification & Build
```bash
# 1. Run repository consistency checks
python tools/check_repo.py

# 2. Build firmware
idf.py -B build -D SDKCONFIG=build/sdkconfig build

# 3. Create merged binary and verify
idf.py -B build merge-bin -o FoloToy-AI-Passport-full.bin
python tools/verify_firmware.py build
```

### Flashing
```bash
# Segmented flashing (recommended; preserves saved badge profiles & NVS):
idf.py -p COM6 flash monitor

# Or use the flash management tool with automatic backup:
python tools/flash_badge.py --port COM6 --backup-dir ./my_backup
```

---

## Licenses & Acknowledgments

- Firmware implementation licensed under the [MIT License](LICENSE).
- Based on the upstream [FoloToy AI Passport](https://github.com/FoloToy/AI-Passport) hardware development baseline.
- City Radio mini-app is based on [leo-radio](https://github.com/leo0183/leo-radio) (MIT License).
- Voice Keychain mini-app originates from [Shinku-Chen/ai-passport (feature/voice-keychain)](https://github.com/Shinku-Chen/ai-passport/tree/feature/voice-keychain) (MIT License).
- XiaoZhi AI voice conversation protocol and integration derived from [FoloToy/folo-ai-passport-xiaozhi](https://github.com/FoloToy/folo-ai-passport-xiaozhi) (MIT License).
- Muse integration is adapted from [manchunx7-bit/ai-passport-muse](https://github.com/manchunx7-bit/ai-passport-muse) (MIT) and the [Muse Gadget SDK](https://github.com/facebookincubator/muse-gadget-sdk) (Apache-2.0). Vendored notices are retained in `components/passport_muse` and `components/noise_core`; official Jollybot artwork is not included.
- I Ching text and core divination rules derived from [CyberYAO](https://github.com/XadillaX/CyberYAO) (MIT License).
- Visual references: Arasaka and Cyberpunk 2077 references belong to CD PROJEKT RED and their respective owners. Used here solely for non-commercial open-source maker education.
