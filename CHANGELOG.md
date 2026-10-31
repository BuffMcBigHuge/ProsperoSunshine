# Changelog

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
