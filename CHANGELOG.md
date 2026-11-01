# Changelog

## Unreleased

### Fixed

- Picking a game in Moonlight while another one is in front now fails the launch instead of reporting success. The host still
  doesn't close the game in front; before, Moonlight named the game it asked for while the stream showed the other one. "PS5
  Screen" streams whatever is in front.

## 0.1.6 (2026-11-01)

### Added

- The console's installed games appear in Moonlight's app list with their covers, after "PS5 Screen". Picking one starts it.
  The host never closes a game: quitting in Moonlight ends the stream only, and with another game in front the new one doesn't
  start.
- HDR: HEVC Main 10 in BT.2020 and PQ when the client asks for it, up to 4K. Measured with the test client and seen on one real
  setup, Moonlight on an HDR TV at 4K in one game. The host sends no HDR metadata, because the capture provides none. `hevc_mode = 2`
  in `sunshine.conf` turns HDR off.
- Touchpad and motion sensors of the Moonlight controller reach the console's virtual pad. Measured as a title reads them; not yet
  in a game that uses them.

### Changed

- The capture check that told users to turn HDR off was about PS VR, as the system's own names for those bits show. The app's message
  now says so.

### Known limits

- Rest mode with a game open and a stream running: in one test the console and the game came back, but the host stayed unreachable
  until it was restarted. End the stream before rest mode.
- Waking the console from Moonlight did not work in the one try, from a PC in the same network.

## 0.1.5 (2026-11-01)

### Added

- HEVC at 1440p and 4K on firmware 13.00, when the client asks for it. The console's fallback to 1080p in some titles applies
  here too. Measured on the home screen, in a quiet game scene and with noise in the console's browser; not yet during active play.
- First attempt at firmware 13.60, for H.264 at 1080p: the capture record there is larger. The layout comes from public projects
  that run on 13.60, and it's untested here, because no 13.60 console is available.

### Fixed

- HEVC no longer skips frames after a key frame or an oversized frame: the frame budget is now as large as the encoder accepts, and
  the bitrate still holds. Measured longest gap under load: about 19 ms, was 33 to 37 ms.
- A 1440p or 4K request falls back to 1080p also when opening or starting the capture fails, not only when the console refuses the
  resource. Others report this failure for a console output smaller than the request, which ended the stream. A real smaller output
  is not tested here.

## 0.1.4 (2026-10-31)

### Added

- H.264 at 1440p and 4K on firmware 13.00, when the client asks for it. HEVC stays at 1080p. If the console refuses the larger
  capture, which two titles did, the stream runs at 1080p instead of failing, also when such a title starts during a stream.
  Measured on the home screen, in quiet game scenes and with noise in the console's browser; not yet during active play at 4K.

### Fixed

- HEVC no longer pauses after each key frame. The encoder skipped frames to make up for it: one per second at 15 Mbit/s,
  100 ms per second at 5 Mbit/s. Key frames now come every 10 seconds instead of every second, for both codecs, and clients
  still get one right away after packet loss.
- Opening the capture is repeated for about a second when the service doesn't list the host's resource as active, seen on
  firmware 10.40. If it keeps failing, the diagnostic report lists the active resources. Whether this helps on 10.40 is not known.
- A stream no longer fails when the capture resource is still listed right after the last session closed it.
- Docs: the host does give paired clients the console's MAC address. Whether Wake-on-LAN wakes the console is untested.

## 0.1.3 (2026-10-31)

### Added

- Diagnostics page in the app: a report for bug reports, without addresses, device names or ids. Read it on the console,
  open it from a phone or PC at `http://<console-ip>:47996` while the page is open and the host runs, or fetch
  `/data/prosperosunshine/diagnostics.txt`.
- The home page says when the firmware is untested, and why capture failed: HDR on, capture in use, firmware not
  supported, or the step that failed.
- The host writes its steps to `/data/prosperosunshine/host_diag.log`, which survives a crash.
- Adaptive triggers: effects a game sends reach a DualSense on Moonlight for PC. Other clients ignore them. If a stream
  ends during an effect, the trigger stays stiff until the next connection, because Moonlight doesn't reset it.

### Changed

- After rest mode the host starts its services again by itself instead of staying unreachable. This worked in two tests on
  the home screen, with no game open and no stream running. An earlier test without this ended in a hung console, for
  an unknown reason, so to be safe, turn the console off instead.

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
