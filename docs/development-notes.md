# MeshInk development notes

Historical development and recovery notes retained from the README for engineering reference. These notes describe the test builds at the time and are not current release instructions.

### test.56 recovery safety
Before any destructive restore, copy the verified SD backup to a computer or second SD card. Test restoration on a spare T5 where possible. The backup checks journal headers and Meshtastic node snapshot checksums; damaged histories are never silently advertised as backed up. The restore checks the staged copies before replacing live flash and keeps existing rollback safety copies from being overwritten. Successful restores free their temporary copies. CI passing is not proof of full hardware recovery.


### test.57 confirmation and reboot display
Backup and Restore both require an explicit confirmation screen with **OK / Cancel** describing the one-shot reboot before protocol startup. No operation is queued until OK. After confirmation, a non-interactive **REBOOTING...** progress screen is refreshed *before* native worker shutdown, so its deferred save/stop operation no longer looks like a frozen confirmation screen. Restore revision warnings and the optional operation cancellation remain available. The Meshtastic startup FreeRTOS queue assertion observed after the test.56 backup is being investigated separately and must pass a hardware reboot regression before release approval.


### test.57 SD mount crash correction
After successful offline B+R, normal startup previously called `map_tiles_warm_storage()`, which attempted to reinitialize an already-mounted SD filesystem. This could invoke `SDFS::end()` / `SPIClass::endTransaction()` and trigger a FreeRTOS queue assert. The board storage backend now reuses readable existing mounts and only tears down an SD mount when a real remount is needed. A separate confirmation/progress screen is shown before flushing the protocol for the requested reboot.

For future hardware crash reports, the matching primary-build `firmware.elf` is now retained as a separate CI artifact named with its exact commit SHA, so backtraces can be resolved without a speculative rebuild.


### test.58 human-readable backup filenames
When the device has a trusted RTC, new SD backups record the **local date and time at the UI confirmation tap**, including the current timezone: `meshink-tastic-YYYY-MM-DD-HH-MM.BAK` for native Meshtastic and `meshink-core-YYYY-MM-DD-HH-MM.BAK` for MeshCore. Same-minute duplicates gain a `-01` through `-99` suffix; no existing archive is overwritten. Without trustworthy time, the proven numbered `MTBK0001.BAK` / `MCBK0001.BAK` fallback remains. Existing archives keep their existing names and are still visible and restorable. SD long filename support is enabled explicitly for the production cache64 build. The on-disk archive *content* format is unchanged.


### test.59 restore-file selection audit
Reviewed the entire archive basename path: SD enumeration, stable in-memory entry storage, touchscreen row selection, full content/revision checks, persistent NVS reboot request, and pre-protocol restore opener. Fixed two selection risks: the list now always retains the **newest 24 valid backups regardless of FAT directory enumeration order**, and correctly ranks `-01`, `-02` collisions after their original minute backup. Full long names are shown over two lines with their suffix intact, and the selected archive filename is repeated on the final Restore confirmation before OK can schedule a destructive operation. Original numbered archives remain supported. Automated host tests and firmware build are required; hardware restore of the new long-name archive is still outstanding.


### test.60 WarmStore evidence and plain logs
Official Meshtastic `/prefs/warm.dat` header warnings require a read-only investigation before attempting repairs. During native Meshtastic initialization (after SPIFFS mount, before NodeDB), the serial log reports `[MT-WARM]` filename, file size, count of header bytes read, snapshot header magic and record count for **both** `/prefs/warm.dat` and the possible `/prefs/warm.dat.tmp`. No node identities/keys are printed, and neither file is modified or deleted. The unchanged, pinned Meshtastic source receives an **idempotent, build-only** patch disabling ANSI colour escapes in both official logger paths. Multiple threads can still interleave serial characters; no logging serialization change is attempted.


