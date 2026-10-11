<p align="right"><a href="config-backup.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Configuration-page import and export

The USB/hotspot configuration page contains **Export all configuration** and
**Import and replace configuration** in section 07. Use them before and after a
merged-image installation or intentional erase. Routine firmware updates should
still use the [preserving upgrade workflow](preserving-upgrade.md).

No cloud service is involved. The same embedded/offline page works from desktop
Web Serial and from a phone connected to the badge hotspot. The browser encrypts
and downloads a `.aipcfg` file locally. Wi-Fi passwords and AI credentials are
never shown in form fields or returned by the ordinary settings APIs.

## Included state

A web-page backup contains:

- a consistent 24,576-byte snapshot of the badge's persisted NVS state: saved
  Wi-Fi networks/passwords, active badge, display/sleep preferences,
  XiaoZhi/Muse credentials and pairing, app preferences/counters and reminders;
- the current committed contents of all five badge slots: metadata, theme,
  photo, brand, logo and QR assets, retaining each slot's profile format;
- the source physical badge identity, retained as provenance so cross-device imports can show an explicit warning.

Only committed state is included. Close mini-apps normally before export so
pending preference saves can complete. Live conversations, recordings, unsaved
browser edits, running timers, and RAM-only cast/location history are excluded.
Firmware, bundled audio, the partition table, PHY calibration, eFuses and the
passive NFC tag are not exported or restored.

The web backup stores each slot's currently valid committed bank, not stale
inactive-bank remnants.

## Export in the page

1. Open `configure/index.html` in Chrome/Edge and connect by USB, or open
   `http://192.168.4.1` while connected to the badge hotspot.
2. Close active mini-apps first, then open **07 Configuration import/export**.
3. Enter the same new password twice. It must contain 12–256 characters.
4. Select **Export all configuration** and keep the downloaded `.aipcfg` file.
5. Keep the file and password private and separate. Passwords cannot be
   recovered; data already erased without an earlier backup cannot be recovered.

The device takes the NVS snapshot once, then serves bounded chunks. The page
reads all five committed profiles, checks every region length and SHA-256,
encrypts in the browser, then starts a local download. Export does not write
Flash. Existing backup files on disk are controlled by the browser's normal
download policy; do not distribute or commit them (`*.aipcfg` is ignored).

## Import in the page

1. Install compatible profile-enabled firmware first and reopen its current
   configuration page. An old page left in a browser tab must be refreshed.
2. In section 07, choose the `.aipcfg` file and enter its original password.
3. Select **Import and replace configuration**. The page authenticates/decrypts
   and validates the complete file locally before sending any restore data.
4. Confirm replacement of **all** settings and five badge slots.
5. The page first exports the target's current configuration using the same
   password and downloads `AI-Passport-before-import-....aipcfg`. Confirm that
   the browser saved this recovery file before authorizing the write.
6. Keep power and the browser connection until the profile slots and NVS bytes
   have been verified. The badge then restarts. A phone hotspot may disconnect
   because the restored Wi-Fi/hotspot state takes effect.

Wrong passwords, modified/truncated files, unknown profile formats or unsupported
firmware stop before import begins. Another physical badge is accepted when its
firmware exposes the same backup capability and profile layout. The page warns
that this is a complete migration before creating the target recovery file.
Profile slots use their existing alternating-bank transaction and readback checks. NVS
is restored last: the firmware receives all chunks in RAM, checks its SHA-256,
deinitializes NVS, erases/writes only the fixed NVS partition, and schedules a
restart only after success. No application, audio, partition-table, PHY or
eFuse address is accepted by the protocol.

Cross-device import copies all five profiles, Wi-Fi/AI credentials, pairing
records, app state and cloud identity. Two active devices using cloned cloud
identity may conflict, and hardware/service-bound pairing may need to be set up
again. Treat it as an intentional complete migration: review the warning, retain
the target's `before-import` file, and retire or reconfigure the source as needed.

Import is not atomic across all six logical destinations. If profile writing is
interrupted, some slots may already contain the imported value. If NVS writing
starts and USB/power/write/verification fails, do not erase or continue normal
configuration. Reconnect and import the downloaded `before-import` recovery
file with the same password. Recovery is user-driven, not automatic.

## Encryption and hotspot fallback

Page backups use format 1, AES-256-GCM authenticated encryption with random
16-byte salt and 12-byte nonce, and PBKDF2-HMAC-SHA-256 with 600,000 iterations.
Source-device identity, NVS, profile metadata, each asset's label/length/SHA-256
and content are inside the encrypted payload; the outer algorithm header is
authenticated as additional data. Strict size/version/KDF limits are checked
before key derivation/decryption.

Secure contexts use browser WebCrypto. The badge hotspot is plain local HTTP,
where WebCrypto may be unavailable, so the page includes pinned
`@noble/ciphers` 2.4.0 and `@noble/hashes` 2.4.0 MIT fallback code. Random bytes
still come from `crypto.getRandomValues`. Sources/licenses are recorded under
`configure/vendor/`; no CDN is used.

A page authorized through the badge's USB session or random WPA2 hotspot can
explicitly request an opaque NVS snapshot for backup. This is a stronger local
capability than ordinary form reads, but the page never displays the snapshot
or credentials and immediately places them inside the encrypted archive. Close
other pages, use the trusted bundled/offline page, and do not install untrusted
scripts or extensions for this operation.

## Validation

- `tests/test_config_backup_ui.cjs` exercises actual browser AES-GCM/PBKDF2,
  complete profile assets, strict schema and tamper/wrong-password rejection.
- `tests/test_config_backup_flow.cjs` checks that recovery download precedes
  writes, NVS uses bounded chunks, commit is last, cancellation sends no restore
  data, and cross-device migration presents a credential/identity warning.
- Firmware builds validate the web backup protocol and embedded page.

Device acceptance must separately exercise USB export/import, hotspot/mobile
export/import, all five profiles/images, active slot, Wi-Fi/AI/app settings,
reboot, cross-device warning/migration and interrupted-import recovery on a privately
backed-up test badge. Host tests and a successful build are not device results.
