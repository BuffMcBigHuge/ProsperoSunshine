# ProsperoSunshine Roadmap und Umsetzungsplan

Stand: 28. Oktober 2026. Dieses Dokument ist der verbindliche Arbeitsplan. `PLAN.md` enthält Ziel, Regeln für das PS5-Backend und Hintergründe, `STATUS.md` den jeweils nachgewiesenen Stand. Bei Widersprüchen gilt für Fakten `STATUS.md`, für Reihenfolge und Abnahme dieses Dokument.

## Stand (28. Oktober 2026, abends)

Erledigt und live belegt (Details in `STATUS.md`): Phase 0 komplett, Spikes R1–R5 positiv (R6 ohne mDNS), Phase 1 bis auf Spiele als Bildquelle, P2.1–P2.4, P3.1–P3.4, P4.1–P4.2, P5.1, aus P6 die 30-Minuten-Sitzung und zehn Wiederverbindungen, P6.4 (automatischer Testclient in `tools/moonlight/`), P4.3 geprüft (Pad wird nach Abbruch neutral, ≈ 5,6 s Nachlauf), P6.2 Paketverlust (10 % ohne Abbruch), P7.1 (CI-Workflow, noch nicht auf GitHub gelaufen). Ende-zu-Ende-Stream mit einem moonlight-common-c-Client funktioniert.

P7.2 (Lizenzübersicht `docs/licenses.md`) und P7.3 (englische README mit Voraussetzungen, Ablauf und Grenzen) sind erledigt.

Als Nächstes: Test mit einer echten Moonlight-App, Spiele als Bildquelle (P1.4), Bild-/Ton-Synchronisation (P5.2), früherer Eingabe-Watchdog (P4.3-Folgepunkt), weitere Fehlerinjektion wie Ruhemodus und Ressourcenentzug (P6.2), Host-API mit der PS5-App abstimmen (P2.5), Release (P7.4).

## Zielbild v1.0

Ein unveränderter Moonlight-Client im lokalen Netz fügt die PS5 per IP hinzu, paart sich und streamt den PS5-Bildschirm mit H.264 1080p SDR, Stereo-Ton und einem Controller. Kein PC vermittelt im Betrieb. Nach Sitzungsende sind alle eigenen Ressourcen freigegeben.

Nicht in v1.0: HEVC/AV1, HDR, 4K, mehrere Clients oder Controller, Mikrofon, Rumble, adaptive Trigger, Internetzugriff, Maus/Tastatur.

## Architekturentscheidung (Vorschlag ADR-001, Abschluss in Phase 0)

**Sunshine wird als festgehaltener Upstream-Stand gebaut, ergänzt um eine PS5-Plattformschicht.** Protokollcode (Pairing, RTSP, Paketierung, FEC, Verschlüsselung, Steuerkanal, Eingabeauswertung) bleibt möglichst unverändert, damit Kompatibilität und spätere Upstream-Updates erhalten bleiben. Ersetzt wird nur, was an PC-Hardware, Prozessstart oder Desktop hängt.

Die Nahtstellen im Sunshine-Commit `a679b059` sind eng und gut abgegrenzt:

| Nahtstelle | Upstream | PS5-Umsetzung |
| --- | --- | --- |
| Video | `stream.cpp` ruft `video::capture(mail, config, session)` und liest `video::packet_t` aus `mail::video_packets`; IDR-Wünsche kommen über `mail::idr`. | Eigene `video::capture()` liefert Avcap2-Frames als `packet_raw_generic`. Kein Encoder, kein Bildformatwandel. |
| Audio | `audio::capture()` holt PCM über `platf::audio_control()->microphone()` und kodiert mit Opus. | PS5-Implementierung von `audio_control_t`/`mic_t`. Opus-Pfad bleibt upstream. |
| Eingabe | `input.cpp` ruft `platf::alloc_gamepad`, `gamepad_update`, `free_gamepad`. | Natives virtuelles PS5-Pad. Maus/Tastatur als leere Implementierungen. |
| Plattform | `platform/common.h`: `init`, `appdata`, `send`/`send_batch`, Hostname, MAC, Timer, Threadpriorität. | `src/platform/ps5/`. |

