# ProsperoSunshine

![ProsperoSunshine](docs/images/banner.jpg)

A native, Sunshine-compatible game-stream host for the PlayStation 5. Add your console to Moonlight and stream
the PS5 screen, game audio and controller input over your LAN.

> **Status: 0.1.2 ([changelog](CHANGELOG.md)).** Pairing, 1080p H.264 or HEVC video at 30 or 60 fps, game audio and controller input work end to end
> with Moonlight on PC, in the browser (Moonlight Web) and on iPhone. Sessions of 30 minutes and ten reconnects
> were tested without errors. Only firmware 13.00 has been tested so far. Not every client or game has been tried.

## How it works

ProsperoSunshine builds [Sunshine](https://github.com/LizardByte/Sunshine) (pinned commit, small documented
patches) for the console and replaces the parts that talk to PC hardware:

- **Video:** the console's hardware encoder (Avcap2) delivers H.264 or HEVC straight into Sunshine's packetiser.
- **Audio:** game audio as PCM from the same capture service, encoded by Sunshine's Opus path.
- **Input:** a native virtual DualSense per Moonlight gamepad.
- **App list:** a single app, "PS5 Screen". There is no web UI.

Details: `docs/adr/001-sunshine-port.md`. Roadmap: `ROADMAP.md`; verified state: `STATUS.md`;
reverse-engineering notes: `docs/` (German).

## Requirements

- PS5 firmware: **13.00 is the only tested version**. Other versions are untested; the host refuses them only if its
  system-library lookups fail (`docs/firmware-porting.md`, German).
- A homebrew environment with an **ELF loader** (for example one listening on port 9021). The app also needs
  [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus), which registers titles from `/data/homebrew/`.
- SDR output (HDR is not supported).
- A wired or good wireless LAN, and a Moonlight client in the same network.

## Install

**Option A: the PS5 app "ProsperoSunshine" (`PPSA47989`).** The app carries the host and offers start/stop,
autoload through the Payload Manager, settings and PIN entry. Install it from ProsperoStore, or copy the folder from
`PPSA47989.zip` (GitHub release) to `/data/homebrew/`. Details: `app/README.md`.

| Home | Settings |
| --- | --- |
| ![Host status, start and stop, pairing](docs/images/app-home.jpg) | ![Host settings](docs/images/app-settings.jpg) |

**Option B: send the ELF manually.** Send `prospero_sunshine.elf` (from a release, or `out/` after building) to your
ELF loader. The host keeps its state (identity, paired clients, `sunshine.conf`) in `/data/prosperosunshine/`.
To replace a running host, stop it first (app: "Stop host"); a second ELF cannot bind the ports and exits, and the
old host keeps running.

## Pairing

1. In Moonlight, pick the console, or add it by its IP address (`<console-ip>`), and start pairing. Moonlight shows a PIN.
2. Enter the PIN in the ProsperoSunshine app on the PS5 (option A).
3. Without the app, approve it through the control socket from the same machine, or from the LAN if you enabled it
   (see below): `printf 'PIN 1234\n' | nc <console-ip> 47995`.

Pairing survives host restarts.

## Streaming

Start the app **"PS5 Screen"** in Moonlight. The first time, the console asks **"Who's using this controller?"**
inside the stream; answer it with your Moonlight controller.

**PS button:** Moonlight's on-screen controller on a phone has none, so holding Select for half a second sends it. A
short press of Select sends Create when you let go, which means Create cannot be held. To get a plain Create button
back, set `back_button_timeout = -1` in `sunshine.conf`; a value of 0 or more is the hold time in milliseconds.

## Known limits

- SDR only. The stream is always 1080p: the console encoder starts no other resolution, so a client asking for less
  gets 1080p and scales it. The frame rate follows the client: 30 or 60 fps.
- HEVC (8-bit) is offered on firmware 13.00 only, elsewhere H.264. Clients that prefer HEVC pick it on their own. To
  force H.264, choose it in Moonlight's settings or set `hevc_mode = 1` in `sunshine.conf`.
