# CLAUDE.md

Notes for coding agents working in this repository. User-facing docs: `README.md`; plan: `ROADMAP.md`;
verified state with evidence: `STATUS.md` (both German).

## Layout

- `src/sunshine/` – PS5 replacements for Sunshine parts (video, audio, input, process, main).
- `src/platform/ps5/` – Avcap2 capture (`capture.c`), `SharedCapture`, backend glue.
- `src/capture/`, `src/video/` – portable logic with unit tests in `tests/`.
- `patches/<dep>/` – patches applied on top of pinned sources; `deps.lock` pins every source by SHA256.
- `app/` – the PS5 settings app `PPSA47989` (ps5-homebrew-ui, ShadowMountPlus title folder).
- `tools/moonlight/` – automated Moonlight test client (pairing, smoke, reconnect, loss, A/V sync).
- `docs/` – reverse-engineering notes (German), ADRs.
- `assets/app/` – sources for the app icon (`icon0.png` 512²) and home-screen background (`pic0.png` 1920×1080).

## Build and test

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1   # host + probes -> out/
```

```sh
docker run --rm --mount type=bind,source="$PWD",target=/repo prosperosunshine-sdk:v0.45 bash scripts/test.sh
```

Release archive from an existing build: `bash scripts/package.sh [version]` (on the host) -> `dist/`.

Toolchain: PS5 Payload SDK v0.45 in the Docker image `prosperosunshine-sdk:v0.45` (clang 18, target
`x86_64-sie-ps5`). Dependencies build once into `out/deps` via `scripts/deps.sh`.

## Rules

- **The repo will be public.** No console IP, local paths, e-mail addresses or names in tracked files.
  Test tools take the console address from `PS5_HOST`. Grep the diff before every commit.
- Firmware 13.00 only is verified. Capture refuses other versions unless the ResourceArbitrator calls
  resolve by NID, and then logs them as unverified; see `docs/firmware-porting.md` before porting.
- New or changed third-party sources: pin them in `deps.lock` (SHA256), patch via `patches/<dep>/`.
- `STATUS.md` records only what was verified live, with how it was measured. Plans go in `ROADMAP.md`.
- Commit only when asked; never push without being asked.
- Do not test rest mode on the console without asking first (risk to the homebrew environment).

## Working on the console

- The host is an ELF payload (ELF loader on port 9021); state lives in `/data/prosperosunshine/`.
  Control socket on TCP 47995, 127.0.0.1 only (the empty file `control_lan` there opens it to the LAN):
  `STATUS`, `PAIRINGS`, `PIN [<id>] <pin> [name]`, `REPORT`, `AUTOLOAD [ON|OFF]`, `QUIT` (one client at a time). `REPORT` makes the
  host answer on TCP 47996 for some seconds with `diagnostics.txt` (a title may not listen on the network).
- Autoload through the Payload Manager: `/data/pldmgr/autoload.txt`, slot `local/prospero-sunshine`.
- The app is installed at `/data/homebrew/PPSA47989`. A ShadowMountPlus scan does **not** pick up changes to
  `sce_sys` (param.json, icon, background) of an already registered title. Re-register it:
  `POST http://<console>:10101/api/v1/games/uninstall {"title_id":"PPSA47989"}` (keeps the source folder),
  then `POST /api/v1/scan {}`.
- The app sits in the Media tab like Prospero Themes: `applicationCategoryType` 65536, while keeping
  `contentBadgeType` 1 and the `launchActivity` intent. A Media-kind title must not carry the `amm` block
  (launch fails with 0x80020016 / CE-100022-5).
- The ProsperoStore catalog is cached on the console under `/data/prosperostore/cache/`; check a title ID
  against it before a release.
