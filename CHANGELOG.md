# Changelog

## Unreleased

### Added

- Diagnostics page in the app: a report for bug reports, without addresses, device names or ids. Read it on the console,
  open it from a phone or PC at `http://<console-ip>:47996` while the page is open and the host runs, or fetch
  `/data/prosperosunshine/diagnostics.txt`.
- The home page says when the firmware is untested, and why capture failed: HDR on, capture in use, firmware not
  supported, or the step that failed.
- The host writes its steps to `/data/prosperosunshine/host_diag.log`, which survives a crash.
- Adaptive triggers: effects a game sends reach a DualSense on Moonlight for PC. Other clients ignore them. If a stream
  ends during an effect, the trigger stays stiff until the next connection, because Moonlight doesn't reset it.

### Fixed

- "Start at boot" failed with "Cannot read the Payload Manager's autoload.txt" once the Payload Manager had saved that
  file itself, for root only. The host now makes the change when the app may not, so the host has to run for it.

## 0.1.2 (2026-10-31)

### Added

- Rumble: classic two-motor vibration from the console reaches the Moonlight client, including short pulses.
- Light bar: the colour a title sets on the controller reaches the Moonlight client.
- HEVC (8-bit, firmware 13.00): clients that support it can stream HEVC. `hevc_mode = 1` in `sunshine.conf` turns it off.

### Changed

- Bitrate cap raised from 15 to 50 Mbit/s, for H.264 too. The encoder now opens at the client's bitrate instead of being
  raised afterwards, which is what stopped it above 15 Mbit/s.

## 0.1.1 (2026-10-31)

### Added

- PS button for clients without one, such as Moonlight's on-screen controller: hold Select for half a second. A short
  press sends Create on release. `back_button_timeout` in `sunshine.conf` sets the time; -1 turns it off.

### Fixed

- README: a second ELF does not restart the host, it exits and the old host keeps running.

## 0.1.0 (2026-10-30)

First release. Tested on PS5 firmware 13.00; other versions are untested.

### Added

- Sunshine-compatible host as an ELF payload: pairing, 1080p H.264 video from the console's encoder, game audio, and
  a virtual DualSense per Moonlight gamepad.
- The stream follows the client's frame rate (30 or 60 fps) and bitrate (up to 15 Mbit/s). Other resolutions are
  delivered as 1080p; Moonlight scales them.
- The console announces itself over mDNS, so Moonlight lists it without entering an address (confirmed on iPhone).
- Control socket for status and PIN entry, bound to the console itself by default.
- PS5 app `PPSA47989` for starting and stopping the host, autoload, settings and PIN entry.

### Known limits

- H.264 SDR 4:2:0 only: no HEVC, AV1 or HDR.
- One stream at a time.
- Rest mode is untested.
