# Arbeitsstand

Stand: 30. Oktober 2026. Plan und Reihenfolge: `ROADMAP.md`. Ziel und Regeln: `PLAN.md`. Architektur: `docs/adr/001-sunshine-port.md`.

## Kurzfassung

**Der Host streamt Ende-zu-Ende.** Ein Client auf Basis von moonlight-common-c (derselben Protokollbibliothek, die Moonlight verwendet) paart sich mit der PS5, startet die App „PS5 Screen“ und empfängt den PS5-Bildschirm als H.264 1080p mit 59,94 fps, den Spielton als Opus und schickt Controller-Eingaben an ein virtuelles DualSense. Pairing übersteht einen Host-Neustart.

Eine 30-Minuten-Sitzung und zehn Wiederverbindungen liefen fehlerfrei.

Echte Moonlight-Clients (Moonlight Web, Moonlight PC, Moonlight auf dem iPhone im WLAN) sind gepairt und streamen; Spiele (GTA III, Mario 64) erscheinen im Stream, und Spielstart/-ende überbrückt der Host automatisch.

**PS5-App (Phase 8):** Die App „ProsperoSunshine“ (`PPSA47989`) liegt auf dem Branch `claude/ps5-settings-app-b5c39b` (Commit `8dc11eb`, noch nicht in `main` gemergt) unter `app/`. Live per Controller auf der Konsole geprüft: Host-Status, PIN-Eingabe für eine echte Pairing-Anfrage (Client meldet `PairStatus=1`), Autoload über den Payload Manager an und wieder aus (Eintrag `local/prospero-sunshine` in `/data/pldmgr/autoload.txt`, andere Einträge unverändert), „Restart host“ (Host fährt sauber herunter und startet neu). Einstellungen speichern und auf Standard zurücksetzen live geprüft (`wan_encryption_mode = 0` geschrieben, danach Datei wieder leer). Wunsch für die nächste Runde: Optik auf das Niveau von ProsperoStore/ProsperoEden/Playden heben. Autoload steht aktuell auf **aus**.

**Bild-/Ton-Synchronisation (P5.2, erste Messung):** Navigationsklick im Home-Menü als gemeinsames Ereignis, 60 s, gemessen am Client (`tools/moonlight/av_sync.py`). Tastendruck → Bildänderung 23–25 ms, Tastendruck → Klickton 28–49 ms; Ton liegt 5–23 ms hinter dem Bild (burst-korrigiert Median ≈ 0 ms), also innerhalb der üblichen ±45-ms-Toleranz. Nur die Rechts-Drücke sind auswertbar (bei Links-Drücken erkennt der Bild-Detektor die Änderung nicht zuverlässig); der Drift über 60 s (−4,3 ms/min) ist zu kurz gemessen, ein 10-Minuten-Lauf steht aus.

**Noch nicht geprüft:** Ruhemodus, Moonlight auf Android/TV, andere Firmwares als 13.00.

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
| iPhone | Moonlight auf dem iPhone (WLAN, PS5 per Kabel) gepairt, Bild und Ton laufen. Vorher kam nur Ton: Der Host meldete im RTSP-DESCRIBE HEVC (`active_hevc_mode = 0` heißt in Sunshine „unbekannt“, nicht „aus“), das iPhone wählte HEVC (`bitStreamFormat:1`) und fand in unserem H.264 nie einen Keyframe (`ML_ERROR_NO_VIDEO_FRAME`). Jetzt `1` = aus für HEVC und AV1 | `src/sunshine/video_ps5.cpp`, Debug-Log der Sitzung |
| Steuer-Socket | `STATUS` meldet während eines Streams `address=<ip> client=<name>` (live: `address=… client=handy`), sonst leer; Sunshine-Patch 0005 merkt sich den Client des letzten Launch/Resume | `src/control/control_server.cpp`, `patches/sunshine/0005-last-launch-client.patch` |
| P4.3 | Client mit gehaltenem Steuerkreuz + R2 hart beendet: Host erkennt den Abbruch nach ≈ 5,2 s (ENet-Timeout) und setzt das Pad 0,4 s später neutral (`Resetting Input`). Keine dauerhaft hängenden Eingaben, aber ≈ 5,6 s Nachlauf | `tools/moonlight/hold_kill_test.sh`, `tools/klog_tail.py` |
| P6.2 (Teil) | 10 % verworfene Videopakete beim Client: Stream läuft durch, 18 von 1792 Frames fehlen (4 Lücken), 6 IDR-Anforderungen jeweils nach 50–70 ms bedient, längster Aussetzer 100 ms | `tools/moonlight/loss_test.sh` |
| P6 (Teil) | 30-Minuten-Sitzung bei 20 Mbit/s angefordert: 107 885 Frames, 59,94 fps, 0 Frame-Lücken, 0 FEC-Fehler, max. Abstand 22 ms, komplett decodierbar; 359 787 Audiopakete ohne Fehler. 10/10 Wiederverbindungen. Host-Speicher 18,8–26,0 MB je nach Sitzungszustand, ohne Anstieg über die Sitzungen | `out/moonlight/soak30.log`, `tools/moonlight/reconnect_test.sh` |

### Ende-zu-Ende-Messung (Smoke-Test, 20 s, 20 Mbit/s angefordert)

