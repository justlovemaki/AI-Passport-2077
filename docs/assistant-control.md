<p align="right"><a href="assistant-control.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Xiaozhi device tools (2.6.23)

## Current capabilities

There are 19 tools. Version 2.6.23 extends the existing reminder tools and adds profile and system operations. Older sections below are historical; this section supersedes their single, volatile reminder behavior.

- Eight persistent reminders: 1–86400 second delays, UTC+8 clock times, calendar dates, daily or selected weekday repetition. Creation requires a synchronized clock. NVS retains schedules across restarts and power loss. Shutdown does not ring; startup resumes after clock synchronization. Overdue one-shot reminders ring, recurring occurrences missed by more than ten minutes are skipped, and already pending alerts remain until acknowledged. Simultaneous alerts are shown one at a time. OK acknowledges. Chimes use 90% without changing application preferences.
- `self.reminder.set`: choose `seconds` or `time:"HH:mm"`; optionally add either `date:"YYYY-MM-DD"` or `weekdays:[1..7]`, Monday=1. Text is limited to 32 characters/96 UTF-8 bytes. `get` lists IDs, local time, next occurrences, remaining seconds and weekdays. `cancel(id)` cancels exactly one entry.
- `self.badge.get_profile(number?)` defaults to the active badge and returns name, department, title, revision and editability. `self.badge.update_profile(number,revision,field,value)` modifies `name`, `department` or `title`. A freshly read revision prevents overwriting concurrent web edits.
- Text metadata and display pixels commit together through the existing dual-bank storage. Name: 20 characters; department/title: 24. Two display rows use ellipsis on overflow; metadata retains the full string. Only V4 profiles can be edited; re-save older profiles through the configuration page. Missing font glyphs are rejected. Avatar, QR, brand and bottom lettering assets are preserved.
- `self.system.action`: `home` returns directly to home; `shutdown` stops network/audio/display and enters deep sleep, with UP as the wake button. This is software sleep, not physical disconnection of power. Both end the current conversation.

Validation: `test_badge_alarms.c` (define `BADGE_ALARM_SERVICE_TEST` for persistence failure tests), `test_profile_text.c`, `test_profile_edit.c`, `test_profile_store.c`, and `test_xiaozhi_tools.c`. Bench-only `BADGE_ASSISTANT_DEVICE_PROBE` adds temporary reminders and enables a timer wake during its shutdown test; it must be OFF in releases. Discovery retains flash-backed raw JSON and a preallocated send buffer, with host checks for a catalog below 7 KiB and construction allocations below 1 KiB.

## Historical behavior: 2.5.x

The existing MCP connection exposes fourteen tools for device status, apps,
sounds, QR display, badges, brightness, Xiaozhi volume, radio and reminders.
No separate Wi-Fi, profile, volume or asset configuration is added.
Cloud speech recognition and tool selection still require the configured
Xiaozhi service to be connected and support device MCP tools.

| Tool | Arguments / behavior |
| --- | --- |
| `self.get_device_status` | Volume, actual Wi-Fi state, brightness, configured badge numbers, active badge, QR presence, last action result (0 pending/initial, 1 success, -1 failure). |
| `self.apps.open` | `app`: `zen-muyu`, `leo-radio`, `voice-keychain`, `cyber-yao`, or `muse`. Opens the existing app. Radio and Muse require Wi-Fi. |
| `self.audio.search` | `query` matches title/category; optional `offset`. At most six matches with clip IDs per page. Empty query browses the catalog. |
| `self.audio.play` | `clip_id` from search. Opens the matching sound category and starts that clip. |
| `self.badge.show_qr` | Shows the active badge's uploaded QR. Missing QR returns a tool error. |
| `self.badge.switch` | `number` 1–5, configured slots only. Returns to the badge home. |
| `self.display.set_brightness` | `percent` 20/40/60/80/100, persisted using the existing system setting. Conversation continues. |

Requests use strict argument validation; an unknown tool or invalid schema
does not enqueue an action. Missing resources and a busy mailbox are reported
as tool errors. Search returns real catalog IDs rather than guessed indexes.

