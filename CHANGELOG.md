# Changelog

## Unreleased

### Added

- Firmware 10.x at 1080p, H.264, and on 10.20 also HEVC (8 bit and HDR): the capture record there is smaller, the encoder
  keeps its own default bitrate, and HEVC starts with `cfg+0x24` alone. Measured on a 10.20 console (issue #2): Moonlight
  streams on Android and iOS (tested with H.264) and on Windows (H.264 and HEVC, also HDR). H.264 there comes as 34 slices per
  frame, more than FFmpeg's hardware decoders on Windows take (32), so Moonlight on Windows needs software decoding for it; HEVC
  comes as 17 and decodes by hardware. Other 10.x versions are untested and get H.264 only.
- An **Experimental** section in the app's Settings, for testers on firmware where a function isn't verified: one switch each for
  HEVC (8 bit), HDR (HEVC 10 bit), 1440p and 4K, and a bitrate cap of 150 instead of 50 Mbit/s, all off by default. They go to
  `sunshine.conf` as `experimental_hevc`, `experimental_hdr`, `experimental_large_modes` and `experimental_high_bitrate`. On verified
  firmware (13.00; 10.20 for HEVC and HDR) a switch shows as not needed and changes nothing. The bitrate switch has no effect on any
  10.x, whose encoder keeps its own rate, and says so. HEVC tries `cfg+0x24` alone first, then 13.00's full setup. A function whose every
  try fails is off until the host restarts, and the host offers H.264, 1080p and 50 Mbit/s again: a lower rate or mode takes over in the
  same stream, while a stream that failed on HEVC or HDR ends and the next one works. The
  diagnostics report lists the switches, each function's result and code, and every try (codec, mode, variant, the step that failed and
  its code), and the Home
  page asks for it to be posted, also after a success. None of this has run on a console yet. The switches come from one list,
  `app/src/psapp/experimental_list.h`, so a new lock starts with one entry there.
- `host version=` in `host_diag.log` and the diagnostics report name the build's commit, so a test build can be told from the
  release. Without git data it says `unknown`.
- When the wait for the capture resource ends without it, `host_diag.log` and the report say which case it was: no answer within
  2 seconds, refused (with the value) or taken away, how long it waited and which resources were active. The report also says outright
  when the firmware is refused because the ResourceArbitrator calls can't be found, which no switch changes.

### Changed

- The bitrate cap is 150 Mbit/s on firmware 13.00, up from 50, and stays at 50 elsewhere. Measured with a test client on wired LAN
  and a noise image at 4K: HEVC follows the setting up to about 151 Mbit/s with no missing frames. Not yet tested with Moonlight or
  in a game, and games may stay well below the setting.
- Firmware 10.20 counts as verified: the app no longer calls it untested, and the host knows its ResourceArbitrator offsets.
  Moonlight on Android, iOS, Windows and Nintendo Switch streamed from a 10.20 console.

### Fixed

- Game audio no longer stays silent after the capture is reopened, as happens when a title starts during a stream. The
  microphone read the session's generation before taking the lock that opens its channel; a reopen in between left the new
  channel marked as old, and every later open failed on it until the capture was reopened again or the stream ended.
- Game audio also comes when the capture isn't available as the stream starts, as can happen while the game picked in
  Moonlight starts. The stream used to stay silent to its end; the audio now retries every half second, as after a later
  loss. Not yet seen on the console.
- Picking a game in Moonlight while another one is in front now fails the launch instead of reporting success. The host still
  doesn't close the game in front; before, Moonlight named the game it asked for while the stream showed the other one. "PS5
  Screen" streams whatever is in front.
- A game picked in Moonlight no longer starts while another one is still open in the background. A game opened before the
  settings app stays open behind it, and once the app has closed, the system no longer reports it as running. Starting another
  title could close it, so the launch fails with 409 as with a game in front. The host reads this from `/mnt/sandbox`.
- A start refused while ShadowMountPlus still releases a title (`0x80940033`, about a second after the settings app closed
  itself, seen once on firmware 10.20 with ShadowMountPlus 1.7beta11) is retried for up to 3 seconds when nothing else is
  open. The retry itself is not yet seen working: in the next try the first start went through.

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
