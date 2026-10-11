<p align="right"><a href="setup-navigation.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# First setup and return navigation — 1.9.0

Current routing (2.2.0): Home UP opens Mini Apps, including from the first-setup
screen. The mini-app list remembers Home or Terminal as its parent. Edit Profile
now displays its hotspot guide directly, with OK to toggle the hotspot in place.
The setup, spacing and historical validation sections below record 1.9.0;
the return policy describes the current implementation.

When no saved profile exists in any of the five slots, Home shows the setup guide and requests the protected configuration hotspot automatically. A configured device continues to boot into its badge. Clearing the final configured profile activates the guide again without deleting any other settings.

The guide displays the hotspot name/password, `http://192.168.4.1`, the steps to save and display a badge, and a reminder to remain connected when the phone reports no Internet. Down opens the terminal and Up opens settings. OK starts the hotspot if it is closed or startup failed. During initial setup the ten-minute idle shutdown is suspended; manual closure is respected. Saving the first profile restores normal idle shutdown with a fresh ten-minute period, preserving the current configuration session.

Opening the network guide from Settings or Edit Profile also starts the hotspot if necessary. Reopening an active hotspot does not rotate its password or disconnect clients. Save the desired profile in the browser; if editing an inactive slot, select **Display this badge** afterward.

## Contextual return navigation

Hold OK to request a return. On Badge Home it still opens the badge switcher.
No new double-hold or multi-button gesture is required.

| Location | Return behavior |
| --- | --- |
| Terminal, Mini Apps list, system settings, badge switcher and QR | Return directly to the recorded parent; QR retains its short-press return |
| Radio settings/volume/city/timer and Voice Keychain clips/volume | Return one internal level directly, without an exit chooser |
| Mini-app root or XiaoZhi conversation, with different entry and badge destinations | Choose the named entry page, Badge Home, or Continue |
| Safe direct launch whose entry already is Badge Home | Return directly; do not offer duplicate destinations |
| Muse recording/local wait or an active XiaoZhi session | Confirm the interruption with Continue selected by default; a Home-only exit has two choices |

Destination labels identify Mini Apps, Terminal, XiaoZhi settings or the retained
hexagram instead of an ambiguous previous page or home. A navigation chooser
normally selects the entry page. Interruption confirmations select Continue.
Up/down moves, OK confirms, and another long OK cancels; Continue is also a
visible cancel action. Choosers neither time out into an exit nor nest a second
confirmation. Radio/sound exit text explains that playback stops. An open
chooser does not pause playback, automatic strikes or cloud work. Muse exits
cancel only local recording/waiting; an already submitted cloud task may continue.
Settings retain their original immediate-save or OK-to-save rules. No new
unsaved-edit or data-loss prompt is invented for RAM-retained hexagrams.

Return queries are side-effect free. A failed app query defers return rather
than treating an unknown internal page as an app root. Choosers retain their
source, destination and app identity; changing any of those or the internal
layer invalidates the old choice. Completion of a request does not invalidate
its otherwise unchanged route. Non-urgent voice wake and assistant commands
cannot replace a page while its chooser is open; reminders keep their existing
priority and dismiss it through the lifecycle owner.

Opening/closing a chooser and returning a level consume the initiating key's
release and delayed click/double-click tail until a fresh press. Releases of
presses already delivered to an app still reach that app once, so a Muse UP
release is not lost; new modal UP presses do not record. Exit displays progress,
joins workers before deleting UI, and on failure retains the screen with retry
destinations and Stay on this page selected. Staying after a failed stop does
not promise that a partially stopped producer resumes.

Host coverage is in `tests/test_badge_navigation.c`,
`tests/test_badge_return_input.c`, `tests/test_radio.cc`, `tests/test_voice.c`,
`tests/test_xiaozhi.c` and `tests/test_menu_font.py`. Run
`python3 tools/render_return_host.py` on a POSIX host with gcc to render the
production LVGL overlay over a mounted Muyu page and check two/three choices,
destination labels, safe defaults, progress/retry layout and 200 create/destroy
cycles in a 32 KiB pool.
RGB565 previews are in `build/return-ui/`; this does not validate physical keys,
audio or cloud cancellation. Device acceptance must check
internal returns during playback, both exit destinations, quick-launch
Home-only exits, Muse release/cancel handling, XiaoZhi-to-hexagram return,
failed-stop retry, reminder preemption and repeated screen lifecycles.

## Badge spacing

The name region now starts at screen `(20,116)`, department at `(20,158)`, and title at `(20,184)`. The information panel starts at `(14,102)`, providing 14 pixels of top space before the name image and 6 pixels on the left. Existing RGB565 crops and storage formats stay unchanged; saved badges immediately use the new spacing. The browser preview uses exactly the same region table, checked by a Node test.

## Validation — 2026-09-15

| Field | Result |
| --- | --- |
| Build | PASS — native ESP-IDF 5.5.3 build, all five images and resource checks; application 1,755,008 bytes |
| Host tests | PASS — repository/static gate, setup/idle policy, five-slot emptiness and actual parent navigation, chooser confirm/cancel and game stop, Node configuration suites, production LVGL render |
| Device tests | PASS — segmented installation and normal startup, five profiles/active slot/Wi-Fi readback unchanged, real WPA2 hotspot serves the exact updated configuration page and profile readback |
| Unverified | Empty-device first-boot sequence and physical button operation; existing profiles were retained throughout hardware testing |

The partition table and both audio images are byte-identical to 1.8.0; installation only updated program images. UI tests preserve every source pixel in the repositioned profile regions. Under the same 40 KB LVGL pool, 100 voice/Muyu/return-chooser cycles left 17,000 bytes free versus 16,984 before, with a 22,680-byte peak. This is a UI heap test, not an audio or total-device heap measurement.

The static gate ran in WSL; native Windows ran the firmware build/merge/verification. The temporary WLAN profile and hotspot test connection were removed afterward, and COM6 was released. No full flash erase or profile clearing was performed.

Configured Home supports holding Up to open XiaoZhi directly. Its exit target
is Badge Home; XiaoZhi opened from its settings returns to those settings, and
interpretation returns to the retained hexagram. Ordinary mini-app list launches
return to that list. My QR Code precedes system settings in the terminal.
