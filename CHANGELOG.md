# Changelog

## 0.2.2 (2026-11-03)

### Added

- **A second player from another device.** A stream from another device no longer gets turned away: it joins the running
  one and gets the same picture and sound, in the codec and dynamic range the first stream opened. Its controller is new to
  the console, which asks "Who's using this controller?", so the friend picks a user or plays as a guest (Add User, Quick
  Play). Every controller after the first is asked this way. When the stream holding the capture ends, a joined one takes it
  over. While another device streams, quitting the app in Moonlight ends only your own stream, so a friend who leaves
  doesn't close your game; the last one to quit ends the app as before. Measured on 13.00 with two test clients at once,
  with H.264 and HEVC, a guest signed in through the second one; a game for two players isn't tested yet
  ([#11](https://github.com/Darkatek7/ProsperoSunshine/issues/11)).

### Changed

- **A second controller on the same client is another player too.** Until now every controller played as the console's
  user. Now only the first does, and the console asks who uses each further one.
- **Waking from Moonlight is documented as not possible.** The PS5 ignores Wake-on-LAN packets, and the console stops the
  host in rest mode, so it can't wake the console either. A Remote Play client such as chiaki-ng can
  ([#10](https://github.com/Darkatek7/ProsperoSunshine/issues/10)).

## 0.2.1 (2026-11-03)

### Added

- **Keyboard and mouse:** Moonlight's keyboard and mouse now reach the console as a virtual keyboard and mouse, so you can
  play with them from Windows. They are added the first time you press a key or move the mouse in a stream and removed when
  the stream ends. Keys move the focus in the console's menus and type into text fields while the on-screen keyboard is open,
  Shift included; text sent from a phone's keyboard is typed as on a US keyboard. The mouse moves the pointer and clicks in
  games that support a mouse, measured in Planet Zoo; the console's browser doesn't use one. Measured on 13.00 with the
  test client, which got `--kbm-seq` for this, and used through Moonlight Web. Wheel, the right and middle
  buttons and firmware 10.20 are not tested yet.

### Changed

- **A new stream from the same device replaces its old one.** The console can only capture one stream, so until now a
  second start was turned away while the first still held the capture. A client that retries when its first attempt
  hangs, like Moonlight Web behind a strict proxy, then waited half a minute or more until the old stream timed out. Now the
  host ends the old stream and the new one takes over within a few milliseconds. A stream from another device is still
  turned away.

## 0.2.0 (2026-11-03)

### Added

- Modes below 1080p on firmware 10.20 and 13.00 (thanks to @EnriqueWood): a client asking for less than 1080p gets whichever of 1280x720, 960x540 and 640x360 is
  nearest to its request, instead of 1080p to scale down, so weak clients and Wi-Fi links have less to carry and decode (a PS
  Vita asking for 960x544 gets 960x540; 1600x900 still gets 1080p). Requests from 1080 lines up get exactly what they got
  before, on every firmware. Measured on 10.20: all three start in H.264, HEVC and HEVC 10 bit at 30 and 60 fps; at the QP the
  host sets for 16 Mbit/s, an IDR of the home screen cost about two thirds of 1080p's at 720p, 40 % at 540p and 20 % at 360p,
  and Moonlight on an iPhone got each mode it asked for. If the system refuses one, the stream falls back to 1080p, as 1440p
  and 4K do, so the client gets what it got before these modes rather than no stream. Elsewhere the new Experimental switch
  `experimental_small_modes` offers them; there a mode that is refused before its first picture also stays off until the host
  restarts.
- On 13.00 these modes needed two more things, measured there: HEVC gets a smaller frame budget per mode (`cfg+0x10c`), and the
  bitrate is capped per mode, because 13.00 won't start them with too many bits per frame: 30 Mbit/s for 360p, 60 for 540p and
  120 for 720p, half of that at 30 fps. A client asking for more gets that much, and the small picture stays. Below 1080p,
  `OpenVideo` is repeated 5 times instead of 25, as above 1080p, so that the fallback to 1080p still fits in the time a client
  waits.

## 0.1.9 (2026-11-02)

### Added

- **Paired devices in the app:** the tile on the home page opens a list. Each device shows the mode it last asked for, like
  `1280x720@60`, or that it is streaming now, and it can be renamed or removed, for example a friend's device that shouldn't
  stream any more. Removing asks first and also ends that device's running stream. The list stays in the app, nothing of it goes
  into the diagnostic report. The control socket has the new commands `DEVICES`, `RENAME <uuid> <name>` and `UNPAIR <uuid>`.

### Changed

- Pairing asks for a name after the PIN, already filled in with "Moonlight 1", "Moonlight 2" and so on. Moonlight sends the same
  name from every device, `roth`, so every paired device used to show up as that. A client that sends its own name keeps it as the
  suggestion. Devices already paired keep their names.

## 0.1.8 (2026-11-02)

### Added

- **Quit closes the game**, a switch in the app's Settings (`close_game_on_quit = 1` in `sunshine.conf`), off by default: quitting
  in Moonlight (`/cancel`) closes the game the stream started or found in front, through the system's own close as from the home
  screen, and unsaved progress is lost. After the stream the game stays listed as running while it is in front, as Sunshine on a
  PC keeps its app, so Moonlight offers Resume and Quit and asks to quit it before starting another game. Only `/cancel` closes it, never a stream's end or the host's shutdown. Starting the same
  game again right after waits until ShadowMountPlus has released it. Measured on 10.20 with Moonlight for PC, Android and
  iOS: resume, quit and switching games work, and a close took about 1 s. On 13.00 the same with a test client, not yet with
  Moonlight: a close took 0.9 s in GTA III and 2.3 to 3.1 s in the Mario 64 demo.

### Changed

- Firmware 10.x: the picture is no longer blocky. The encoder there takes no bitrate and was left at its own QP, which sent
  0.3 to 0.4 Mbit/s in a moving game whatever Moonlight asked for. Its QP now follows the client's bitrate: 30/34 (IDR/other
  frames) below 8 Mbit/s, 27/31 from 8, 24/28 from 12 and 21/25 from 20. Measured on 10.20 in GTA III, driving fast, HEVC
  10 bit at 1080p60: about 2, 2.4, 3 and 5.3 Mbit/s on average, with a busiest second of 3.1, 3.7, 5.1 and 10 Mbit/s. The
  encoder still doesn't hold the bitrate set in Moonlight, and busier games will send more.
- The diagnostics report no longer names what you play: game titles and title IDs show as `<title>`. It also no longer holds the
  console's local user ID, which the host's log line for the virtual pads gave away.
- The app's Settings page has a new layout, to fit the new switch: four tiles above three.

### Fixed

- An empty `/mnt/sandbox/<title>_000`, left behind when ShadowMountPlus missed a title's exit, no longer counts as an open
  title. It made the check for a game open in the background refuse every other start with 409.

### Known limits

- Without `close_game_on_quit`, quitting a game from Moonlight for PC's app list, also by picking another game and confirming,
  sends nothing to the host: it reports no running game once the stream has ended, so Moonlight skips its `/cancel` and waits at
  "Quitting…". Other clients are untested.
- The new QP on firmware 10.x is measured only on 10.20 and with HEVC. On 10.40 and with H.264 it is untested.
- The 13.00 tests of the switch and of closing a game used a test client. Moonlight's own Resume and Quit menus were only tried on 10.20.

## 0.1.7 (2026-11-01)

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

### Known limits

- The Experimental switches have only run on firmware 13.00, where they change nothing. On other firmware they are untested.
- Firmware 10.40 is unconfirmed. One report from 0.1.6 failed waiting for the capture resource, and the 10.x change has not been
  confirmed to run there.
- Rest mode with a game open and a stream running can still leave the host unreachable until it is restarted. End the stream first.

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