- The bitrate is capped at 50 Mbit/s.
- One streaming session at a time. More than one Moonlight controller is untested.
- A real DualSense disconnects when the stream controller connects. Turned back on during the stream it stays without
  effect, because apps read only the controller that connected first; after the stream it works normally. The other way
  round, a controller that is already connected and not disconnected (seen with a second virtual pad) keeps the app, and
  only the console's home screen still reacts to Moonlight. If the stream controller does nothing in an app, turn the
  other controller off.
- No HDR, AV1, 4K, mouse, keyboard, touch or microphone.
- Rumble is the classic two-motor kind only. Games that drive the DualSense's haptics send nothing to your controller.
  Whether your device vibrates also depends on the Moonlight client.
- Avoid rest mode for now. In the only test, the host stopped accepting connections after waking, the console then hung and had
  to be switched off at the power button. Turn the console off instead.
- Adaptive triggers work with Moonlight on a PC and a DualSense; other clients ignore them. Only Teardown has been tested. If a stream
  ends during an effect, the trigger stays stiff until you connect again, because Moonlight doesn't reset it.
- No Wake-on-LAN (the console MAC address is not available to the host).
- Starting or quitting a game briefly takes the capture resource away: video and audio pause for about 2 seconds
  and then resume on their own.
- After a hard network drop, held controller input is released after about 2 seconds.

## Troubleshooting

- **Moonlight cannot find the console:** Moonlight normally lists it on its own (mDNS, confirmed on iPhone). If not, for
  example because the network blocks multicast, add it by IP.
- **Connection fails or stalls:** make sure nothing blocks TCP 47984, 47989, 48010 and UDP 47998-48000 between the
  client and the console (client or router firewall).
- **iPhone:** works. An earlier bug (sound but no picture, because the host announced HEVC) is fixed.
- **Black or paused picture for a moment:** expected when a game starts or exits (see limits).
- **Controller works on the home screen but not in an app:** turn off other controllers connected to the console (see
  limits).
- **Other firmware:** only 13.00 is tested. On other versions capture is refused if the system-library lookups fail;
  if they succeed it runs, untested. The kernel log says which case applies.
- **Diagnosis:** the app's **Diagnostics** page collects a report for a bug report, without addresses, device names or
  ids. While the page is open and the host runs, a phone or PC in the same network can open it at
  `http://<console-ip>:47996`; it is also saved as `/data/prosperosunshine/diagnostics.txt`. For more detail, set `min_log_level = debug` in
  `/data/prosperosunshine/sunshine.conf`; messages go to the kernel log and `sunshine.log`.

## Development control socket

The host listens on TCP **47995** with a line-based control socket: `STATUS`, `PAIRINGS`, `PIN [<id>] <pin> [name]`,
`REPORT`, `AUTOLOAD [ON|OFF]` and `QUIT`. It is **unauthenticated**. By default it listens on `127.0.0.1` only (ROADMAP P9.1), so the app on the
console can use it but the LAN cannot. Creating the empty file `/data/prosperosunshine/control_lan` re-enables LAN
access for development. Do this only on trusted networks.

## Building from source

Requires Docker. The image contains LLVM 18 and the
[PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) v0.45 (hash-checked); all third-party sources are pinned
with SHA256 in `deps.lock`.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
```

The first run builds the dependencies into `out/deps` (around 30 minutes, mostly Boost). The host is
`out/prospero_sunshine.elf`; the other ELFs are hardware probes. `app/build.ps1` then builds the app, and
`bash scripts/package.sh` the release archive. Portable unit tests:

```sh
docker run --rm --mount type=bind,source="$PWD",target=/repo prosperosunshine-sdk:v0.45 bash scripts/test.sh
```

An automated Moonlight-protocol test client (pairing, smoke test, reconnects, packet loss, A/V sync) is in
`tools/moonlight/`.

## License

GPL-3.0-only, like Sunshine, which this project builds on (`LICENSE`). Third-party components, their licences and
the patches applied to them are listed in `docs/licenses.md`; what a release must bundle is in `docs/THIRD_PARTY.md`.
The build installs third-party licence texts to `out/deps/licenses`.
