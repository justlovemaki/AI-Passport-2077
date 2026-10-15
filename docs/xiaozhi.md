<p align="right"><a href="xiaozhi.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# XiaoZhi mini app

Version 2.1.0 integrates the voice conversation protocol from
[Folo AI Passport XiaoZhi](https://github.com/FoloToy/folo-ai-passport-xiaozhi),
commit `d24fce080d86d7cc642f71585f6efde40fb99104` (upstream XiaoZhi 2.4.2).
The MIT license and upstream notice are retained in `assets/xiaozhi/`.

## Current navigation in 2.6.0

Use **Settings → Xiaozhi AI**, removed from Mini Apps. Start conversation connects
automatically; Home long-UP remains available. Reply volume reuses the same saved
value. Optional local background wake and custom trained-model import are described
in [background wake](xiaozhi-background.md). Older version sections below describe
their original behavior; the current entry and local-wake support supersede them.

## Shared settings and controls

Open the employee terminal, choose Mini Apps, then XiaoZhi. The badge owns
Wi-Fi, hotspot onboarding, brightness, identity profiles, theme and navigation.
There is no imported XiaoZhi settings page or independent Wi-Fi manager.
The exact home header displays AI Passport, Wi-Fi and battery percentage.

- Press OK to connect. If activation is needed, open `xiaozhi.me`, sign in to
  the console and add the device using the code displayed on the badge.
  After binding, press OK to retry.
- In 2.1.3, OK connects and begins continuous conversation. The server's `auto`
  listening mode detects the end of speech; no send button is required. After
  the reply queue drains, the badge automatically opens the next listening turn.
- OK while listening pauses conversation; OK again resumes. OK during a reply
  aborts it and opens a new listening turn after a short echo/tail guard.
  A recording still has a 30-second safety limit.
- Current UP/DOWN volume keys in conversation and the system AI-volume page
  adjust by 5 percentage points, clamped to 0–100. Existing saved percentages,
  including exact voice/API values not divisible by five, are retained rather
  than rounded. The preference introduced in 2.1.4 still survives re-entry and
  reboot, including mute at zero, and uses the shared audio hardware.
- Long OK uses the system Previous / Home / Continue chooser.
- Exiting closes the network connection and frees the audio resources.
  Microphone upload begins after the user starts conversation and continues on
  automatically reopened listening turns until paused, disconnected or exited.

The integration uses continuous, half-duplex conversation after explicit start.
It listens between replies; interrupting speech playback uses OK, without
full-duplex voice barge-in or a local wake-word model. Standalone OTA, additional board
drivers and the upstream settings UI are excluded from this mini app. The
discovery endpoint supplies service/activation data only; its firmware download
instructions are never executed. Cloud conversation requires an account bound
by the user and a reachable XiaoZhi service.

## Immersive conversation and remembered volume in 2.1.4

A central voice instrument, listening/thinking/speaking state and unboxed caption
stage replace stacked chat cards. The current badge palette and shared header
remain in use. Actual captured/decoded PCM energy drives the instrument, with
only a 41 by 35 pixel region invalidated. Unchanged screens do not redraw and
there is no perpetual animation timer.

Captions wrap into four visible lines. Thinking focuses recognized input;
speaking focuses the current reply. Long captions page every four seconds with
one overlapping line. While listening or paused, reaching the final page swaps
to the other side of the conversation. New captions start at their first page;
connection errors and activation instructions take priority over old captions.
Storage remains bounded to the latest input (255 UTF-8 bytes) and current reply
sentence (511 bytes), rather than an unlimited conversation history.

Volume keys enqueue an atomic request; the audio worker applies it even while
idle. The existing `xiaozhi` NVS namespace stores `volume` in 0..100, including
mute. Without a saved preference, it inherits the shared hardware value. Writes
coalesce for one second and run without active audio, or after a reply drains;
exit flushes the latest value. Failed writes remain dirty and retry after three
seconds, with an error log. No flash writes occur during playback. Abrupt power
loss before a pending write commits can lose that latest change.

## Implementation

### Standby in 2.1.1

The standby policy is scoped to XiaoZhi; shared badge settings stay authoritative.

| Item | Behavior |
| --- | --- |
| Screen | After 60 seconds without input or an active conversation: backlight 0, ST7789 Sleep In, LVGL tick timer stopped and LVGL task suspended. |
| CPU | ESP-IDF automatic power management, maximum 160 MHz / minimum 40 MHz, tickless idle and automatic light sleep while the screen is asleep. Active pages retain performance locks. |
| Wi-Fi | Station listen interval 20. XiaoZhi idle uses MAX modem sleep; connecting, recording and answering use NONE. Leaving restores the shared MIN policy. |
| Buttons | Shared scan period 50 ms, one scan for debounce. The entire first wake gesture is consumed, including delayed clicks and long presses. |
| Audio | 15 seconds without recording/playback suspends ES8311 and stops I2S TX/RX, including microphone input. Screen wake restores the prior format, microphone gain and shared volume; the next audio operation retries restoration if needed. |
| Background work | Screen-off skips status/battery/profile UI refresh and XiaoZhi drawing. USB idle polling drops to once per second. No periodic heap report is added. |
| Wake | Restore panel, LVGL task/timer and saved brightness; zero brightness falls back to 80%. Exit restores the display before the page can be deleted. |

`lvgl_port_stop()` alone is insufficient with the pinned LVGL release: the
disabled timer handler returns a 1 ms delay. The BSP holds the LVGL lock and
suspends the captured LVGL worker as well; resume occurs under the same lock.
Codec suspend failures remain retryable instead of being reported as asleep.

The board has `BSP_I2S_PA_CTRL=-1`, so there is no MCU-controlled amplifier power
switch. Codec standby is supported; cutting amplifier supply power is not.
USB attachment holds the ESP-IDF no-light-sleep lock to preserve serial setup
and flashing. Battery-only operation is required for automatic light-sleep
and current measurements. An active setup hotspot can also limit Wi-Fi savings.
Listen interval 20 can add roughly 2–3 seconds of idle-message latency depending
on the access point; it is not an end-to-end latency guarantee. The requested
4–6 mA saving requires a current-meter measurement. See
[Espressif's Wi-Fi low-power guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/api-guides/low-power-mode/low-power-mode-wifi.html).

### Protocol and memory

`xiaozhi_app.cc` adapts the upstream discovery, WebSocket v1/v2/v3 and MQTT/UDP
protocols. UDP audio uses AES-CTR and rejects old sequence numbers. TLS uses
the ESP-IDF certificate bundle. Remote packet lengths, message assembly,
session identifiers and service addresses are bounded. An unsupported
fragmented WebSocket message closes the connection for an explicit retry.
Since 2.4.9, MCP exposes device status, mini-app launch, sound search/playback,
QR display, badge switching and the existing brightness setting. See
[device control](assistant-control.md) for schemas, lifecycle handling and
validation. It cannot replace firmware; app switches end the conversation.

One worker owns network, NVS and audio. Buttons update atomics. MQTT callbacks
only assemble bounded control messages into a bounded queue. UI access holds
the LVGL lock. Stop waits for producers to exit; a timeout retains the page.
Opus encoder and decoder are initialized alternately in one preallocated work
area on the no-PSRAM C3. Version 2.1.3 reserves this area before MQTT/TLS opens
and uses the standard in-place Opus APIs exported by the existing
`esp_audio_codec` library; no second codec implementation is linked.
The voice worker reserves 28 KiB of stack for SILK encoding. The UI pool is
32 KiB; all existing page/return-overlay lifecycle tests run with that same
limit. TLS allocates I/O buffers dynamically. Certificate verification and 16 KiB
incoming records are retained; outgoing fragments use 2 KiB, enough for the
bounded MQTT controls and 1,516-byte WebSocket audio frames.
Wi-Fi uses the upstream C3 receive budget (3 static / 6 dynamic buffers and
a 3-frame BA window), dynamic management buffers and flash-resident RX code.
This recovers internal RAM without introducing another network manager.
Voice Keychain shares `esp_audio_codec` 2.5.0; the reference libopus sources are
kept for offline tools but excluded from the device link to avoid duplicate
Opus symbols. Both applications retain their existing 16 kHz mono audio format.

`font_xiaozhi_14.c` contains 7,540 ASCII/GB2312 glyphs, generated from Noto Sans
SC at 14 pixels, 1 bit per pixel. The 182,101-byte bitmap stays in flash. Rare
characters outside this character set may use the font's missing-glyph fallback.

The existing application, voice and five-profile partition addresses are
unchanged. Preserve NVS and profile partitions during an upgrade. A device that
was flashed with another application must have its actual partition table
inspected and incompatible data backed up before installing the badge layout.
Missing profile partitions and failed mappings now return an empty profile
instead of dereferencing an uninitialized bank.

## Reply playback in 2.1.2

Incoming Opus packets enter a 4 KiB variable-length queue, allocated only while
receiving an answer and released before microphone recording. Playback starts
with approximately 180 ms buffered; short answers drain on TTS stop or after
300 ms. Network bursts are drained before blocking I2S writes, and the UDP
mailbox holds 16 packets. TTS completion waits for both the network quiet period
and an empty playback queue so the final syllable is retained.

The WebSocket receive area is allocated only for WebSocket connections, saving
about 2 KiB with MQTT. The decoder is allocated before the compressed queue to
avoid splitting the large free block required by Opus on the no-PSRAM C3.
The display transfers ten rows per block instead of twenty, freeing another
4,800 heap bytes. The copied UI snapshot uses spare space in the LVGL pool.
These changes preserve a sufficiently large contiguous decoder allocation,
including when the shared hotspot is enabled; total free bytes alone are not
enough to establish that an Opus allocation will succeed.

The audio worker posts a copied snapshot without waiting for the display lock;
a 100 ms LVGL timer applies the latest snapshot on the UI task. It is removed
on page exit and stops with LVGL during screen sleep. Unchanged backgrounds
are not invalidated. Volume is written to the codec only when it
changes. Active conversation servicing uses a 1 ms loop delay; idle servicing
retains 50 ms. The existing 60-second screen sleep and active-audio Wi-Fi/CPU
performance policy remain in place. Buffering trades a small start delay for
tolerance of network jitter; it cannot conceal an arbitrarily long outage.

## Validation

- `tests/test_xiaozhi.c`: URL/header rejection, binary frame bounds, UDP replay
  protection, UTF-8 truncation, volume bounds/mute, repeat-key coalescing,
  audio-safe persistence, failed-write retry and exit flushing.
- `tests/test_xiaozhi_buffer.c`: Opus duration, prefill, short tails, starvation,
  circular-buffer wraparound and non-destructive overflow rejection.
- Adding `BADGE_XIAOZHI_PLAYBACK_PROBE=ON` to the device probe requests a fixed
  cloud greeting and checks received/decoded packet counts and write timing.
  This probe sends no microphone audio. Disable it in delivered images.
- `tests/test_profile_store.c`: safe reads after missing partitions or mapping
  failure, alongside existing five-profile transactional storage tests.
- `tools/render_radio_host.py`: home and all four mini-app headers match pixel
  for pixel across four themes, Wi-Fi states and battery values through 100%.
  It also checks copied/coalesced UI snapshots and 100 page lifecycle cycles.
- `BADGE_XIAOZHI_DEVICE_PROBE=ON` enables a private bench-only build. Adding
  `BADGE_XIAOZHI_VERIFY_ALL_VOICES=ON` decodes every retained voice clip.
  The bench tests synthetic PCM encoding/decoding before
  checking service discovery. It does not automatically record the microphone.
  Both this flag and `BADGE_RADIO_DEVICE_PROBE` must be OFF in delivered images.
- `tests/test_badge_power.c`: 15/60-second boundaries, active-audio exclusion,
  timestamp wraparound and complete wake-gesture suppression.
- Adding `BADGE_XIAOZHI_POWER_PROBE=ON` tests real idle deadlines, a frozen LVGL
  tick, MAX modem sleep and microphone recovery. It reads short 60 ms microphone
  frames into RAM only; no recording is saved or sent to a server. All probe
  flags are disabled in the delivered firmware.

Build/host/device results must be reported separately. A codec test or successful
activation response alone is not acceptance of a real spoken cloud conversation.

Version 2.1.0 bench measurement on 2026-09-15: with the shared hotspot and 16 kHz audio open,
free heap was 64,108 bytes before the encoder and 39,468 bytes with it allocated.
Thirty synthetic 60 ms frames encoded in an average 23,912 microseconds per
frame; decoding produced the expected 1,920 PCM bytes. The worker retained
3,204 bytes of stack margin and returned to the home page. All 726 retained
voice clips (119,735 packets) decoded successfully with the shared decoder.
These measurements do not include a live cloud connection.

Version 2.1.1 USB bench acceptance on the same date: PASS for the real 15-second
codec suspend (register readback), 60-second panel standby, frozen LVGL tick,
MAX modem-sleep selection, 50 ms scan configuration, zero-brightness fallback,
three codec/microphone/volume restore cycles, and a full wake gesture followed
by a successful 1,920-byte microphone read and return home. A regression covers
clicks delayed by screen rendering beyond the former 350 ms suppression window.
Battery-only automatic light sleep, actual current savings and access-point
message latency still require unplugged/instrumented measurements.

Version 2.1.2 final USB/cloud probe on 2026-09-15, with the shared hotspot on:
113 received Opus packets were decoded/written, with no observed UDP sequence
gaps. The maximum interval between one completed PCM write and the next write
was 23,132 microseconds; the compressed queue peaked at 692 bytes. Free heap
was 19,392 bytes after decoder creation and 18,660 bytes at reply completion.
The probe returned home successfully. A prior synchronous-caption trial had
an 84,028-microsecond maximum; the two greetings were not identical audio.
The final host render test passed copied/latest-snapshot behavior, all themes,
ten-row display transfers and 100 lifecycle cycles. These measurements verify
the cloud greeting transport/playback path, not subjective listening quality,
long microphone conversations or adverse-network endurance.

The opt-in `BADGE_XIAOZHI_CONVERSATION_PROBE` uses a supplied 16 kHz mono
16-bit WAV converted by `tools/make_xiaozhi_probe_pcm.py` into a build-only
header. It runs that fixed input through the real encoder and cloud auto-stop
path for two turns, without reading or uploading microphone audio. The PCM and
probe flag are excluded from production images.

Version 2.1.3 device acceptance on 2026-09-15: two consecutive automatically
ended turns passed using a fixed locally synthesized Chinese sentence. Both
recognized inputs contained 39 UTF-8 bytes. The replies decoded/wrote 154/154
and 131/131 packets with no observed UDP sequence gaps; maximum write gaps were
25,635 and 21,063 microseconds. The 24,524-byte shared codec area was allocated
once before TLS. The worker retained 2,856 bytes of stack margin and returned
home (97,976 free heap bytes). Host tests passed multiline input/replies,
whole-line automatic paging, copied UI snapshots, four themes and 100 page
create/destroy cycles. Natural microphone pauses, room noise, acoustic echo and
long conversation endurance remain to be assessed with a human speaker.

The 2.1.4 opt-in conversation probe also checks idle volume application, NVS
readback, restoration after an MCU restart, and app re-entry after changing the
shared codec cache. It checkpoints `xz_vol_probe/original`, restores the original
volume and erases that test key on success, then runs two fixed cloud speech
turns. These probe paths are excluded from production. Host rendering covers
four themes, eight states, alternating captions, whole-line paging, error
priority, partial redraw and 100 UI lifecycle cycles.

Final 2.1.4 device acceptance on 2026-09-15 passed idle volume application,
NVS readback, restoration after MCU restart, and re-entry after changing the
shared temporary volume. The test restored the original value. Two fixed speech
turns played 127/127 and 119/119 received packets, with no UDP sequence gaps.
Maximum write intervals were 23,238 and 24,762 microseconds; queue peaks were
697 and 698 bytes. Worker stack margin was 2,824 bytes and return-home succeeded.
An initial 14,208-pixel instrument redraw showed one 95,768-microsecond gap.
The final design retains static outer geometry and updates only the 1,435-pixel
central waveform, verified across all four themes. This uses fixed test speech;
ambient noise, extended natural dialogue and subjective acoustic quality remain
unverified.

## Simplified conversation screen in 2.1.5

The outer dial, English subtitle and repeated footer are removed. A small
voice waveform and brief state leave more room for captions: six visible lines,
with five-line page steps and one overlapping line. Page numbers appear only
for multi-page text. Volume is shown for three seconds after a change.
The shared header, themes, remembered volume, continuous speech and navigation
remain unchanged. The waveform still redraws only 1,435 pixels.

## Composition refinement in 2.1.6

The compact layout now pairs a bold XiaoZhi title on the left with a small
interlocking voice-loop mark on the right. A fine accent rule connects the
header composition to six-line captions. Footer actions align to opposite
edges. No additional text panels or persistent animation are introduced.
Only the 1,435-pixel waveform center redraws with audio energy; the loop mark
uses integer line segments. The shared 28-pixel font adds the two title glyphs.

## Voice-first field and progressive captions in 2.1.7

The voice field occupies the main screen area. Thirteen tapered bars respond
directly to measured input/output PCM energy, without delayed easing, woven
strands or layered glow. It is an audio-reactive visual, not a frequency spectrum
or biometric voiceprint. Only changed 4 by 144 pixel column strips are invalidated
(at most 7,488 pixels per energy update); unchanged energy requires no redraw.
Active audio snapshots are posted at a 50 ms minimum interval, directly after
input measurement and output writes. The UI consumes them every 50 ms while
listening, thinking or speaking, and every 100 ms otherwise. These are scheduling
intervals, not a guaranteed end-to-end latency; PCM framing and task scheduling
still apply. There is no idle animation, and the existing LVGL sleep stop remains.

Captions use a small two-line viewport. Input appears one complete UTF-8 code
point at a time using a local clock. Reply reveal advances against PCM duration
written to the shared audio output, with an anchor including audio already
queued when the sentence arrives. The service supplies sentence text without
word timestamps: 150 ms per non-ASCII character and 80 ms per ASCII character
are approximate pacing, not exact speech alignment. Audio-clock progress gates
reveal; after playback ends the remaining text finishes on a local clock.
A new sentence replaces the previous source; long text scrolls on whole-line
boundaries to keep the newest two lines visible. It is not a full transcript
history. Volume persistence, explicit start/pause, shared header and sleep
behavior remain in place. UI work stays on LVGL, outside the audio worker.

`xiaozhi_caption.h` is host tested for multibyte boundaries, elapsed-time gates
and malformed tails. Actual LVGL tests cover stalled PCM clocks, copied
snapshots, gradual input/reply, two-line scroll, theme headers and lifecycle.

Audio processing uses priority 5, above the shared LVGL task at priority 4.
Blocking I2S reads/writes and the one-millisecond servicing delay yield time
back to rendering. Captions keep a fixed left origin during reveal to avoid
shifting earlier letters as each new character appears.

Validation: ESP-IDF 5.5.3 production build and merged layout checks passed
(application 2,553,664 / 2,883,584 bytes). The complete static host gate passed.
Actual LVGL rendering on four themes consumed an active snapshot within 50 ms
of simulated time, flushed 7,488 pixels for a changed energy value, and zero for
an unchanged value. UTF-8 reveal, two-line scrolling and 100 lifecycle cycles
per theme passed. On-device fixed-speech testing completed two automatic turns:
149/149 and 155/155 packets played, with no reported UDP sequence loss.
The maximum measured gap between audio writes was 73,992 / 84,449 microseconds;
these results do not establish glitch-free playback. Volume save, reboot restore,
reentry restore and return home passed. Physical sound-to-display latency and
long-session behavior have not been instrumented in this iteration.

## Caption timing correction in 2.1.8

Final recognition now appears in full immediately, using the existing two-line
viewport and whole-line scroll for long input. It no longer types behind a reply.
The UI mailbox preserves a recognition presentation before a coalesced reply on
the next UI tick; errors and canceled/cleared input take precedence. This is one
frame of presentation ordering, not a reading-time pause in the conversation.

Sentence text is posted immediately rather than waiting for the periodic audio
snapshot. Replies show an opening with a 180 ms reading lead before the first
PCM write. Reveal then uses 90 ms per non-ASCII code point and 40 ms per ASCII
code point, catching up all due characters in a single label update after a slow
frame. A stalled playback clock does not accumulate additional reading lead.
These replace the 2.1.7 rates; they are approximate pacing because the service
provides no word timestamps. Late server text cannot be displayed before it
arrives, and physical output/display alignment is not guaranteed.

Host checks cover immediate full recognition, recognition/reply mailbox bursts,
pre-audio opening text, multibyte catch-up, stalled playback, two-line scrolling,
shared headers, the 7,488-pixel voice update bound and page lifecycle.

Validation: ESP-IDF 5.5.3 build and image/layout verification passed (production
application 2,553,936 / 2,883,584 bytes), as did the complete static host gate
and actual LVGL tests. Hardware fixed-speech tests completed 172/172 and 153/153
packets with no reported UDP sequence loss, automatic listening, volume restore
and return home. Maximum inter-write gaps were 222,531 and 16,472 microseconds;
this run does not prove glitch-free playback or physical subtitle synchronization.

## Short-lived connection reuse (2.6.4)

Returning to the badge or settings keeps a healthy MQTT/TLS connection for up to
120 seconds. Audio buffers/codecs, the 28 KiB voice worker, UDP socket and page
are released. Returning starts a fresh server hello/UDP session over the retained
transport; it does not claim to preserve conversation history. MQTT keepalive is
owned by its client task. Hidden messages are discarded, never rendered or
executed as tool calls. WebSocket transports are closed on exit.

Entering a mini app releases the retained connection first. Disconnection,
low free heap (below 48 KiB) and the idle deadline also release it; cleanup runs
in the navigation task outside the LVGL lock. Wi-Fi remains system-wide.
Validated service configuration is cached only in RAM for ten minutes. A failed
cached connection refreshes discovery once; activation-required results are not
cached. No service credentials are stored in badge profiles or NVS.

The opt-in `BADGE_XIAOZHI_REUSE_PROBE` exercises real service handshakes, repeated
entries, idle wake coexistence, mini-app release, disconnect and idle expiry.
It disables microphone uploads and must be OFF in distributed firmware.

Validation on the connected badge: six real server handshakes, including two
retained-transport handshakes (207/477 ms from worker ready to reuse completion),
one mini-app release, one injected MQTT disconnect and the actual 120-second
expiry passed. Discovery ran once; local wake remained listening with roughly
61 KiB free heap while parked. Volume was unchanged. This probe does not validate
acoustic recognition, speech playback after reuse or long-duration server stability.

## MCP registration ordering and subtitle regression (2.6.5)

The 2.6.4 session filter incorrectly rejected MQTT MCP `initialize` arriving
before audio `hello`. This prevented tool discovery, including random playback.
Transport-level read-only discovery now accepts `initialize`, `tools/list` and
initialization notifications before hello. Calls, speech and stale session
messages retain their existing gates; hidden pages still discard incoming data.
The navigation owner now deletes a joined voice worker synchronously, so its
28 KiB stack is available immediately when reopening the conversation.

The subtitle parser and final renderer suppress whole tool-progress markers,
including markers without ellipsis and standalone `%s` / `%s...` placeholders.
Normal percentages and sentences explaining format strings are preserved.

On-device tests sent four fixed synthetic utterances (no microphone recordings):
cold battery query, reused-connection reminder query, then two random plays.
Both queries called actual tools; reply audio decoded 50/50 and 39/39 packets.
Random clips 196 and 108 reached PCM output on separate tool calls. All four
internal progress captions were omitted. Native LVGL checks verify placeholder
suppression while retaining recognition text and `100%`. These tests do not
measure real-speaker recognition accuracy or long-duration network stability.

## Current presentation (2.6.9)

The face replaces the earlier waveform described in the historical sections above. See [expressions and shared clock](face-clock.md) for SVG sources, offline conversion, audio timing and the HH:mm header.

## 2.6.22 connection readiness and reading handoff

MCP discovery describes stable tools only. The immutable catalog is emitted as raw JSON instead of allocating a full temporary cJSON tree; the host suite parses the wire result and checks all 16 tools and its size bound. Full reading snapshots are returned by `self.yao.get`, never appended to the tool description. In the initial interpretation turn, an omitted ID returns the exact page-confirmed snapshot; an explicit different ID is rejected. This preserves full names, both hexagrams and all six lines without caching an old reading in the server's tool catalog.

Tool-registration readiness belongs to the MQTT transport: parking retains it, closing clears it. Each audio conversation still requires a fresh hello and UDP keys. On a fresh connection, capture starts after hello and successful catalog transmission, avoiding capture during discovery. The 24 KB shared codec arena is reserved before the handshake and retained throughout discovery, since freeing it can allow smaller allocations to fragment the only sufficiently large block before the first reply. Missing hello or discovery produces a named handshake timeout.

Opt-in `BADGE_XIAOZHI_CONNECT_PROBE` exercises six ordinary entries (one retained-connection reuse and repeated fresh discovery) with synthetic silence (no microphone upload). `BADGE_YAO_DEVICE_PROBE` exercises two page-to-AI handoffs using a fixed synthetic request. Both must be disabled in production. See the validation record for measured results; these short tests do not establish long-duration Wi-Fi stability.

### Reproduced failures and verification

On the affected board, the old reading-specific catalog was 8,396 bytes and its transmission repeatedly received socket error 104 (connection reset by peer). The exact upstream message-size limit has not been independently established. The new stable catalog is 6,809 bytes. A second failure occurred on first reply playback: total free heap was 54,624 bytes but the largest block was 22,528 bytes, smaller than the 24,524-byte codec arena. Holding that arena alone exposed the serializer's allocation peak, so catalog output now uses immutable raw JSON and one preallocated 8 KB output buffer, with no expanding print buffer.

The fixed device successfully performed two reading handoffs, including retained-connection reuse. Both `self.yao.get` responses contained full names and six lines for each hexagram. The two playback checks decoded 134,400 and 129,600 samples before the test returned to the same reading. These are bounded playback checks, not a claim that two entire lengthy interpretations were played. The host catalog-construction test runs under a 1 KB temporary-allocation budget and validates the emitted JSON.

Three ordinary entries also passed eight-second listening checks, including two MQTT connection reuses; only one catalog registration was required. Synthetic silence was uploaded, not microphone recordings.

### 2.6.37 discovery memory fix

Device traces reproduced an abort in `std::string::resize(8192)`: the string
requested 8193 bytes while the largest free block was 8192 bytes. Replacing
it with a checked heap allocation prevented that abort but still exposed
`esp-aes: Failed to allocate memory` during catalog transmission. Catalog
serialization therefore borrows 8192 bytes from the existing worker stack in
a non-inlined helper, releases the JSON tree before TLS transmission, and
returns before audio encoding. Ordinary tool replies avoid the additional
throwing string copy. Output overflow is reported as a session failure.

Location invalidation retains its worker lease until the old HTTP request
finishes, preventing overlapping TLS workers after re-entry or reconnect. XiaoZhi blocks new location requests and waits (up to ten seconds) for active HTTP cleanup before allocating its worker and TLS session; location resumes after XiaoZhi stops.
Release builds reject stale non-size-optimized configurations by default.

The 2.6.37 device probe passed all six entries: five catalog registrations,
one retained-connection reuse, and eight seconds of stable listening per entry.
It sent 630 synthetic-silence frames and ended with `DONE failed=0`; no
microphone recordings were uploaded. The discovery helper retained at least
16,536 bytes of measured stack headroom in this run. Windows equivalents of
the repository's static and host checks passed. This does not verify lengthy
spoken conversations or USB recovery after screen-off; the probe temporarily
keeps the screen awake without changing the saved timeout.

### 2.6.39 playback memory reuse

The decoder and jitter queue now share the existing codec arena. Its size is
`max(encoder_bytes, align8(decoder_bytes) + queue_bytes)`. Only the decoder
and queue coexist; listening reinitializes the same arena as an encoder.
This removes the per-answer queue allocation without reducing its 4096-byte
packet capacity or changing buffering thresholds. Layout tests cover alignment,
integer overflow, future codec sizes, queue wraps and decoder/tail guards.
Memory failures log the allocation stage, requested bytes, total free heap and
largest free block, without recording conversation text or credentials.
The original intermittent error has not yet been reproduced in this run;
a normal reply completed before this change.

On-device validation completed five synthetic-speech replies (55, 140, 58, 58
and 58 packets, all decoded), including a 22-second listening pause and one
intentional transport failure followed by recovery. The codec arena remained
24,524 bytes: encoder 24,524, decoder 17,784, queue offset 17,784. Two full
reading handoffs also returned both hexagrams and six lines each, decoded
128,640 and 133,440 samples in bounded playback checks, and returned to the
same reading. No allocation failure occurred in either test. These checks do
not reproduce or conclusively resolve every cause of the reported intermittent
warning; diagnostic stages remain enabled in production for follow-up.

### 2.6.40 codec reservation lifetime

The reported failure was reproduced in 2.6.39: `opus_arena` requested 24,524
bytes with 75,956 bytes free but only a 23,552-byte largest block. Repeated OK
retries failed at the same allocation. The 2.6.39 playback-queue saving did not
address this entry/connection failure.

Reserve the codec arena before creating the 28 KiB voice task, and keep it
through transport close/retry while that task exists. Transport shutdown now
releases network resources without releasing the codec block. Parking when
leaving the conversation and final session destruction release the block.
The allocation is still dynamic and does not permanently reduce RAM available
to other mini apps. The stability probe additionally asserts that five replies
and an injected reconnect use exactly one successful codec allocation.

Device stability validation for 2.6.40 completed five synthetic-input replies
and decoded all 322/322 audio packets, including 22 seconds of listening and
one deliberately injected transport failure followed by two more replies.
The codec arena was allocated exactly once, with no allocation failure or
crash; the saved volume was preserved. No microphone recording was uploaded.
Minimum task-stack headroom was 4,616 bytes, so the original 28 KiB stack
remains unchanged instead of trading stack safety for heap space.

All six re-entry rounds passed as well: one connection reuse and five tool
catalog registrations, 619 synthetic silence frames in total, and eight
seconds of stable listening per round, ending with `DONE failed=0`. Windows
equivalents of the static gates and host tests passed, as did release image
layout/resource checks. Production disables all diagnostic probes and the
app-only upgrade preserves user data. Long-running human conversations and
USB recovery after screen-off are outside this validation; USB recovery
still requires separate investigation.

### 2.6.41 reading handoff and contiguous-memory pressure

The user reproduced a reading-to-AI startup failure in 2.6.40: codec allocation
succeeded, but the voice task needed 28,672 bytes with 71,368 bytes free and
only a 27,648-byte largest block. Reserving the codec first did not resolve
the need for two large contiguous allocations.

The old handoff serialized the full reading into JSON, copied it into launch
storage, and then copied it into the session string. The new handoff owns a
compact `yao_record_t` snapshot containing the question, six lines, result ID,
cast timestamp and location. Startup transfers ownership without copying full
text. When AI queries the pinned reading, immutable text data reconstructs
both full names, judgments and all six lines of each hexagram. Query time is
fresh while cast time remains fixed. The snapshot survives eviction from the
four-entry history cache. The 28 KiB voice task stack is unchanged.

All six device reading handoffs passed, alternating retained and released
connections. Each returned full text, decoded reply audio, and navigated back
to the same result; 953,280 samples were decoded in total. Each bounded
playback check returns after reaching 128,000 samples; this does not claim
six full long-form readings were played. No allocation failure, session
fault or crash occurred. Host checks cover all 4096 outcomes plus compact
snapshot/JSON equivalence, survival after history eviction, preserved cast
time and refreshed query time.

Five normal synthetic-input replies also passed with 306/306 packets decoded,
22 seconds of continuous listening, and recovery after one injected transport
failure. The codec arena was allocated once, saved volume was unchanged, and
minimum task-stack headroom was 4,540 bytes. Production disables all probes.
Long-running human conversations and every memory state produced by other
page operations have not been exhaustively validated.

The production 2.6.41 app-only update was installed and hash-verified while
preserving user data. The user repeated the originally failing path and
confirmed a normal reply. Production logs confirmed snapshot handoff,
connection and playback with no allocation failure. The serial capture was
then stopped and the port released.
