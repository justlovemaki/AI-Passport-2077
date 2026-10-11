<p align="right"><a href="cyber-yao.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Cyber Yao and XiaoZhi interpretation

Version 2.6.20 adds a themed six-line divination mini-app and two MCP tools.
The core derives from [CyberYAO](https://github.com/XadillaX/CyberYAO/tree/3c4780feea04737e6e59b6e1849e591710e6656c).
Its MIT notice is retained in [LICENSE](../assets/cyberyao/LICENSE).
No upstream fonts, pictures, audio, recovery firmware, network manager or standby page are included.

## Use

Open the mini-app from the mini-app list. Think of a question, then press OK
six times, one three-coin cast per press. Lines grow from bottom to top.
The completed page shows the original and changed hexagrams, marks moving
lines. From 2.6.21, up/down browse each hexagram’s complete judgement, all six line passages and the special Qian/Kun texts, retaining primary/secondary reading marks. Unchanged hexagrams are shown only once. Scrolling stops at both ends.
Hold Down to start over. At the app exit, hold OK to choose Mini Apps, Badge Home or Continue when the destinations differ; a safe Home-only launch returns directly. See [return navigation](setup-navigation.md).

On a completed result, press OK to request XiaoZhi interpretation. A working
system Wi-Fi connection and an activated XiaoZhi service are required. The conversation retains a complete snapshot of the selected result, returned by `self.yao.get`: ID, original/changed hexagrams, six lines, moving lines, reading rule, complete judgements, all six line passages for each hexagram and Qian/Kun special texts. Both `name` fields and reading references (`hexagram_name`) use full names such as Water over Fire (Ji Ji), Water over Mountain (Jian), and Qian as Heaven; `short_name` preserves the source abbreviation and `number` remains the hexagram index. From 2.6.27, the complete source passages remain available as context. By default, the model is asked to give the conclusion, trend, cautions and practical suggestions in plain language, informed by the primary reading and moving lines, without reciting or translating every passage. Original text or line-by-line explanation is provided only when explicitly requested; unchanged hexagrams are not interpreted twice. This policy applies to page handoff, voice casting and result queries; the cloud model generates the actual wording. After tool discovery, a fixed synthetic spoken request asks XiaoZhi to query and interpret the retained result through the normal manual listen/audio/stop protocol. The 4.7-second Opus clip occupies about 5.3 KB, is uploaded directly, never played locally, and does not record the microphone. Cloud detect rejects long text and did not answer short triggers in device tests, so it is not used for this handoff. The caption shows both hexagram names. The app does not recast or open a transient home page. The initial interpretation turn rejects cast calls in firmware. Choose Return to hexagram in XiaoZhi's exit chooser to return to the retained result; an active session defaults to Continue. A missing
question is discussed in the subsequent conversation. Source passages and
AI interpretation are distinct; the application does not generate predictions
from a separate prewritten commentary database.

## Question, time and automatic location

When neither the record nor the conversation identifies a question, XiaoZhi asks for the specific matter and necessary background, waits for an answer, and interprets the same result without recasting or inventing a question.

From 2.6.29, no region or longitude entry is required. Obtaining a Wi-Fi IP without a valid cache marks a lookup pending. App entry reuses region and longitude obtained within the past hour and leaves an active request running. With a valid clock, awake screen, idle audio hardware and enough memory, the worker queries ipwho.is and falls back to ip-api.com for an approximate city and longitude. Playback or screen sleep defers it; background wake-word listening does not. Failures retry every 15 seconds. Reconnects and network changes reuse a still-valid cache. Casting never waits for location.

Region and longitude are cached in RAM for one monotonic hour after a successful lookup; rebooting clears them. Clock adjustments do not change this lifetime. Old manual settings are ignored and no flash writes occur. Time values are never cached: the system clock keeps running, and each cast calculates solar time from its actual UTC timestamp, longitude and equation of time. Expiry queues a refresh when resources permit. Timeout, service failure, missing fields, task creation failure or insufficient memory never blocks casting. Without a location, interpretation uses the existing system time without apparent solar correction and does not ask for manual entry. The casting page shows queued, fetching, retrying, calibrated or waiting-for-Wi-Fi state; the configuration page mirrors it.

Manual casts capture `cast_time` at the sixth throw; voice casts capture it on completion. The snapshot includes UTC, explicitly labeled Beijing time (UTC+8), and any available approximate region, longitude, apparent solar time and equation of time. If a manual cast finishes while lookup is still running, that record accepts the location when it arrives and recomputes solar time from the original cast timestamp; handoff to XiaoZhi checks once more. `current_time` separately reports the query instant.

The [NOAA approximate solar equations](https://gml.noaa.gov/grad/solcalc/solareqns.PDF) compute apparent solar time from UTC plus four minutes per longitude degree plus the equation of time, handling date rollover and leap years. The result is local solar wall time, not a UTC timestamp. System clocks, reminders and header time remain unchanged. No sexagenary calendar or Na Jia chart engine is added; interpretation still uses source judgements, line passages and moving-line rules.

An invalid clock produces `clock_valid=false`; an unavailable location produces `location_set=false`. Invalid clocks must not lead to invented time-dependent claims, and missing locations do not block interpretation. Location is marked `network_ip_approx`, never precise positioning. The lookup service receives the device public IP. Source texts, question, time and region are sent to the configured XiaoZhi service as context; the cloud model generates the final response.

## Tools and storage

- `self.yao.cast(request_id, question?)`: performs all six casts in one call,
  returning structured data without leaving the conversation. Identifiers
  accept 1–48 ASCII letters, digits, underscores or hyphens. Questions accept
  at most 96 Unicode characters / 384 UTF-8 bytes.
- `self.yao.get(result_id?)`: retrieves a retained result without drawing
  randomness. Omit the ID for the latest result.

The most recent four completed records are held in RAM across page changes
and AI reconnects, and are lost on reboot. Repeating an identifier still in
this cache with the same question returns the exact saved result. Reusing it
with a different question fails. Replay protection is limited to these four
records; it is not durable or unbounded. Use get for follow-up questions and
never automatically recast when a result is unavailable.

Manual and voice casts share one core and record store. The navigation task
accesses it while the mini-app is active; the joined/stopped AI lifecycle
transfers ownership to the voice worker. LVGL callbacks never access the store.
Results contain bottom-to-top line values, moving line numbers, original and
changed hexagrams, guaci, and one or two prioritized readings. Rules cover
zero through six moving lines, including Qian/Kun special passages.

## Resource and network behavior

The app uses shared theme, header, footer and caption glyphs. It has no audio
worker, animation timer, independent Wi-Fi credentials or dedicated partition.
Shared RF preparation can start the existing station without connecting to a
network, scanning, opening an AP, stopping or deinitializing Wi-Fi. Random
draws fail until RF is ready; there is no seeded pseudo-random fallback.
XiaoZhi remains online for interpretation and subsequent questions.

## Validation

`tests/test_yao.c` checks all 4096 line combinations, all 64 hexagrams,
reading priorities, coin probabilities, replay, missing entropy, invalid
arguments, result retention and prompt construction. `test_xiaozhi_tools.c`
also checks discovery and zero-navigation-ticket tool responses. Run both
with the core, text table, service and cJSON; define `BADGE_CONTROL_HOST_TEST`
to inject deterministic entropy. UI rendering and actual cloud interpretation
must be reported separately from those host checks.

The result reader starts with the true solar time captured when the sixth line was cast (approximate IP location), followed by Beijing time and the complete texts. UP/DOWN scroll through it. Missing location or an unsynchronized clock is shown explicitly; reopening a result never substitutes the current time or location. The configuration page displays the live time separately.

Returning from XiaoZhi reuses a valid longitude cache. Without one, the navigation task releases the parked AI connection to make RAM available for geolocation rather than leaving it queued behind that connection. Current time, historic cast time and location cache lifetime remain independent.