Navigation actions send an acceptance response before committing, so stopping
Xiaozhi cannot discard the response; acceptance is not completion. In-place volume,
brightness and reminder changes wake the main task immediately and await completion
for up to two seconds, returning execution=completed/pending/failed. If sending an
in-place result fails, the action may already have taken effect: query before retrying.
A failed navigation acknowledgement cancels its uncommitted action. Disconnects and
app exit invalidate unconsumed tickets. A critical section protects the mailbox and a navigation
snapshot; the voice worker never reads live navigation or touches LVGL.

The navigation task rechecks mutable profile data, stops Xiaozhi, waits for
its worker to release microphone/codecs/network resources, and only then
deletes its screen and starts the target. Stop failure preserves the existing
screen. A target startup failure attempts to return home. Late execution
failures are logged; Xiaozhi cannot announce them after its session has ended.
Brightness stays in Xiaozhi; app/page switches end voice conversation. Voice
control is not available in the background of the launched mini app.

## Validation

`tests/test_xiaozhi_tools.c` uses the project's cJSON and generated voice
catalog, checks discovery, arguments, error replies, bounded searches,
deferred/failed/stale tickets, and selection of all 726 audio entries. Build
with `BADGE_CONTROL_HOST_TEST` and link `xiaozhi_tools.c`, `badge_control.c`,
`voice_navigation.c`, the generated `voice_catalog.c` and ESP-IDF's `cJSON.c`.

`BADGE_CONTROL_DEVICE_PROBE=ON` is a temporary bench-only firmware option.
It drives tools from the navigation task, tests app lifecycles, a short sound,
available QR, badge selection, brightness save/restore and cancellation.
It restores the original selected badge and brightness. Normal release builds
must use `BADGE_CONTROL_DEVICE_PROBE=OFF`. This local probe does not test cloud
intent recognition; spoken commands must also be checked with the live service.

## 2.5.0: volume, names, stations and reminders

- `self.xiaozhi.set_volume(percent)` accepts 0–100, with zero meaning mute.
  It changes only Xiaozhi reply volume through the existing deferred-save and
  exit-flush path. Radio, woodfish and soundboard preferences remain independent.
  Status identifies `volume_scope=xiaozhi`; this is not a global volume tool.
- `self.get_device_status` adds `battery_percent` (-1 means unavailable),
  `active_badge_name` and `badge_names`. Battery sampling updates about every
  30 seconds while awake and pauses when the screen sleeps.
- `self.badge.switch` accepts either `number` or `name`, never both. Names must
  exactly match saved profiles. Duplicate names require choosing a number.
  The navigation task rechecks names before switching to avoid stale selections.
- `self.radio.search(query, offset)` searches curated station names, six per page.
  `self.radio.play(station_id)` uses a returned ID and starts that preset without
  waiting for initial city discovery. Additional online city-directory entries
  are not currently included in voice search.
- `self.reminder.set(seconds, text)` creates one countdown of 1–86400 seconds,
  with at most 32 characters / 96 UTF-8 bytes. Existing reminders are never silently
  replaced. `self.reminder.get` reports remaining time and `self.reminder.cancel`
  cancels it. These tools keep the conversation open.

The main task owns the reminder and uses monotonic time, so leaving Xiaozhi,
switching apps and light sleep do not cancel it. At expiry it wakes the display,
stops the active app, waits for audio ownership to be released, and displays a
theme-colored reminder over the home screen. Failed app stops are retried before
starting the chime. Checks run about every 250 ms, or every second with the screen
off; stopping network/audio workers can add seconds, so this is not precision timing.

The three-tone chime uses a fixed 90% volume, restores the previous shared codec
volume, then suspends audio. It never changes an app's saved preference. OK dismisses
the reminder to home; interrupted playback does not resume automatically. A failed
chime-task allocation still leaves the visual reminder. Reminders live only in RAM:
reboot or power loss clears them. Daily repeats and wall-clock alarms are not supported.
The existing GB2312 font may not render rare characters or emoji.

### Added validation

`tests/test_badge_reminder.c` covers duration boundaries, exact expiry, duplicate
rejection, cancellation and long uptime. `tests/test_xiaozhi_tools.c` adds duplicate
names, volume bounds, actual station search, offline rejection, reminder lifecycle
and failed-send checks. Link `radio_tool_catalog.cc` and `radio_presets.cc` into this
test, compiling C and C++ sources separately.

The bench probe adds saved-volume reentry, name switching, a specific station,
reminders after leaving Xiaozhi, cancellation and display-sleep wakeup. It restores
the original badge, brightness, Xiaozhi volume and radio settings. Disable the probe
in production; real speech interpretation and external station availability still
need on-site confirmation.