1196 Frames, 59,94 fps, 0 fehlende Frame-Nummern, 0 FEC-Fehler, max. Frame-Abstand 19 ms, SPS/PPS vor jedem IDR, Datei mit `ffmpeg -xerror` fehlerfrei decodierbar; Audio ≈ 190 Opus-Pakete/s ohne Decodierfehler; Host setzte 14 988 kbit/s.

## Offen und bekannte Grenzen

### Stand 30. Oktober 2026 (alles lokal in `main`, nichts gepusht)

Live auf der Konsole geprüft mit dem aktuellen Host (Testclient vom PC):

- **mDNS im echten Host:** `tools/mdns_query.py` sieht `PS5._nvstream._tcp.local` mit PTR, SRV (Port 47989), TXT und A. Ob Moonlight die PS5 von selbst listet, ist noch nicht gesehen.
- **NID-Auflösung im Host:** Bildaufnahme öffnet sich auf 13.00, Stream 15 s: 895 Frames, 0 Lücken, 0 Audiofehler, fehlerfrei decodierbar.
- **Client-Abbruch (behebt P9.8):** Client hart beendet ohne `/cancel`: Ping-Timeout, 0,2 s später `Session app stopped` und `virtual pad 0 detached (0)`. Danach `currentgame = 0`, ein neuer Client startet direkt, und die App bekommt sofort wieder Eingaben (RemoteEye). Vorher blieb das Pad angemeldet und nahm der App die Eingaben (Sunshine-Patch 0006).
- **Bildrate:** 720p30 angefordert: 298 Frames in 10 s, geliefert als 1080p; 1080p30: 297 Frames; 1080p60: 598 Frames. Jeweils 0 Lücken, 0 Audiofehler, fehlerfrei decodierbar.
- **Verbindungsaufbau:** Der auf 2 s gesenkte `ping_timeout` galt in Sunshine auch für die Zeit von `/launch` bis RTSP; Clients, die länger brauchten, scheiterten mit „RTSP OPTIONS -1“ (reproduziert mit dem Docker-Testclient, ≈ 3 s). Jetzt 10 s für den Aufbau, 2 s bleiben für die Abbruch-Erkennung (Patch 0007). Kommt kein Client, endet auch „PS5 Screen“.
- **App mit aktuellem Host:** App neu gebaut und installiert (`01.000.001`), sie kopiert ihren Host beim Start; „Restart host“ startet ihn neu („The host restarted with the saved settings“).
- **Release-Archiv:** `scripts/package.sh` lokal aus dem aktuellen Build ausgeführt: 23 Dateien, `sha256sum -c` ohne Fehler, keine lokalen Pfade oder Konsolenadresse im ELF (per `grep -a`).

Noch offen:

1. **Auflösung (P9.3):** Avcap2 liefert in der jetzigen Konfiguration nur 1080p (`src/mode_spike.cpp`): 30 und 60 fps gehen (90 bzw. 180 Frames in 3 s, fehlerfrei decodierbar), mit 360p, 540p, 720p, 1440p und 4K scheitert das Öffnen, und `sceAvcap2ChangeScanInResolution` lehnt ab (`0x81950001`, `0x8195000A`). Der Host beachtet daher nur die Bildrate (bis 30 fps gewünscht: 30 fps); Moonlight skaliert 1080p selbst herunter.
2. **Bild und Ton beim Tastendruck (P9.4):** Die Drift ist sauber gemessen (−0,01 ms/min über 10 Minuten; +0,11 ms/min im 60-s-Lauf auf dem Home-Bildschirm). Der Lauf ohne Klickton (0/380, 265 ms) war ein Messfehler: Die Startsequenz von `stream_test` drückt A und startete die fokussierte App, die keinen Klickton hat und deren animierter Hintergrund den Bild-Detektor auslöst. Ein Kontrolllauf reproduzierte das (0/30, 342 ms); `av_sync.py` nutzt jetzt immer `--no-script`. Auf dem Home-Bildschirm (60 s, 30 Drücke, Klickton 18/30): Taste → Bild meist 88–102 ms, Taste → Ton 85–118 ms, Abstand im Median 7 ms. Ab Druck 19 reagierten nur die Rechts-Drücke im Bild nach 29–41 ms, der Ton blieb bei ≈ 100 ms (Abstand dort bis 88 ms). Ungeklärt bleibt dieser Sprung und der Unterschied zu den 23–25 ms (Bild) und 28–49 ms (Ton) der ersten Messung. Es ist nicht entschieden, ob die Verzögerung im Host, im Eingabeweg oder in der Oberfläche der Konsole entsteht.
3. **Payload-Manager-Fenster:** Ein offenes Fenster des Payload Managers liegt über Apps und nimmt Eingaben an sich, auch wenn es nicht reagiert. In dem Zustand lässt es sich nur am echten Controller schließen. Beim Fernsteuern vorher prüfen.
4. **Eigene Werkzeuge vom PC:** `PIN`/`QUIT` und `pair.py status` über den Steuer-Socket gehen vom PC aus nur noch mit der leeren Datei `/data/prosperosunshine/control_lan` (danach Host neu starten). Sonst geht der Host-Neustart über die App.
5. **Release:** CI lief noch nie auf GitHub, Tag und Upload (P7.4) und Store-Release (P8.8/P8.9) offen.
6. **Weiter offen wie bisher:** Ruhemodus (nur mit Rückfrage testen), Android/TV-Clients, andere Firmwares (Checkliste in `docs/firmware-porting.md`), MAC-Adresse 0 (kein Wake-on-LAN).

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
