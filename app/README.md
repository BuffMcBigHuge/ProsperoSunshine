# ProsperoSunshine app

A PS5 homebrew title (`PPSA47989`, "ProsperoSunshine") that runs the ProsperoSunshine host from the
console itself, with a controller:

- **Host**: shows whether the host runs or streams, and starts, stops or restarts it.
- **Pairing**: lists Moonlight's pending pairing requests and takes the PIN Moonlight shows, on an
  on-screen keyboard. This replaces Sunshine's web UI for pairing.
- **Settings**: edits `/data/prosperosunshine/sunshine.conf`.
- **Start at boot**: puts the host into the Payload Manager's autoload list, or takes it out.

It is drawn with [ps5-homebrew-ui](https://github.com/Phononenom/ps5-homebrew-ui) and built, packaged
and installed the same way as [PS5SD](https://github.com/Darkatek7/PS5SD): a title folder for
ShadowMountPlus.

## Requirements

- The host's requirements (`../README.md`): FW 13.00, an ELF loader on port 9021.
- ShadowMountPlus, to register the title.
- For **Start at boot**: the [Payload Manager](https://github.com/itsPLK/ps5-payload-manager)
  (`PLDM00001`, tested with v0.6.3), with its own autoload switched on.

## Installing

Copy the folder `PPSA47989` (from `build/app/`, or unpacked from `PPSA47989.zip`) to
`/data/homebrew/` on the console, for example with ftpsrv on port 2121, and let ShadowMountPlus scan.
"ProsperoSunshine" then shows on the home screen.

The title carries the host (`payloads/prospero_sunshine.elf`). At every start the app copies it to
`/data/prosperosunshine/prospero_sunshine.elf` when that file is missing or another build; start,
restart and autoload all use that copy. So a new host reaches the console with a new version of the
app.

## Using it

The screen has two panels. Up and down move within a panel; L1 and R1, or left and right on a row
where they change nothing, switch panels. Cross acts on the row.

| Row | What it does |
| --- | --- |
| Status | Host stopped, running, or streaming (and which app). Polled every two seconds through the host's control socket. |
| Start host / Stop host | Start sends the host ELF to the ELF loader (`127.0.0.1:9021`) and waits until the host answers. Stop sends `QUIT` and waits until it is gone. |
| Restart host | Stop, then start: the way new settings take effect. |
| Start at boot | The switch for the Payload Manager's autoload (below). The line under it warns when the Payload Manager's own autoload is off. |
| Pairing requests | One row per Moonlight client waiting to pair, with its name and address. Cross opens the PIN keyboard; type the four digits Moonlight shows and press Done (or Options). The app waits until Moonlight has finished pairing and says whether it worked. |

**Settings** (right panel): left and right change a value, Square puts it back to the host's default
(the key is then removed from the file), Cross edits the host name or types a number on the keyboard.
A dot marks values that are not saved yet. **Save** writes the file; the host reads it only when it
starts, so use **Save and restart** (or Restart host later) to apply it. Triangle reads the files
again, for example after editing them over FTP.

| Setting | `sunshine.conf` key | Values | Without the key |
| --- | --- | --- | --- |
| Host name | `sunshine_name` | text, up to 63 characters, no `#` or `[` | the console's network name |
| Log level | `min_log_level` | verbose, debug, info, warning, error, fatal, none | info |
| Ping timeout (ms) | `ping_timeout` | 500 to 60000 | 2000 (the host's own default) |
| Encryption on the LAN | `lan_encryption_mode` | 0 off, 1 when the client supports it, 2 required | 0 |
| Encryption over the internet | `wan_encryption_mode` | as above | 1 |
| Base port | `port` | 1025 to 65514 | 47989 |

The names, ranges and defaults are Sunshine's (`src/config.cpp` at a679b059). Every other line of the
file, comments included, is kept as it is. Sunshine uses the first line of a key; when the app sets a
key that appears more than once, it keeps the first line's place and removes the others.

## Start at boot: how the Payload Manager is set up

The Payload Manager keeps its payloads in `/data/pldmgr/payloads/`, one folder ("slot") each with the
ELF and a `meta.json`, and runs `/data/pldmgr/autoload.txt` from top to bottom at boot (after
`AUTOLOAD_DELAY` seconds, only when `AUTOLOAD_ENABLED=1` in `/data/pldmgr/pldmgr_config.txt`). A line
`!<ms>` waits; any other line names a slot, such as `repo/elfldr@e8cce5d1` or `local/<name>`, whose
ELF the manager sends to the ELF loader on `127.0.0.1:9021`. Absolute paths are accepted only under
`/data/pldmgr` and on USB drives (`slot_validate_payload_path` in its `src/slot.c`), so the host gets
a slot of its own.

Turning **Start at boot** on:

1. copies `/data/prosperosunshine/prospero_sunshine.elf` to
   `/data/pldmgr/payloads/local/prospero-sunshine/prospero_sunshine.elf` and writes a `meta.json`
   beside it (schema 2, a local slot);
2. appends the line `local/prospero-sunshine` to `autoload.txt`, so the host starts after everything
   the list loads before it (kstuff, the ELF loader, ...).

Turning it off removes that line and leaves everything else in the list as it was. The slot stays, so
the host can still be started from the Payload Manager's own page. When the app installs a new host,
it updates the slot's copy too. Each file is written as `<name>.tmp` and renamed over the old one.

## The host's control socket

The app talks to the host on `127.0.0.1:47995` (`src/control/control_server.hpp`), one line per
request:

| Request | Answer |
| --- | --- |
| `STATUS` | `OK running=<0\|1> app=<name> pending=<n>` |
| `PAIRINGS` | `OK <id> <name> <address>` per pending request, then `END` |
| `PIN <id> <pin> [name]` | `OK paired` or `ERR ...`, once Moonlight has finished the handshake |
| `QUIT` | `OK bye`, then the host ends |

The socket serves one client at a time and has no authentication; it is meant for the local network
during development. The app keeps each connection short (a poll sends `STATUS` and `PAIRINGS` and
closes), except for `PIN`, which waits up to 60 s for Moonlight.

## Building

Needs Docker. Build the host first (`scripts/build.ps1`), then:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File app/build.ps1
```

The result is `build/app/PPSA47989/` and `build/app/PPSA47989.zip`. `-HostElf <path>` bundles another
host build.

`app/build.ps1` builds the image `prosperosunshine-app:1` (`app/Dockerfile`: Debian with Clang 18) and
runs `app/build.sh` in it, with a Docker volume `prosperosunshine-app-cache` for everything that is
fetched or built once:

1. ps5-homebrew-ui at a pinned commit (`kit_rev` in `app/build.sh`);
2. a staged copy of the kit in which `app/src` replaces the kit's own app (its designs, demo and
   `main.cpp`), and `app/sce_sys` its title data. The kit's music and second sound set are left out;
3. the kit's own `make app`: the PS5 payload SDK v0.42, its clean-room `libc.prx`, compile, link
   against ps5-opengl, sign, and the title folder;
4. the host ELF and the licences added to the folder.

The one thing the build does not fetch is the **ps5-opengl SDK** (EGL and OpenGL 4.6 for the screen):
its release download is gone. PS5SD builds it from source with its `scripts/build-opengl-sdk.sh`
(about 30 to 60 minutes; see its `docs/BUILDING.md`). `app/build.ps1` copies it into the cache volume
from PS5SD's Docker volume `ps5sd-work` when that exists, or from a folder given with
`-OpenGlSdk <folder>`. The SDK's `manifest.sha256` is checked before every build.

PC tests for the `sunshine.conf` and autoload edits:

```powershell
docker run --rm -v "${PWD}:/repo" prosperosunshine-app:1 bash app/tests/run.sh
```

## Source

| File | What it is |
| --- | --- |
| `src/main.cpp` | The frame loop: display, fonts, pad, sound, the screen. Ends itself when `/data/prosperosunshine/app.quit` appears (for unattended tests). |
| `src/psapp/screen.*` | The screen, its rows, the keyboard overlay. |
| `src/psapp/host.*` | The worker thread: polls the control socket, starts and stops the host, saves the settings, switches autoload. The screen reads a snapshot of its state every frame and never waits. |
| `src/psapp/conf.*` | `sunshine.conf`: the offered options, their checks, line-preserving edits. |
| `src/psapp/autoload.*` | The Payload Manager's `autoload.txt` and slot. |
| `src/psapp/io.*` | Files and loopback sockets. |
| `sce_sys/` | `param.json` and the icon. |
| `tests/` | The PC tests. |

The app logs to the kernel log (klogsrv, port 3232) with the prefix `[PSAPP]`.

## Licence

GPL-3.0-only, like the host. ps5-homebrew-ui is GPL-3.0-or-later; its licence and third-party notices
(fonts, sounds) go into the title's `licenses` folder.