## 2.5.1: tool-progress captions and completion receipts

An on-device cloud trace captured `% self.xiaozhi.set_volume...` in a TTS sentence
event before the normal reply. It is a tool-progress marker, previously displayed as
a caption, rather than an unsubstituted C format argument. The caption ingress now
filters only a complete `% self.method.name...` marker (including Unicode ellipsis).
Ordinary percentages, method names in prose and explanations of `%s` remain intact.
Audio and final replies retain their existing paths.

Tool descriptions clarify that a specific radio/sound play already opens its app:
do not call apps.open first. Compound tasks must apply settings before the final app
switch. Badge number/name exclusivity is also expressed in the schema. Completion
receipts for in-place actions prevent the common stale-read/second-command-busy race.
A timeout remains pending rather than claiming completion. App switches still end
the conversation.

`tests/test_xiaozhi.c` distinguishes progress markers from ordinary percentages and
code. Tool tests cover wakeup, completion, cancellation, failure and stale receipts.
`BADGE_TOOL_CLOUD_PROBE` is a temporary diagnostic flag using fixed synthesized audio,
never real microphone input. Raw diagnostics stay in private local logs. Production
must disable it. The generated cloud_probe_pcm.h lives in the build directory and
is neither a release resource nor included in production firmware.

## 2.6.3: Random sounds and repeated wake-up

`self.audio.play_random(query)` selects and plays a random matching clip directly. Omit `query` or pass an empty string for all clips; otherwise filter by title or category. No prior search or app-open call is required. With multiple candidates the previous random selection is excluded; a single match may repeat. No match returns an error without navigation. The response reports the real clip ID, title and category and retains acknowledgement-before-execution. Explicit ID playback is unchanged.

Once soundboard initialization finishes and no sound is queued or playing, the navigation task joins the player, releases its decoder and stack, keeps the list visible, and resumes local wake detection. Key actions first join the detector before restarting playback. Queued requests retain audio ownership; an old completion cannot release a newer request. Wake-up works again after playback or manual stop. A detected phrase leaves the soundboard for Xiaozhi; closing the conversation returns to the originating home or app list. Wake remains paused during playback; wooden fish and radio retain their audio exclusion policy.

2.6.3 validation: host tests cover random selection bounds, category filters, no consecutive repeat with multiple matches, invalid arguments/no matches, and retaining newer audio ownership when an old request completes. Device probes use real microphone frames and injected wake triggers to check idle-list recovery, playback exclusion, recovery after manual stop, and three playback-to-wake cycles. Five detector start/stop cycles finish with the same idle heap as baseline. Acoustic wake accuracy and cloud selection of the random tool still require real conversation checks.

## Direct page transitions (2.6.6)

Voice actions that open a mini app, play a specific/random clip, select a radio
station, show a QR code or switch badges no longer render the badge home as an
intermediate page. The navigation task joins audio producers while preserving
the conversation screen, then destroys it and creates/renders the destination
under one LVGL lock. Badge switching renders the selected badge only once.
In-place actions (volume, brightness, reminders) keep the conversation as before.
If destination preparation fails, the existing conversation producer is restarted.
If an app fails to start after its page is created, it is stopped and the badge
home is restored. All task joins stay outside the LVGL lock.

The opt-in control probe counts home renders around each action, checks the
badge identity rendered, and injects preparation/start failures. Production builds
must disable the probe. The probe temporarily disables background wake for the
screen-off reminder check and restores its original setting at completion.

Hardware validation passed all three app entries, specific/random audio,
QR display, preset radio, and badge switching by number/name. Instrumented
successful actions rendered home zero times, except badge switches which
rendered the target badge exactly once. Preparation failure restored the chat;
injected app-start failure restored home. Existing volume persistence, failed
send cancellation, reminders, screen-off wake and setting restoration passed.
The probe uses real UI/audio lifecycle and device tool dispatch, with cloud
connection disabled; it does not measure physical panel timing with a camera.

## 2.6.20: Cyber Yao

`self.apps.open` also accepts `cyber-yao` for manual casting.
`self.yao.cast` and `self.yao.get` return structured results without navigating
or interrupting the conversation. See [Cyber Yao](cyber-yao.md) for replay
limits, parameters, interpretation handoff and storage behavior.
