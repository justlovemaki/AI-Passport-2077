[简体中文](auto-screen.zh_CN.md) · **English**

# Automatic screen sleep

From the home menu open Settings → Automatic screen sleep. Choose Always on,
30 seconds, 1 minute (default), 2 minutes or 5 minutes with Up/Down. OK saves
the selection across restarts. Long OK returns directly to Settings without
saving the tentative selection, preserving the previous choice. This preference applies to
all pages, independently of whether XiaoZhi background wake is enabled.

Listening, thinking and speaking keep the screen awake. Waiting for a local
wake word does not. A recognized wake word opens XiaoZhi and wakes the display.
When the screen is asleep, the first physical button gesture only wakes it;
the next gesture operates the current page. Reminders also wake the screen.
Screen sleep does not pause radio or soundboard playback.

The shell alone controls the panel, backlight, LVGL and display performance
leases. The exclusive audio worker holds separate performance leases, suspends
the codec and both I2S channels after 15 silent seconds, and restores performance
and audio before playback. Leaving an audio app suspends audio immediately.
Being on a mini-app page no longer blocks light sleep by itself. Boot also
suspends the codec before the first playback.

Version 2.6.26 uses MAX modem sleep in silent screen-off standby while retaining
the Wi-Fi connection. Playback and conversation restore responsive networking.
Enabled voice wake continues sampling and detection, retaining its performance
requirements; it cannot achieve microphone-off standby power. Automatic wooden
fish strikes, radio and sound playback continue with the screen off. Silent
screen-off wooden fish and soundboard tasks poll less and skip unused UI work.

Wi-Fi connection eligibility does not depend on the current page or modem-sleep policy. Key/wake probes are coalesced and respect a 15-second grace after a new station attempt, so repeated navigation does not continuously disconnect and rescan that attempt. AI idle/busy and screen-off standby can still request a probe. An active setup hotspot deliberately suspends automatic roaming/retries to protect configuration; explicit configuration-page scan/connect requests remain available.

The read-only USB `info` response exposes screen/audio sleep and application
performance leases in `power`, plus the configured Wi-Fi mode in
`wifi.powerSave`. Reading it does not wake the display. This is not a current
measurement. USB connection protection is configured to prevent automatic light
sleep while connected. However, a board diagnostic run lost USB replies after
screen sleep while the COM port remained visible; enumeration alone is not proof
of a live diagnostic channel. Verify replies before collecting and temporarily
keep the display active for affected USB sessions, restoring normal behavior when
diagnostics end. This observation does not establish a fixed USB/low-power path.
Measure battery standby current with the computer disconnected; an enabled setup hotspot also
has different power requirements from station-only standby.

The saved choice uses `screen_timeout` in `cyber_badge_v1` NVS. Missing or invalid
values fall back to one minute without clearing existing data. Saving failure
retains the prior active preference and leaves the selection page open.

Validation: `tests/test_badge_power.c` covers choices and deadline arithmetic;
`tests/test_badge_power_runtime.c` exercises the real power coordinator with
stubbed time/display/audio (including wake retry, owner handoff and microphone
restore); `tests/test_badge_navigation.c` covers selection, save and cancel;
`tests/test_badge_network_runtime.c` covers probe grace, boot-attempt deduplication,
page-independent power policies and setup-hotspot isolation.
