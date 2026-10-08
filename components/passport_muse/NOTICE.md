# Muse SDK adapter provenance

Protocol implementation from https://github.com/facebookincubator/muse-gadget-sdk
Commit: 4bd647bc805b2e0de49dc894c0680225ce29bd06 (2026-10-05 checkout).
Copyright (c) Meta Platforms, Inc. and affiliates. Apache-2.0; see LICENSE.

This is a community port to ESP-IDF 5.5 / ESP32-C3, not an officially supported SDK build.
Local changes use IDF 5.5's public GCM header, exclude manufacturer eFuse signing,
and retain reversible BLE lifecycle for Passport OS. No eFuse writes, SDK OTA,
home-network tunnel, or shell/file execution are included in this adapter.
This integration reuses Passport OS's existing XiaoZhi vector-face elements
with an independently persisted Muse style. Official Jollybot artwork remains
excluded from the SDK's Apache-2.0 license and from this source and firmware.
SDK and device access tokens are supplied by the owner at runtime, never embedded in source.
Pre-pair public device_info follows the upstream plaintext chunked response;
the encrypted sender is reserved for established pairing-session messages.

Additional local changes: smaller C3 outbound WebSocket/Noise buffers (inbound
protocol limits retained); checked/idempotent queue initialization; atomic worker
lifetime; bounded network attempts; nonblocking socket during the handshake;
draining BLE dispatch workers before teardown; no SDK-token prefixes in logs;
UTF-8-safe caption truncation; a fresh static-random BLE identity that avoids
stale GATT caches from other badge firmware; and an isolated Passport OS NVS namespace. The
WAV/base64 helpers and native app UI are local implementations. The original
SDK remains the authority for pairing and chat protocol. The owner has confirmed
that the phone Muse device list now contains this device. Real account API,
certificate-checked TLS, WebSocket and Noise registration have been verified
through the owner's computer proxy. The owner subsequently confirmed voice task
delivery and two consecutive on-device text replies before the avatar UI update;
this does not establish long-duration stability or validate a later UI build.

Pairing-flow review also consulted RongleCat/ai-passport-muse, passport-port
commit fee6234e08cd0f8c90fbe8af2a6bdfad5d2808a0. Local adapter changes add
phone-setup UI state, generation-bound timeout recovery, preserved discovery
after a rejected session, and distinct network/storage/authorization errors.
Production-function fault-injection tests do not replace live phone pairing.

The HTTP CONNECT transport in sdk/http_proxy.c and .h is adapted from the same
RongleCat commit. Passport OS adds runtime NVS/phone settings, cancellable
nonblocking connect/TLS/HTTP operations, and bounded response allocation. Muse
API and real-time TLS share this transport; certificate verification is retained.

The current upstream AI Passport memory profile at commit
86cf33fb4092ba700b4dc33928966d1bcb31556d was also consulted for C3 IRAM,
I2S DMA, Wi-Fi and TLS sizing. The C3 receive block is reused for in-place
Noise decryption and one-chunk service decoding; live 16,424-byte completion
frames require the retained 17 KiB receive bound. Its AES-GCM calls use the
same mbedTLS authenticated implementation directly to
avoid PSA payload isolation copies in scarce internal RAM. Other PSA algorithms
and other targets retain their upstream paths. Host checks cover 16 KiB payloads,
byte compatibility with PSA, invalid tags, empty bodies and multi-frame assembly.
