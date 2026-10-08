<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Offline badge configuration

Edit `page.html`, `profile-core.js` and `app.js`, then run `python tools/build_config_page.py` from the repository root. CMake runs the same generator when inputs change. `index.html` and `main/assets/configure.html.gz` are generated standalone/embedded copies. No CDN, external API or cloud service is used.

The serial and hotspot transports share protocol 2. On connection the editor reads persisted identity, theme, icon, avatar, QR, Wi-Fi status, and non-secret Muse configuration status. Wi-Fi passwords are not returned; an empty password retains an existing password only for the same SSID. Supported station credentials are open networks or personal passwords of 8–63 UTF-8 bytes. The board supports 2.4 GHz Wi-Fi.

Hotspot setup: System → OK Network → OK Enable. Join the displayed WPA2 hotspot with its per-session random password and open `http://192.168.4.1`. The server is restricted to the AP interface, rejects foreign origins, and requires a per-session header token for API calls. Ten minutes of HTTP inactivity closes the hotspot. Uploaded data is committed in two banks, with transaction ownership and timeout shared across USB/HTTP. Wi-Fi credentials use a separate NVS blob and are not embedded in profile exports.

Muse setup accepts the owner's SDK token and an optional IPv4 HTTP CONNECT proxy. The token is write-only: the protocol returns only `tokenSet`, never the value. An empty token preserves the saved token. Changing the token clears the old Muse account pairing; changing or clearing only the proxy preserves pairing. Configuration writes are rejected while the Muse app owns its session.

Brand text is rasterized at 148 × 44, icon at 32 × 32, and avatar at 72 × 88. The identity card is 208 × 148 RGB565. QR images are decoded locally, regenerated with integer-sized modules and a quiet zone, then re-decoded to verify identical bytes before accepting the image. Codes too dense for 192 × 192 at two pixels per module are rejected. QR display retains black/white contrast regardless of theme. Actual phone/WeChat scanning must still be checked on the physical display.

## Third-party libraries

- jsQR 1.4.0 from the official npm package, upstream https://github.com/cozmo/jsQR, Apache-2.0; see `vendor/jsQR.LICENSE`.
- Project Nayuki QR Code generator, upstream https://github.com/nayuki/QR-Code-generator/blob/master/typescript-javascript/qrcodegen.ts, retrieved 2026-09-15, MIT; see `vendor/qrcodegen.LICENSE`. Compiled with TypeScript 5.9.3; both libraries minified with Terser 5.44.0. These runtime copies are bundled inline.
- `assets/images/cyber-badge/test-qr.bin` encodes the public fixture `https://example.com/test-contact`; it contains no real contact information.

## Quick QR display

Starting with 1.3.1, DOWN on badge home immediately displays the saved QR. DOWN, OK or long OK returns to badge home. Opening QR from the terminal returns to that terminal instead. Games remain accessible from the employee terminal.

## Nearby networks (1.3.2)

Connecting scans automatically; Refresh List starts another scan. Up to 16 strongest networks are shown, merging duplicate SSID/security pairs. At most 64 raw records are read per scan. Hidden SSIDs are omitted and can be entered manually. WEP, WPA-only and enterprise networks cannot be selected. Choosing a different SSID clears any unsaved password to avoid applying it to another network.

The badge scans through shared USB/HTTP `wifi_scan` and `wifi_scan_results` operations. A network worker scans while the page polls asynchronously; buttons and protocol services keep running. Failed/busy scans retain the previous list and offer refresh. Scanning never automatically selects or saves a network.

## Five badge profiles (1.6.0)

The configuration page stores five independent badges. Select a slot in the badge library, edit its name, brand, colors, avatar and QR, then save. Editing or saving another slot does not change the displayed badge. Use **Display this badge** to activate a saved slot. The page distinguishes the edited slot from the active slot and reads back saved data when connected. Clear affects only the selected slot. Wi-Fi settings are shared by the device.

On badge home, **double-click OK** to open the badge list. UP/DOWN selects a slot, OK displays it, and long OK returns. Empty slots are marked and cannot be activated from the list. Single OK still opens the terminal; DOWN still shows the active badge's QR. The active slot is remembered across restarts. Existing profile data stays in slot 1 during the upgrade.

Both USB and hotspot configuration support all five slots. Refresh an already-open configuration page after upgrading. Writes explicitly target a slot and check its loaded revision, so changes made by another connection require a fresh read. Updating the displayed slot refreshes the screen; saving an inactive slot preserves the active identity and QR.

### Multi-profile protocol

Card IDs are zero-based (`badge: 0` through `4`). `info` and `read` accept an explicit `badge`; omitting it reads the active card. `info` returns `badge`, `badgeRevision`, `activeBadge`, `badgeCount`, and a `badges` array with each slot's ID, revision, configured flag, name and brand. `badges` returns just the catalog and runtime revision.

`begin` and `clear` require `badge` on five-slot firmware. Include `baseRevision` from the last read to reject stale writes. After `begin`, `chunk`/`commit` remain bound to that slot and transaction owner; they cannot be redirected by switching the display. `badge_select` with `badge` persists the active slot. Switching and another write are rejected while a transfer is active or the UI has not released references to the previous committed images; retry after the current operation completes. Card revisions are independent; `revision` remains a runtime UI refresh counter.

The active badge palette also controls the built-in game in firmware 1.7.0 and later. Save the active profile to refresh a running game; saving another slot leaves the current theme unchanged until that card is displayed.

## Name wrapping and card numbers (1.7.1)

Names wrap automatically into at most two lines, with English word boundaries preferred. Long names use a smaller font; only names exceeding both lines at the minimum size get an ellipsis. The complete name remains in metadata. The saved image region stays 112 x 32, preserving the storage format and other fields. Existing cards must be read and saved once in the updated editor to regenerate their name image. Home and preview show `1/5` without zero padding; picker row numbers are 1–5.

## Information spacing (1.9.0)

Name, department and title now use the updated firmware positions while preserving source crops and profile format. Existing badges require no resave for hardware spacing; reload the configuration page to view the matching preview.

## Company-name typography (2.6.31)

Company names use embedded Noto Sans SC 700 Latin proportional glyphs, with the existing Chinese fallback. The editor reduces font size uniformly to fit a 144-pixel safe width inside the 148-pixel brand image; it does not stretch or squeeze characters. Existing badge images remain unchanged until that badge is saved again. USB and hotspot editors share this renderer.