Einordnung der Sunshine-Quellen:

| Behandlung | Dateien |
| --- | --- |
| Übernehmen, höchstens kleine Patches | `nvhttp.cpp`, `httpcommon.cpp`, `rtsp.cpp`, `stream.cpp`, `crypto.cpp`, `network.cpp`, `audio.cpp`, `input.cpp`, `config.cpp`, `logging.cpp`, `cbs.cpp`, `file_handler.cpp`, `globals.cpp`, `stat_trackers.cpp`, Header wie `sync.h`, `thread_safe.h`, `thread_pool.h`, `task_pool.h`, `utility.h`, `uuid.h`; aus moonlight-common-c `RtspParser.c` und nanors |
| Ersetzen | `video.cpp` (PS5-Capture statt Encoder-Pipeline), `process.cpp` (feste App „PS5-Bildschirm“, kein Prozessstart), `main.cpp`/`entry_handler.cpp` (Daemon-Start, Konfigurationspfad), `platform/linux/*` → `platform/ps5/*` |
| Weglassen, Aufrufe per Stub | `confighttp.cpp` (Web-UI), `system_tray.cpp`, `display_device.cpp` + libdisplaydevice, `upnp.cpp` + miniupnpc, NVENC, `virtualhid_input`, curl-Teile in `httpcommon.cpp` |

Upstream-Code bleibt unverändert im Cache. Änderungen liegen als einzelne, begründete Dateien unter `patches/sunshine/` und werden beim Build angewendet. Sunshine selbst kommt mit Hash in `deps.lock`.

**Folge für die Abhängigkeiten:** Boost wird mit den von den übernommenen Dateien tatsächlich genutzten Teilen gebaut (Asio, SSL, property_tree, endian, Log, program_options, filesystem; locale und process nur, falls nach dem Weglassen noch referenziert). FFmpeg wird minimal gebaut, nur `libavutil` und `libavcodec` ohne Encoder/Decoder, aber mit H.264-Parser und Coded-Bitstream (`cbs_h264`). So bleiben `video.h` und `cbs.cpp` unverändert, und Sunshines SPS-Anpassung für niedrige Decoder-Latenz funktioniert.

Gegenentwurf, falls Boost oder der Gesamtbau scheitern: Nur `nvhttp`/`rtsp`/`stream`/`crypto`/`input`/`audio` in ein eigenes, schlankes Gerüst übernehmen und Logging, Konfiguration und HTTP-Server selbst stellen. Mehr Eigenarbeit, schwerer zu aktualisieren. Entscheidung fällt mit P0.6.

## Phasenübersicht und kritischer Pfad

```text
Phase 0  Fundament ──► Phase 2  Sitzung & Pairing ──► Phase 3  Video in Moonlight ──► Phase 6  Härtung ──► Release 0.1
   │                        ▲                                ▲                          ▲
   ├─► Phase R  Risiko-Spikes (IDR, Audio, Encoder, Pad) ─────┼──────────┐               │
   └─► Phase 1  Capture-Dauerbetrieb ────────────────────────┘          ├─► Phase 4  Controller
                                                                         └─► Phase 5  Audio
```

Kritischer Pfad: P0.6 (Sunshine-Kern baut für PS5) → P2.3 (Pairing) → P3.2 (erstes Bild). Die Risiko-Spikes laufen früh und parallel, weil ihr Ergebnis den Funktionsumfang von v1.0 verändern kann.

Aufwand: **S** etwa eine Arbeitssitzung, **M** zwei bis drei, **L** mehr. Jede Aufgabe ist erst erledigt, wenn ihr Nachweis in `STATUS.md` steht; auf der PS5 bedeutet das einen Live-Lauf mit Log, nicht nur einen erfolgreichen Build.

## Phase 0: Fundament (offener Rest von M0)

