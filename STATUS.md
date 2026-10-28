# Arbeitsstand

Stand: 28. Oktober 2026, abends. Plan und Reihenfolge: `ROADMAP.md`. Ziel und Regeln: `PLAN.md`. Architektur: `docs/adr/001-sunshine-port.md`.

## Kurzfassung

**Der Host streamt Ende-zu-Ende.** Ein Client auf Basis von moonlight-common-c (derselben Protokollbibliothek, die Moonlight verwendet) paart sich mit der PS5, startet die App „PS5 Screen“ und empfängt den PS5-Bildschirm als H.264 1080p mit 59,94 fps, den Spielton als Opus und schickt Controller-Eingaben an ein virtuelles DualSense. Pairing übersteht einen Host-Neustart.

Eine 30-Minuten-Sitzung und zehn Wiederverbindungen liefen fehlerfrei.

Echte Moonlight-Clients (Moonlight Web, Moonlight PC) sind gepairt und streamen; Spiele (GTA III, Mario 64) erscheinen im Stream, und Spielstart/-ende überbrückt der Host automatisch.

**PS5-App (Phase 8):** Die App „ProsperoSunshine“ (`PPSA47989`) liegt auf dem Branch `claude/ps5-settings-app-b5c39b` (Commit `8dc11eb`, noch nicht in `main` gemergt) unter `app/`. Live per Controller auf der Konsole geprüft: Host-Status, PIN-Eingabe für eine echte Pairing-Anfrage (Client meldet `PairStatus=1`), Autoload über den Payload Manager an und wieder aus (Eintrag `local/prospero-sunshine` in `/data/pldmgr/autoload.txt`, andere Einträge unverändert), „Restart host“ (Host fährt sauber herunter und startet neu). Einstellungen speichern ist im Code, aber noch nicht live geprüft. Wunsch für die nächste Runde: Optik auf das Niveau von ProsperoStore/ProsperoEden/Playden heben. Autoload steht aktuell auf **aus**.

**Bild-/Ton-Synchronisation (P5.2, erste Messung):** Navigationsklick im Home-Menü als gemeinsames Ereignis, 60 s, gemessen am Client (`tools/moonlight/av_sync.py`). Tastendruck → Bildänderung 23–25 ms, Tastendruck → Klickton 28–49 ms; Ton liegt 5–23 ms hinter dem Bild (burst-korrigiert Median ≈ 0 ms), also innerhalb der üblichen ±45-ms-Toleranz. Nur die Rechts-Drücke sind auswertbar (bei Links-Drücken erkennt der Bild-Detektor die Änderung nicht zuverlässig); der Drift über 60 s (−4,3 ms/min) ist zu kurz gemessen, ein 10-Minuten-Lauf steht aus.

**Noch nicht geprüft:** Langzeit-Drift Bild/Ton, Ruhemodus, Moonlight auf Handy/TV.

## Nachweise nach Roadmap

