# ProsperoSunshine

A native, Sunshine-compatible game-stream host for the PlayStation 5, so that Moonlight clients can
stream the PS5 screen.

> **Status: early development.** The host streams end to end to a client built on
> moonlight-common-c (pairing, 1080p60 H.264 video, game audio, controller input), but it has not
> yet been tried with the Moonlight apps themselves. Firmware 13.00 only.

## How it works

ProsperoSunshine builds [Sunshine](https://github.com/LizardByte/Sunshine) (pinned commit, small
documented patches) for the console and replaces the parts that talk to PC hardware:

- **Video:** the console's own hardware encoder via Avcap2 delivers H.264; frames go straight into
  Sunshine's packetiser, with keyframes on request and runtime bitrate control.
- **Audio:** game audio as PCM from the same capture service, encoded by Sunshine's Opus path.
- **Input:** a native virtual DualSense per Moonlight gamepad.
- **App list:** a single app, "PS5 Screen". There is no web UI; pairing PINs go through a small
  control socket for now.

Details: `docs/adr/001-sunshine-port.md`. Roadmap: `ROADMAP.md`; verified state: `STATUS.md`;
reverse-engineering notes: `docs/` (German).

## Building

Requires Docker. The image contains LLVM 18 and the
[PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) v0.45 (hash-checked); all third-party
sources are pinned with SHA256 in `deps.lock`.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
```

The first run builds the dependencies into `out/deps` (around 30 minutes, mostly Boost). The host
is `out/prospero_sunshine.elf`; the other ELFs are hardware probes. Portable unit tests:

```sh
docker run --rm --mount type=bind,source="$PWD",target=/repo prosperosunshine-sdk:v0.45 bash scripts/test.sh
```

## Requirements

- PS5 on firmware 13.00 with a homebrew environment that runs ELF payloads (an ELF loader such as
  the one on port 9021); other firmware versions are refused on purpose.
- SDR output (HDR is refused for now) and a wired or good wireless LAN.
- A Moonlight client in the same network.

## Running

1. Send `out/prospero_sunshine.elf` to the ELF loader. The host keeps its state (identity, paired
   clients, `sunshine.conf`) in `/data/prosperosunshine/`.
2. In Moonlight, add the console by its IP address and start pairing; Moonlight shows a PIN.
3. Approve it through the control socket, e.g. `printf 'PIN 1234\n' | nc <console-ip> 47995`.
4. Start "PS5 Screen". The first time, the console asks "Who's using this controller?" in the
   stream; answer it with your Moonlight controller.
5. Stop the host with `printf 'QUIT\n' | nc <console-ip> 47995`.

An automated client for pairing and stream checks is in `tools/moonlight/`.

## Known limitations

- H.264 1080p60 SDR only; bitrate is capped at 15 Mbit/s (the console encoder stops above that).
- No HDR, HEVC, 4K, mouse, keyboard, touch, rumble or microphone yet; one client at a time.
- The control socket is unauthenticated and meant for trusted networks during development.
- After a hard network drop, held controller input stays active for about five seconds until the
  host detects the disconnect.
- Not yet tested with the Moonlight apps themselves or with games as the picture source.

## License

GPL-3.0-only, like Sunshine, which this project builds on. Third-party licence texts are installed
to `out/deps/licenses` by the build.