### test.61: Serial log line integrity
On the H752-01 ESP32-S3 HWCDC USB serial port, native Meshtastic's `log_to_serial` previously emitted its timestamp, severity, thread tag and message as separate `printf`/`Print::write` calls, causing interleaving and potentially dropped characters under simultaneous MeshInk e-paper and PhoneAPI logs. The pinned official Meshtastic source remains unchanged: an idempotent **build-only** adapter formats each record in bounded stack storage and performs a single `dest->write(buffer,size)` call. Arduino HWCDC serializes that buffer write with its own driver lock. This preserves severity, text and other Bluetooth/syslog paths and builds on the test.60 ANSI-colour removal. Native log-line format host checks added; actual concurrent USB log quality still needs hardware observation. No changes made to WarmStore or backup/restore logic.


### test.62: USB CDC output under Meshtastic PhoneAPI startup bursts
The test.61 hardware log shows Meshtastic DEBUG/INFO lines no longer interleaving, but two MeshInk `Serial.printf` diagnostics are partially missing and long RF replay lines were clipped by a fixed 160-byte body buffer. ESP32-S3 Arduino HWCDC defaults to a 256-byte TX ring; `HWCDC::write` can return a partial write on congested USB. Set a 4096-byte TX ring once, **before** the unified boot's `Serial.begin`, avoiding replacing a live queue. Increase native Meshtastic body buffer to 384 bytes and atomic record cap to 512 bytes. No extra blocking retries, radio-worker policy, logging level or data-store changes. Host tests cover long log lines. Test.62 hardware log remains required to verify that the rare incomplete MeshInk `Serial.printf` lines are gone.


### Test.63: full-wipe restore failure triage and safe repeatability
The 2.1.1-test.61 user full-wipe MeshCore restore failed AFTER archive validation/staging and returned `Restore aborted; original data recovered`. The subsequent retry refused `Existing restore salvage files; refusing overwrite`. Code investigation found an independent rollback-cleanup defect: virtual NVS stages (`@node-name`, `@device-settings`, etc.) copy old values into `/rstNN.old` but successful rollback removed staged `.tmp` and the txn marker **without deleting those `.old` copies**, stranding files that permanently block future restores. Fixed that for **new** rollback transactions, leaving any pre-existing orphan `.old` files untouched. Added archive/device-settings preflight and phase/slot/path diagnostics to distinguish file rename failure from NVS-apply failure; no keys or payloads are logged. On the first welcome screen, `RESTORE FROM SD` and `NEXT` are neither displayed nor clickable until the user selects a protocol. **Do not delete old salvage files or retry the existing wiped device until copies have been secured and the first failure investigated.** No WarmStore or radio changes.


### Test.63 follow-up: leave test.61 rollback evidence untouched
A second attempt on the full-wiped test.61 device refused `Existing restore salvage files; refusing overwrite`. Correct safety behaviour for the legacy `/rstNN.old` namespace, but the snapshots were orphaned by the old successful-rollback cleanup defect. New test.63 restore transactions use **isolated** `/r2sNN.tmp` and `/r2sNN.old` names; they never touch preexisting `/rstNN.old` files and reject *any* preexisting files in their own namespace before writing. Crash recovery accepts both old and new stage prefixes. Future complete rollbacks clean their NVS rollback snapshots. To preserve evidence, first make a **read-only** full flash backup of the current device before flashing the update image and attempting restore. No automatic salvage deletion, SPIFFS reformat or full-wipe is performed.


### Test.64 automatic orphan cleanup for SD restore retry
New restore attempts first verify the selected archive's format, protocol, manifest, and entry CRCs. The boot routine also completes rollback of any committed `/restore.txn` before entering the restore. With no active transaction, MeshInk automatically deletes only exact abandoned rollback files `/rst00..59.{old,tmp}`, `/r2s00..59.{old,tmp}`, and `/restore.txn.tmp`, then checks internal storage space and begins a fresh transaction. An earlier failed rollback no longer permanently blocks subsequent SD restores and no full wipe/manual deletion is required. An **unresolved** active transaction or real flash-I/O error still reports a failure rather than deleting required recovery copies. All new transaction stages, including setup-state NVS, use the `/r2sNN` namespace; boot recovery still accepts legacy `/rstNN` journals. This is a deliberate change from test.63's preserve-forever orphan policy; only verified orphan scratch is removed, not live protocol files, any SD backup, or valid active recovery data. CI includes a host pathname allowlist test and static restore ordering tests. Hardware restoration remains to be confirmed.