| Paket | Ergebnis | Beleg |
| --- | --- | --- |
| P0.1 | Alle Quellen mit SHA256 in `deps.lock`, Patches unter `patches/<name>/`, modulare `scripts/deps.sh` + `scripts/deps/*.sh` mit Stempeln | — |
| P0.2/0.3 | Boost 1.92.0 (Sunshines Pin), Simple-Web-Server, nlohmann_json gebaut; Live: Asio-TLS 1.3, Boost.Log, property_tree | `src/boost_probe.cpp` |
| P0.4 | FFmpeg 9.0.2 minimal (avcodec/avutil/swscale, CBS, kein Codec); Sunshines `cbs.cpp` läuft unverändert auf der Konsole | `src/cbs_probe.cpp` |
| P0.5 | Ein CMake-Projekt baut Host, Probes und Bibliotheken | `CMakeLists.txt`, `scripts/build.sh` |
| P0.6 | `out/prospero_sunshine.elf` (12,4 MB) startet, beantwortet `/serverinfo`, fährt per `QUIT` sauber herunter | — |
| P0.7 | ADR-001 festgeschrieben | `docs/adr/001-sunshine-port.md` |
| R1 | `sceAvcap2RequestKeyFrame`: IDR nach 2–3 Frames (8/8); im Stream nach ~50 ms | `src/avcap_spike.cpp` |
| R2 | Spielton über `sceAvcap2OpenAudio` (cfg+0x1C = RA-Id 180): PCM float32 Stereo 48 kHz, 1024 Frames/Paket; auch Opus 10/20 ms möglich | `src/audio_spike.cpp`, `docs/re/avcap2-exports.md` |
| R3 | `sceAvcap2SetVideoBitrate` wirkt live; bis 15 Mbit/s (Spitze ×1,12) stabil, ab 16 Mbit/s stoppt der Encoder → Host klemmt auf 15 Mbit/s | `src/bitrate_spike.cpp` |
| R4 | Host läuft als Payload-Daemon, Daten unter `/data/prosperosunshine/`, Neustart per Steuer-Socket + ELF-Loader | — |
| R5 | Natives virtuelles Pad ohne Mono; neues Pad löst „Wer benutzt diesen Controller?“ aus (im Stream sichtbar und beantwortbar) | `src/pad_probe.cpp` |
| R6 | Ports 47984/47989/48010 TCP und 47998–48000 UDP frei; Discovery (mDNS) nicht untersucht | — |
| P1.1–P1.4 | `CaptureSession` mit Tests; 10 min Home-Menü: 35 966 Frames, 59,94 fps, 600 IDR, fehlerfrei decodierbar; 5 saubere Zyklen; RSS konstant 12,8 MB; Descriptor-Felder entschlüsselt (u. a. TSC-Zeitstempel) | `src/capture_soak.cpp`, `docs/avcap2-descriptor.md` |
| P2.1–P2.4 | Identität bleibt über Neustart; Pairing per PIN über Steuer-Socket; ungepairter Client wird abgewiesen (401); Launch/Resume/Cancel; zweite Sitzung abgelehnt | `tools/moonlight/` |
| P3.1–P3.3 | Video über unveränderten `stream.cpp`-Pfad inkl. FEC; 0 Frame-Lücken; IDR-Anforderung des Clients bedient | `tools/moonlight/run_smoke.sh` |
| P3.4 | Nicht nötig: der PS5-Encoder schreibt bereits VUI mit `max_num_reorder_frames=0`, `max_dec_frame_buffering=1` | `cbs_probe` |
| P4.1/4.2 | Gamepad-Ereignisse kommen an, Pad wird angelegt; Tastenabbildung als getestete Tabelle | `src/input/gamepad_map.hpp`, `tests/gamepad_map_test.cpp` |
| P5.1 | Echter Spielton im Stream (RMS ≈ −40 dBFS im Home-Menü), Sunshines Opus-Encoder mit 5-ms-Paketen | — |
| P1.4 / P6.2 | Spiele als Bildquelle: GTA III und Mario 64 erscheinen im Stream. Spielstart und -ende entziehen dem Host die Capture-Ressource (`0x81950009`); der Host öffnet Video und Ton automatisch neu (1,6–2,1 s Pause, Frame-Nummern laufen weiter). 120-s-Stream über zwei Spielstarts/-enden: 6989 Frames, 0 Frame-Lücken, fehlerfrei decodierbar, 0 Audiofehler | `out/game_tiles.png` (lokal), `src/sunshine/video_ps5.cpp` |
| Echte Clients | Moonlight Web und Moonlight (PC) gepairt (PIN über Steuer-Socket) und gestreamt; Moonlight Web braucht ein App-Cover (jetzt eingebettet) | — |
| P4.3 | Client mit gehaltenem Steuerkreuz + R2 hart beendet: Host erkennt den Abbruch nach ≈ 5,2 s (ENet-Timeout) und setzt das Pad 0,4 s später neutral (`Resetting Input`). Keine dauerhaft hängenden Eingaben, aber ≈ 5,6 s Nachlauf | `tools/moonlight/hold_kill_test.sh`, `tools/klog_tail.py` |
| P6.2 (Teil) | 10 % verworfene Videopakete beim Client: Stream läuft durch, 18 von 1792 Frames fehlen (4 Lücken), 6 IDR-Anforderungen jeweils nach 50–70 ms bedient, längster Aussetzer 100 ms | `tools/moonlight/loss_test.sh` |
| P6 (Teil) | 30-Minuten-Sitzung bei 20 Mbit/s angefordert: 107 885 Frames, 59,94 fps, 0 Frame-Lücken, 0 FEC-Fehler, max. Abstand 22 ms, komplett decodierbar; 359 787 Audiopakete ohne Fehler. 10/10 Wiederverbindungen. Host-Speicher 18,8–26,0 MB je nach Sitzungszustand, ohne Anstieg über die Sitzungen | `out/moonlight/soak30.log`, `tools/moonlight/reconnect_test.sh` |

### Ende-zu-Ende-Messung (Smoke-Test, 20 s, 20 Mbit/s angefordert)

1196 Frames, 59,94 fps, 0 fehlende Frame-Nummern, 0 FEC-Fehler, max. Frame-Abstand 19 ms, SPS/PPS vor jedem IDR, Datei mit `ffmpeg -xerror` fehlerfrei decodierbar; Audio ≈ 190 Opus-Pakete/s ohne Decodierfehler; Host setzte 14 988 kbit/s.

## Offen und bekannte Grenzen