Bereits erledigt: Toolchain, C++23, `std::jthread` (mit `-fexperimental-library`), Sockets, OpenSSL 3.5.9, Opus 1.6.1, ENet, nanors, gepinnte Quellen in `deps.lock`.

| ID | Aufgabe | Aufwand | Ergebnis und Nachweis |
| --- | --- | --- | --- |
| P0.1 | Sunshine-Tarball und moonlight-common-c in `deps.lock` aufnehmen, Patch-Mechanismus `patches/sunshine/` in `deps.sh` | S | Reproduzierbarer, hashgeprüfter Quellbaum unter `.cache` |
| P0.2 | Boost ≥ 1.89 cross-bauen (b2 mit Prospero-Toolchain), zuerst header-only Teile, dann Log, program_options, filesystem; Bedarf an locale/process anhand der Includes nach Weglassen bestimmen | M | Statische Bibliotheken in `out/deps`; Live-Probe: Asio-TCP-Server mit TLS über libssl, Boost.Log nach klog, property_tree XML |
| P0.3 | Simple-Web-Server (gepinnter Commit) und nlohmann_json bereitstellen | S | Header in `out/deps`; HTTPS-Endpunkt im Probe |
| P0.4 | Minimales FFmpeg: `--disable-everything --enable-avcodec --enable-avutil --enable-parser=h264 --enable-bsf=h264_metadata` (oder was `cbs.cpp` braucht) | M | `libavcodec.a`, `libavutil.a`; Probe parst SPS aus `out/capture.h264` und schreibt VUI um |
| P0.5 | Eigenes CMake-Projekt statt `build.sh`: Toolchain `prospero.cmake`, Ziele für Capture-Bibliothek, Probes, Tests und später den Host | M | `build.ps1` ruft CMake; alle vorhandenen Probes bauen weiter |
| P0.6 | Sunshine-Kern mit PS5-Stubs kompilieren und linken: alle Dateien aus „Übernehmen“, Stubs für Plattform, Video, Audio, Eingabe, Prozess | L | `prospero-sunshine.elf` startet, lädt Konfiguration, beendet sich sauber; Liste der nötigen Patches mit Begründung |
| P0.7 | ADR-001 in `docs/adr/001-sunshine-port.md` festschreiben, inklusive Lizenz- und Herkunftsdokumentation | S | Entscheidung, verworfene Alternative, Lizenzliste |

**Abnahme Phase 0:** Der Host-ELF mit gelinktem Sunshine-Kern startet auf der PS5 und beendet sich auf Signal ohne Leck an eigenen Ressourcen. Alle Bibliotheken sind gepinnt, gehasht und lizenzdokumentiert.

## Phase R: Risiko-Spikes (parallel zu Phase 0 und 1)

Jeder Spike endet mit einer schriftlichen Antwort in `docs/` („geht, so“ oder „geht nicht, Ersatz ist …“), nicht mit Produktcode.

