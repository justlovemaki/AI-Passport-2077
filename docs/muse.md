<p align="right">
  <a href="muse.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Muse mini-app

The `muse` mini-app connects AI Passport to the owner's Muse account through the community Muse Gadget protocol. It records 16 kHz mono audio while UP is held, sends a voice note on release, and displays up to 2 KiB of reply text under the standard badge header, with as many as nine lines per page and OK advancing pages. It does not provide TTS, full conversation history, quotas, or remote shell/device-command execution.

## Setup

1. Connect the badge to a 2.4 GHz Wi-Fi network in the local configuration page.
2. Obtain your own SDK token from [Muse Gadgets](https://gadgets.muse.ai/) and review the applicable terms.
3. In the configuration page, save the SDK token. Leave the HTTP proxy empty when the badge can reach Muse directly. If a trusted-LAN proxy is required, enter its IPv4 address and HTTP/combined port; hostnames and SOCKS-only ports are not accepted.
4. Open **Mini Apps → Muse**. In the phone Muse app, enable developer mode under **Settings → Devices**, add the displayed `MuseGadget-Passport-XXXXXX`, and press OK on the badge when the physical confirmation prompt appears. Select the Wi-Fi network already used by the badge.
5. Wait for **Ready**, hold UP to speak, and release UP to send.

The SDK token is write-only through the badge protocol and is never returned to the browser. Changing it clears the previous Muse account pairing. Editing only the proxy preserves pairing. Configuration writes are refused while the mini-app owns its session. Credentials, Wi-Fi data, and pairing material live in NVS and must never be committed or included in logs.

Community pairing has physical confirmation and encrypted provisioning but no manufacturer attestation. Pair only on a trusted network. TLS certificate validation and the Muse Noise session remain enabled. The integration does not write eFuses, enable SDK OTA, expose a home-network tunnel, or execute cloud-supplied device commands.

## Controls

| Input | Action |
| --- | --- |
| Hold UP / release UP | Record / send a voice note, up to 30 seconds |
| Short DOWN | Stop local recording or waiting; an already submitted cloud task may continue |
| Short OK | Confirm pairing, retry an error, or advance reply pages |
| Hold DOWN on ready/reply | Cycle and persist Muse's independent face style |
| Hold DOWN on pairing/error | Clear Muse account pairing while preserving the SDK token, system Wi-Fi, and badge profiles |
| Hold OK | Open the global return chooser |

The app owns microphone, BLE pairing, TLS/Noise buffers, and its worker only while open. Exit joins the worker, closes BLE and network sessions, releases large parser buffers, suspends the codec, and then deletes the LVGL screen.

## Source and acceptance

The adapter comes from [`manchunx7-bit/ai-passport-muse`](https://github.com/manchunx7-bit/ai-passport-muse) commit `6e461e71760e75286ff50d479a6f6db565db6d00` (local integration under MIT) and the [Muse Gadget SDK](https://github.com/facebookincubator/muse-gadget-sdk) commit recorded in `components/passport_muse/NOTICE.md` (Apache-2.0). The interface reuses the firmware's existing XiaoZhi vector face elements without duplicating image assets. Muse keeps its own persisted style selection, independent from XiaoZhi, and includes no official Jollybot assets.

Before release, verify on the physical badge: fresh token setup, phone discovery and physical confirmation, direct and optional-proxy connectivity, non-silent capture, two consecutive replies, multi-page navigation, cancellation, disconnect/reconnect, exit during each stage, and repeated enter/exit without task or heap loss. Record the minimum free heap and largest block during BLE pairing, TLS connection, recording, and reply subscription. A firmware build or host protocol test is not device acceptance.