- **Echte Moonlight-App:** braucht ein Gerät mit Moonlight im LAN; PS5 per IP hinzufügen, PIN über `tools/moonlight/README.md` bzw. Steuer-Socket eingeben.
- **Spiele als Quelle (P1.4):** nur Home-Menü und Systemoberfläche geprüft.
- **MAC-Adresse** in `/serverinfo` ist 0: weder `getifaddrs` noch `sceNetGetMacAddress` liefern sie im Payload. Betrifft nur Wake-on-LAN.
- **Boost.Filesystem `create_directories`** scheitert bei neuen verschachtelten Pfaden, wenn `errno` vorher belegt ist (ein Libc-Aufruf setzt kein `errno`). Der Host nutzt nur `std::filesystem` und ist nicht betroffen.
- **Zweite RTSP-Verbindung** wird ohne erklärende Fehlermeldung geschlossen (Upstream-Verhalten).
- **Eingaben nach hartem Verbindungsabbruch** wirkten mit Sunshines Standard (`ping_timeout` 10 s) ≈ 5,6 s nach. Mit dem neuen Host-Standard von 2 s (Moonlight pingt alle 100 ms) erkennt der Host den Abbruch nach 1,8 s und setzt das Pad dann neutral (gemessen mit `tools/moonlight/hold_kill_test.sh`).
- **Entzug der Capture-Ressource** passiert bei Spielstart/-ende (immer) und sporadisch wenige Sekunden nach Anmeldung des virtuellen Pads (2 von 7 Sitzungsstarts; in vier gezielten Wiederholungen mit vollem Kernel-Log nicht reproduziert). Der Host stellt Video und Ton jeweils nach ≈ 2 s wieder her; der Client sieht eine kurze Pause.
- **Nur eine Sitzung gleichzeitig:** Eine zweite Streaming-Sitzung (z. B. Moonlight Web, das während einer laufenden Sitzung erneut verbindet) wird beendet, weil sonst zwei Leser denselben Avcap2-Kanal leeren.
- **Avcap2-Symbolauflösung war nicht threadsicher:** `kernel_dynlib_dlsym` aus mehreren Threads gleichzeitig ließ Ton und Keyframe-Anforderung scheitern; alle Funktionen werden jetzt einmal beim Öffnen aufgelöst.
- **Log-Stufe:** `min_log_level` in `/data/prosperosunshine/sunshine.conf` gilt auch für das Kernel-Log (z. B. `min_log_level = debug` für Diagnose).
- **Steuer-Socket** ist im LAN offen und nur für Entwicklung gedacht; die Host-API für die PS5-App (P2.5) ist noch nicht abgestimmt.
- **Kein HDR/HEVC, keine Maus/Tastatur, kein Rumble** (v1.0-Umfang).

## Bauen und testen

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
```

Baut im Docker-Image `prosperosunshine-sdk:v0.45` zuerst fehlende Abhängigkeiten (`out/deps`, erster Lauf ~30 min, vor allem Boost), dann alles mit CMake nach `out/`. Portable Tests unter ASan/UBSan:

```powershell
docker run --rm --mount "type=bind,source=$PWD,target=/repo" prosperosunshine-sdk:v0.45 bash scripts/test.sh
```

Host starten: `out/prospero_sunshine.elf` an den ELF-Loader (Port 9021) schicken. Steuer-Socket TCP 47995: `STATUS`, `PAIRINGS`, `PIN <pin>`, `QUIT`. Testclient, Pairing und Smoke-Test: `tools/moonlight/README.md`.

## Wichtige ABI-Erkenntnisse (FW 13.00)

- Avcap2-Videokonfiguration 0x2E0 Bytes; `0x2A4 = 2` zusammengesetzter Bildschirm; RA-Profil 180 (keine Benutzer-ID); Details in `src/platform/ps5/capture.c`.
- Audio-Konfiguration 0x38 Bytes; `cfg+0x1C` ist dieselbe RA-Id 180.
- Descriptor-Wort 9 = Aufnahmezeit in TSC-Ticks (~1,596 GHz); Wort 3 Bildtyp; Wort 4 Framezähler (`docs/avcap2-descriptor.md`).
- `sceAvcap2*` werden per Name über `kernel_dynlib_dlsym` aufgelöst; Export-Tabelle mit 59 NIDs in `docs/re/avcap2-exports.md`.
- `__builtin_cpu_supports` und AVX-Typen in `immintrin.h` funktionieren auf dem `__SCE__`-Ziel nicht ohne Umweg (siehe `scripts/deps/nanors.sh`).
- `std::jthread` braucht `-fexperimental-library` (im CMake gesetzt).

## Verwandtes Repository ps5-mcp

Die direkte Avcap2-Screenshot-Methode für `ps5_screenshot` ist dort umgesetzt und live geprüft (59 Python-Tests, Selbsttest mit elf Prüfungen bestanden). Nicht veröffentlicht; Restpunkte: veralteter Kopfkommentar in `payload/avcap_probe/avcap_probe.c`, ungetesteter Release-Download des neuen ELF.

## Herkunft und Lizenz

Sunshine `a679b059` und alle Bibliotheken mit Pins in `deps.lock`; Lizenztexte nach dem Build in `out/deps/licenses/`. Eigene Änderungen an Sunshine nur als begründete Patches in `patches/sunshine/` (0001 libvirtualhid, 0002 NVENC/VAAPI-Konstanten, 0003/0004 ohne libcurl). Analysen von Sony-Binärdateien enthalten keine Sony-Daten; die entschlüsselten Module liegen nur im ignorierten `.cache/re`.