| ID | Frage | Vorgehen | Ersatzlösung, falls negativ |
| --- | --- | --- | --- |
| R1 | Lässt sich bei Avcap2 ein IDR-Frame sofort anfordern? Moonlight fordert nach Paketverlust einen an. | Exporte von `libSceAvcap2` auflisten (NIDs mit bekannten Namen abgleichen), Konfigurationsfelder um `0x2E0` durchsuchen, Kandidaten live testen | Kurzes festes GOP (gemessen: 11 IDR in 601 Frames, etwa alle 55 Frames ≈ 0,9 s) und bis zum nächsten IDR einfrieren; Abstand per Konfiguration verkürzen, falls möglich |
| R2 | Wie kommt der Spielton an? | Audio-Funktionen in Avcap2 suchen (analog zu `ReadVideo`), sonst Remote-Play-/Share-Pfad (`SceRemotePlay`, `SceAvCapture`, `orbis_audiod`) untersuchen; Format, Rate, Zeitstempel festhalten | v1.0 zunächst ohne Ton als „0.x Preview“ veröffentlichen; Audio wird eigenes Folgeziel |
| R3 | Lassen sich Bitrate, Bildrate und Auflösung einstellen? | Konfigurationsfelder variieren, Ergebnis mit ffprobe messen | Feste Werte als einzigen ausgehandelten Modus anbieten (Moonlight akzeptiert, was der Host meldet) |
| R4 | Laufzeitmodell: läuft der Host stabil stundenlang als Payload-Daemon, und wo liegen persistente Daten? | Langlauf eines Leerlauf-Daemons, Schreibtest unter `/data/prosperosunshine`, Single-Instance-Sperre, Verhalten bei Ruhemodus | Start durch die App der anderen Session statt eigenständigem Daemon |
| R5 | Natives virtuelles Pad ohne Mono | RemoteEye (`../ps5-mcp/payload/RemoteEye`, C#) nach C portieren: Gerät anlegen, Report senden, freigeben; Zuordnungsdialog „Wer benutzt diesen Controller?“ beobachten | Pad an bestehenden Benutzer binden; Dialog einmalig vor Streambeginn bestätigen lassen |
| R6 | Port-/Dienstkonflikte | Prüfen, ob 47984, 47989, 48010 (TCP) und 47998–48000 (UDP) frei sind; ob mDNS (5353) mit `SceSpZeroConf`/nanodns kollidiert | Basisport verschieben (Moonlight unterstützt `IP:Port`); Discovery erst nach v1.0 |

**Abnahme Phase R:** Für R1 bis R6 liegt je eine dokumentierte Antwort vor. Ist R1 oder R2 negativ, ist der angepasste v1.0-Umfang in diesem Dokument nachgetragen.

## Phase 1: Capture-Dauerbetrieb (M1)

| ID | Aufgabe | Aufwand | Ergebnis und Nachweis |
| --- | --- | --- | --- |
| P1.1 | `CaptureSession` (C++) über `capture.h`: Start, blockierendes Lesen mit Timeout, Stop, Ressourcenentzug als Ereignis | M | Klare Zustandsmaschine; Tests mit nachgebildetem Backend |
| P1.2 | Frame-Metadaten: Größe, IDR, SPS/PPS-Wechsel, lokale Monotonzeit; Descriptor-Zeitstempel untersuchen | M | Feldbedeutung in `docs/avcap2-abi.md` |
| P1.3 | Diagnose-Ausgabe: Stream per TCP an den PC (nur Entwicklung) statt Datei | S | `ffplay` am PC zeigt Live-Bild |
| P1.4 | Langlauf und Zyklen: 10 Minuten Home-Menü und Homebrew, 5× Start/Stop, ein reguläres Spiel | M | Frame-Abstände, Speicherverlauf, `ffprobe` fehlerfrei |

**Abnahme Phase 1:** Wie in `PLAN.md` M1: zehn Minuten decodierbares Video, gemessene Frame-Abstände und Speicher, fünf saubere Zyklen.

## Phase 2: Sitzung und Pairing (M2)

| ID | Aufgabe | Aufwand | Ergebnis und Nachweis |
| --- | --- | --- | --- |
| P2.1 | Host-Daemon: Konfigurationsdatei und Zustand unter `/data/prosperosunshine`, Hostidentität (Zertifikat, UUID) beim ersten Start erzeugen und behalten | M | Neustart behält Identität |
| P2.2 | `nvhttp` live: `/serverinfo` über HTTP/HTTPS, Applist mit genau einer App „PS5-Bildschirm“, nur tatsächlich unterstützte Modi melden | M | Moonlight zeigt die PS5 nach manuellem Hinzufügen als Host |
| P2.3 | Pairing mit PIN: Entwicklungsweg über lokalen Steuer-Socket (vom PC oder MCP-Tool), Produktweg über die Host-API (P2.5); zusätzlich PS5-Benachrichtigung „Pairing angefragt“ | M | Moonlight paart; ungepairter Client wird abgewiesen; Paarung übersteht Neustart |
| P2.4 | Launch, Resume, Cancel und RTSP-Aushandlung bis `PLAY`; zweite Sitzung mit klarer Meldung ablehnen | M | Moonlight startet die App, Sitzung wird mit Stub-Medien aufgebaut und sauber beendet |
| P2.5 | Host-API für die PS5-App (andere Session): C-Header `prospero_host.h` mit Start, Stop, Status, Pairing-PIN, Fehlercodes; Transport als lokaler Socket mit kleinem JSON-Protokoll | M | Header und Protokoll mit der App-Seite abgestimmt; Testclient am PC |

**Abnahme Phase 2:** wie `PLAN.md` M2, plus: Pairing ist ohne Web-UI vollständig über Steuer-Socket bzw. Host-API möglich.

## Phase 3: Video in Moonlight (M3)

| ID | Aufgabe | Aufwand | Ergebnis und Nachweis |
| --- | --- | --- | --- |
| P3.1 | `video::capture()` für PS5: `CaptureSession` → `packet_raw_generic` mit IDR-Kennung, Frame-Index, Zeitstempel; SPS/PPS vor jedem IDR | M | Pakete landen in `mail::video_packets` |
| P3.2 | Erstes Bild in Moonlight über den unveränderten `stream.cpp`-Pfad (Paketierung, FEC, Verschlüsselung) | M | Moonlight zeigt das Home-Menü |
| P3.3 | IDR-Anforderung und Referenzinvalidierung gemäß R1 bedienen; vorhandene `frame_queue.hpp`-Regeln (Verwerfen bis IDR) anbinden | M | Künstlicher Paketverlust führt zu Erholung in begrenzter Zeit |
| P3.4 | SPS-Anpassung über `cbs.cpp` (VUI, `max_dec_frame_buffering`) prüfen und aktivieren | S | Geringere Decoder-Latenz am Client messbar oder Begründung, warum nicht nötig |
| P3.5 | `send_batch` für die PS5 (`sendmmsg`, falls vorhanden, sonst Schleife), Pacing messen | S | Keine Burst-Verluste bei 20 Mbit/s |

**Abnahme Phase 3:** wie `PLAN.md` M3: kontinuierliches Bild aus Home-Menü und Homebrew, Wiederaufbau nach Abbruch, kontrolliertes Verhalten bei Verlust und Rückstau.

## Phase 4: Controller (M4)

| ID | Aufgabe | Aufwand | Ergebnis und Nachweis |
| --- | --- | --- | --- |
| P4.1 | Native Pad-Komponente aus R5 hinter `platf::alloc_gamepad`/`gamepad_update`/`free_gamepad` | M | Tasten, Sticks, Trigger kommen an |
| P4.2 | Abbildung Moonlight → DualSense: Wertebereiche, Totzonen, PS-Taste (Guide), Touchpad-Klick (Back/Select) | S | Tabelle in `docs/input-mapping.md`, Unit-Tests |
| P4.3 | Watchdog: Pad neutralisieren bei Eingabepause und Verbindungsverlust; Konflikt mit gleichzeitigen MCP-Eingaben verhindern | S | Test: Taste halten, Kabel ziehen → keine hängende Eingabe |

**Abnahme Phase 4:** wie `PLAN.md` M4.

## Phase 5: Audio (M5, abhängig von R2)

| ID | Aufgabe | Aufwand | Ergebnis und Nachweis |
| --- | --- | --- | --- |
| P5.1 | `audio_control_t`/`mic_t` für PS5 nach Ergebnis von R2; Stereo 48 kHz, Paketdauer laut Aushandlung | M | `audio::capture()` liefert Opus-Pakete |
| P5.2 | Gemeinsame Zeitbasis mit Video, Drift über 30 Minuten messen, Korrektur festlegen | M | Messverfahren und Grenzwert vor dem Test dokumentiert |

**Abnahme Phase 5:** wie `PLAN.md` M5.

## Phase 6: Härtung und Diagnose (M6)

| ID | Aufgabe | Aufwand | Ergebnis und Nachweis |
| --- | --- | --- | --- |
| P6.1 | Gemeinsamer Sitzungszustand (bereit, Aufbau, aktiv, Abbau, Fehler) über alle Komponenten | M | Zustandsdiagramm und Tests |
| P6.2 | Fehlerinjektion: Clientabsturz, belegte Capture-Ressource, Ressourcenentzug, Ruhemodus, Teilinitialisierung | M | Testprotokoll in `docs/tests/` |
| P6.3 | Begrenzte Logs (klog und Rotationsdatei) mit Sitzungs-ID, FPS, Bitrate, Queue-Dauer, Verlust, Abbaugrund; keine Schlüssel oder PINs | S | Log-Review |
| P6.4 | Automatisierter Smoke-Test vom PC: Moonlight-Client in Docker (Kandidat: moonlight-embedded, falls ein Datei-/Null-Ausgabemodus vorhanden ist) paart, startet, zählt Frames | M | Ein Befehl prüft einen Build gegen die Konsole |

**Abnahme Phase 6:** wie `PLAN.md` M6: 30-Minuten-Sitzung, zehn Wiederverbindungen, fünf Host-Zyklen, kein Speicheranstieg, keine hängenden Eingaben oder Ressourcen; danach ein zweiter Client auf anderer Plattform.

## Release 0.1 (öffentlich)

| ID | Aufgabe | Aufwand |
| --- | --- | --- |
| P7.1 | GitHub Actions: Docker-Build, portable Tests, ELF als Artefakt | S |
| P7.2 | Lizenzprüfung: GPL-3.0 für eigenen und Sunshine-Code, Lizenztexte aller Bibliotheken im Release, Patch-Liste mit Herkunft | S |
| P7.3 | Englische Nutzerdoku: Voraussetzungen (Firmware 13.00, ELF-Loader), Installation, Pairing, bekannte Grenzen | S |
| P7.4 | Release mit ELF, Prüfsummen und Changelog | S |

Ist R2 negativ, erscheint 0.1 als Vorschau ohne Ton und benennt das deutlich.

## Nach v1.0

In dieser Reihenfolge, jeweils mit eigenem Spike: 60 FPS stabil, Moonlight-Discovery per mDNS, HEVC, Rumble und Feedback, weitere Firmware-Versionen (Offsets je Version in der Plattformdatei), HDR und 4K, mehrere Controller.

## Risikoregister

| Risiko | Wirkung | Wahrscheinlichkeit | Gegenmaßnahme |
| --- | --- | --- | --- |
| ~~Kein IDR auf Anforderung (R1)~~ | erledigt: `RequestKeyFrame` liefert IDR nach 2–3 Frames | – | – |
| ~~Kein Audio-Zugang (R2)~~ | erledigt: PCM über `OpenAudio` mit RA-Id 180 | – | – |
| ~~Boost-Teile bauen nicht (P0.2)~~ | erledigt: Boost 1.92 gebaut, ein Patch für Process v2 | – | – |
| Encoder-Bitrate über 15 Mbit/s | Encoder stoppt | sicher bei > 15 Mbit/s | Host klemmt auf 15 Mbit/s (Spitze ×1,12) |
| Firmware-Update ändert Offsets | Capture fällt aus | sicher bei Update | Strikte Versionsprüfung, kontrollierte Ablehnung |
| Avcap2 im Spiel belegt oder gesperrt | Kein Bild in bestimmten Titeln | unbekannt | Früh mit regulärem Spiel testen (P1.4), Meldung statt Absturz |
| Zuordnungsdialog des virtuellen Pads | Bedienung blockiert | mittel | R5, einmalige Bestätigung vor Streambeginn |
| Upstream-Drift bei Sunshine | Aufwändige Updates | mittel | Fester Commit, kleine dokumentierte Patches, Updates nur gezielt |

## Nächste konkrete Schritte

1. P0.1 und P0.2 (Sunshine-Pin, Patch-Mechanismus, Boost).
2. Parallel R1 und R2 als Spikes, weil sie den v1.0-Umfang bestimmen.
3. P0.3 bis P0.6 bis zum gelinkten Host-ELF, dann ADR-001 festschreiben.
