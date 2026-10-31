# Arbeitsstand

Stand: 31. Oktober 2026. Plan und Reihenfolge: `ROADMAP.md`. Ziel und Regeln: `PLAN.md`. Architektur: `docs/adr/001-sunshine-port.md`.

## Kurzfassung

**Der Host streamt Ende-zu-Ende.** Ein Client auf Basis von moonlight-common-c (derselben Protokollbibliothek, die Moonlight verwendet) paart sich mit der PS5, startet die App „PS5 Screen“ und empfängt den PS5-Bildschirm als H.264 1080p mit 59,94 fps, den Spielton als Opus und schickt Controller-Eingaben an ein virtuelles DualSense. Pairing übersteht einen Host-Neustart.

Eine 30-Minuten-Sitzung und zehn Wiederverbindungen liefen fehlerfrei.

Echte Moonlight-Clients (Moonlight Web, Moonlight PC, Moonlight auf dem iPhone im WLAN) sind gepairt und streamen; Spiele (GTA III, Mario 64) erscheinen im Stream, und Spielstart/-ende überbrückt der Host automatisch.

**PS5-App (Phase 8):** Die App „ProsperoSunshine“ (`PPSA47989`) liegt unter `app/`. Live per Controller auf der Konsole geprüft: Host-Status, PIN-Eingabe für eine echte Pairing-Anfrage (Client meldet `PairStatus=1`), Autoload über den Payload Manager an und wieder aus (Eintrag `local/prospero-sunshine` in `/data/pldmgr/autoload.txt`, andere Einträge unverändert), „Restart host“ (Host fährt sauber herunter und startet neu). Einstellungen speichern und auf Standard zurücksetzen live geprüft (`wan_encryption_mode = 0` geschrieben, danach Datei wieder leer). Wunsch für die nächste Runde: Optik auf das Niveau von ProsperoStore/ProsperoEden/Playden heben. Autoload steht aktuell auf **aus**.

**Bild-/Ton-Synchronisation (P5.2, erste Messung; die Bildwerte sind überholt, siehe „Bild und Ton beim Tastendruck“ unter „Stand 30. Oktober 2026“):** Navigationsklick im Home-Menü als gemeinsames Ereignis, 60 s, gemessen am Client (`tools/moonlight/av_sync.py`). Tastendruck → Bildänderung 23–25 ms, Tastendruck → Klickton 28–49 ms; Ton liegt 5–23 ms hinter dem Bild (burst-korrigiert Median ≈ 0 ms), also innerhalb der üblichen ±45-ms-Toleranz. Nur die Rechts-Drücke sind auswertbar (bei Links-Drücken erkennt der Bild-Detektor die Änderung nicht zuverlässig); der Drift über 60 s (−4,3 ms/min) ist zu kurz gemessen, ein 10-Minuten-Lauf steht aus.

**Noch nicht geprüft:** Moonlight auf Android/TV, andere Firmwares als 13.00.

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
| R3 | `sceAvcap2SetVideoBitrate` wirkt live; bis 15 Mbit/s (Spitze ×1,12) stabil, ab 16 Mbit/s stoppt der Encoder. Überholt: Seit dem HEVC-Einbau öffnet der Host mit der Bitrate in der Konfiguration und klemmt auf 50 Mbit/s (siehe „HEVC im Host“) | `src/bitrate_spike.cpp` |
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
- **Moonlight auf dem iPhone (Nutzerbericht, 2026-10-31):** Die PS5 erscheint ohne IP-Eingabe zum Hinzufügen (P9.2), das Pairing mit PIN-Eingabe in der App klappt, die Steuerung funktioniert.
- **Release-Archiv:** `scripts/package.sh` lokal aus dem aktuellen Build ausgeführt: 23 Dateien, `sha256sum -c` ohne Fehler, keine lokalen Pfade oder Konsolenadresse im ELF (per `grep -a`).
- **Release 0.1.0:** Inhalt von `PPSA47989.zip` auf die Konsole kopiert, App gestartet, „Restart host“ ausgelöst, danach Stream 15 s: 896 Frames, fehlerfrei decodierbar. GitHub-CI baut von null und ist grün; das fand einen Fehler in der Baureihenfolge (`nlohmann-json` vor `libdisplaydevice`), den der lokale Cache verdeckt hatte. Die SHA-256 der Release-Datei stimmt mit dem Katalog-Eintrag überein, der Katalog-Bot hat den Eintrag nach seinen Prüfungen gemergt.
- **Bild und Ton beim Tastendruck (P9.4):** Gemessen am Client mit `tools/moonlight/av_sync.py`, 44 Drücke im Abstand von 1537 ms, nur das Stream-Pad verbunden. Die Zeiten laufen vom Absenden des Drucks bis zur Ankunft am Client.

  | Vordergrund | Taste → Bild | Taste → Ton | Ton minus Bild (Median) |
  | --- | --- | --- | --- |
  | ProsperoSunshine-App | 49–85 ms, Median 57 (44/44) | 31–71 ms, Median 47 (44/44) | −10 ms |
  | Home-Bildschirm | 87–113 ms, Median 95 (44/44) | 80–122 ms, Median 105 (29/44) | +10 ms |

  Die ganze Kette samt Encoder und Netz bleibt in der App also im Median unter 60 ms; die rund 40 ms mehr auf dem Home-Bildschirm entstehen in der Oberfläche der Konsole. Der Eingabeweg im Host hat keinen eigenen Puffer oder Takt. Bild und Ton liegen in beiden Fällen innerhalb der ±45-ms-Toleranz. Die Drift bleibt klein: −0,01 ms/min über 10 Minuten, +0,00 ms/min im Lauf oben.
- **Korrektur früherer Bildwerte:** Der Bild-Detektor sprach auf die Schlüsselbilder an, die der Encoder einmal pro Sekunde sendet (32 von 37 Treffern fielen genau darauf). Die 23–25 ms der ersten Messung waren daher keine Reaktion: Links-Drücke lagen dort exakt 500 ms später (524 ms), das 1-Hz-Muster im 1,5-s-Raster. `av_sync.py` überspringt Schlüsselbilder jetzt und drückt im Abstand von 1537 ms. Im Kontrolllauf ohne Klickton (0/30, 342 ms) hatte die Startsequenz von `stream_test` zudem die App gestartet; für den 10-Minuten-Lauf (0/380, 265 ms) ist das dieselbe Sequenz und damit die wahrscheinliche Ursache, belegt ist es dort nicht. `av_sync.py` nutzt jetzt immer `--no-script`. Die 28–49 ms beim Ton aus der ersten Messung (Home-Bildschirm) lassen sich nicht mehr nachstellen; dass der Tonweg nicht langsamer geworden ist, zeigt der App-Wert oben.

**PS-Taste über Select (2026-10-31):** Moonlights Bildschirm-Controller am Handy hat keine PS-Taste. Select halten sendet sie jetzt nach 500 ms (`back_button_timeout`), ein kurzer Druck sendet Create beim Loslassen. Gemessen mit `stream_test --tick-kind back` auf dem Home-Bildschirm, je zwei Drücke im Abstand von 6 s, ausgewertet an Bildern aus dem empfangenen Stream: 700 ms halten öffnet das Kontrollzentrum ohne Create-Menü, der zweite Druck schließt es. 120 ms öffnet das Create-Menü, der zweite Druck schließt es. In Moonlight auf dem iPhone öffnet Select halten das Kontrollzentrum (Nutzertest).

**Release 0.1.1:** App aus dem Release-Build installiert (`01.000.002`, in der App angezeigt), „Restart host“ ausgelöst, danach Stream 27 s: 1614 Frames, fehlerfrei decodierbar; 700 ms Select öffnet das Kontrollzentrum über der App, der zweite Druck schließt es. Das Archiv prüft `sha256sum -c` ohne Fehler, im ELF stehen keine lokalen Pfade oder Konsolenadresse (per `grep -a`).

**Rumble (P9.10, 2026-10-31):** Der Host liest die Motorwerte des virtuellen Pads wie Remote Play und schickt Änderungen an den Client (`docs/re/pad-output.md`). Gemessen mit Homebrew UI Lab im Vordergrund und `stream_test --tick-seq left,...`, nur das Stream-Pad verbunden: 8 von 8 Abweisungen kamen als `rumble low=0 high=16191` (Stärke 0,25 auf dem kleinen Motor) und danach als Stopp an. Der Start lag 6–22 ms nach dem Absenden des Drucks, der Stopp 30–50 ms nach dem Start; alles am Client-Log abgelesen, 19 s Stream fehlerfrei decodierbar. Vor Patch 0008 kam von denselben Pulsen nur der Stopp an, also vibrierte nichts.

**Lichtleiste (P9.10, 2026-10-31):** Der Host schickt Farbänderungen des virtuellen Pads an den Client. Gemessen mit Homebrew UI Lab im Vordergrund und `stream_test`: 2 s nach Stream-Start meldet das Client-Log `led pad=0 rgb=0,0,255` (Systemblau), 50 ms später `127,240,216` (die Farbe des Titels). Rumble lief im selben Stream weiter (6 von 6 Pulsen), 0 Frame-Lücken. Ob ein echter Moonlight-Client die Farbe am Controller zeigt, ist nicht geprüft.

**HEVC-Spike (P9.7, 2026-10-31):** Avcap2 liefert HEVC (`docs/re/hevc.md`). Gemessen mit `src/hevc_spike.cpp` auf dem Home-Bildschirm, je 180 Frames in 1080p60, frischer Kanal je Stufe: Bei 6, 15, 30, 50, 80 und 100 Mbit/s kein Fehler. `ffprobe` meldet HEVC Main, Level 5.0, yuv420p, BT.709; die Streams zu 6, 50 und 100 Mbit/s decodiert `ffmpeg` mit 180 Frames und ohne Fehlermeldung, sobald das VPS vor SPS und PPS steht. Das decodierte Bild zeigt den Home-Bildschirm korrekt. 30 → 10 → 30 Mbit/s im laufenden Kanal geht, 20 → 40 Mbit/s bricht nach dem Wechsel mit `0x80c78000` ab. 1440p und 4K scheitern bei der Speicher-Reservierung (`0x81950003`). Der Code nach dem Aufräumen ergab dieselben Ergebnisse, seine Streams wurden nicht erneut decodiert. Nicht gemessen: Spielszenen mit viel Bewegung, längere Läufe, echte Moonlight-Clients.

**Schlüsselbilder alle 10 s statt jede Sekunde (2026-10-31, noch in keinem Release):** Der HEVC-Encoder ließ nach jedem Schlüsselbild Bilder aus (bei 15 und 20 Mbit/s eines, bei 5 Mbit/s fünf, also 100 ms Pause je Sekunde und 54 fps), die Bildnummern verrieten das nicht. Der Host setzt den Abstand jetzt auf 10 s (`docs/re/hevc.md`). Alle Werte mit dem Windows-Client (`NATIVE=1`):

- **Home-Bildschirm, 25 s:** HEVC 5 Mbit/s 59,62 fps (vorher rund 54), 15 Mbit/s 59,90 (vorher 58,94), 31 Mbit/s 59,94; H.264 bei 5, 15, 31 Mbit/s je 59,94. Jeweils 0 Lücken, `ffmpeg -xerror` fehlerfrei.
- **Bewegung (Demo von Mario 64, je 20 s, Stufen 5, 15, 31 und 50 Mbit/s, beide Codecs):** 0 Lücken, fehlerfrei decodierbar. H.264 59,93–59,96 fps. HEVC 59,63–59,90 fps, bei 5 Mbit/s 58,64 mit 21 Sprüngen über 25 ms und längstem Abstand 86 ms: Dort lässt der Encoder auch ohne Schlüsselbild Bilder aus. Ein Vergleich derselben Szene mit dem alten Abstand wurde nicht gemessen, die Bildqualität auch nicht.
- **Verlust (`loss_test.sh`, Docker-Client, Home-Bildschirm, 30 s):** 10 %: 1416 Frames, 96 Schlüsselbilder auf Anforderung, längster Aussetzer 170 ms. 3 %: drei Läufe, zwei mit 85 und 88 ms; der erste, wenige Sekunden nach dem Schließen eines Spiels begonnen, mit einem Aussetzer von 1,9 s, der sich nicht wiederholte. Ein Lauf mit dem Windows-Client (H.264, 20 s, 3 s nach dem Schließen eines Spiels) zeigte keine Pause, längster Abstand 19 ms; die Ursache bleibt offen, dieser Lauf lief im Docker-Client.
- **Starts:** 50 Stream-Starts ohne Fehlschlag (unten).
- **Wiederverbinden (`reconnect_test.sh`, je 6 s, ohne Pause):** Windows-Client 40 von 40. Docker-Client einmal 9 von 10 und einmal 29 von 30. Der Fehlschlag von 9/10 lag beim Host, siehe unten. Der von 29/30 war ein Timeout der TCP-Verbindung zu RTSP und trat mit dem Windows-Client nicht auf.
- **Belegt beim Öffnen (`step=resource list error=-2`, Log):** Direkt nach dem Schließen der eigenen Sitzung führte der ResourceArbitrator die Ressource noch, das erneute Öffnen scheiterte als „belegt“ und der Client bekam kein Bild. Der Host wartet jetzt bis 0,8 s darauf. In den 70 Öffnungen mit der Korrektur kam es nicht wieder vor, die Wartezeit selbst wurde dabei also nicht ausgelöst und ist nicht live geprüft.
- **Spiel im Stream (HEVC, 70 s, Mario 64 gestartet und geschlossen):** 3866 Frames, 0 Lücken, fehlerfrei decodierbar, 0 Audiofehler. Pausen 0,42 s und 1,88 s beim Start, 2,83 s beim Schließen; das erste Bild nach jeder Pause ist ein Schlüsselbild.
- **30 fps (Home-Bildschirm, 15 Mbit/s, 25 s):** H.264 29,97 fps, HEVC 29,85 fps, 0 Lücken, fehlerfrei decodierbar, periodisch ein Schlüsselbild alle 300 Frames.
- **Moonlight 6.2.0 am PC (per Kommandozeile gestartet, Home-Bildschirm, kein Spiel, keine Eingabe, je rund 45 s, 15 Mbit/s eingestellt, Moonlights Statistik im Log):** HEVC 59,93 fps empfangen und decodiert, 59,84 fps angezeigt, 0 % Netzverlust, 0,12 % durch Jitter. H.264 59,94 fps, 0 % Verlust. Der Host setzte dabei 9452 kbit/s; das ist die Rechnung von Sunshine für FEC und Ton (`rtsp.cpp`), keine Klemme des Hosts.
- **Nicht geprüft:** Moonlight in einem Spiel und auf anderen Geräten mit diesem Stand; Dauertest über mehrere Stunden (ein 2-Stunden-Lauf wurde auf Wunsch des Nutzers früh abgebrochen und nicht ausgewertet).

**Bericht von Firmware 10.40 (Issue 2, 2026-10-31, nicht selbst gemessen):** Host, Pairing und „Start at boot“ laufen laut Bericht, `OpenVideo` scheitert mit `0x81950008`. Der Host wiederholt den Aufruf jetzt und schreibt die aktiven Ressourcen in den Bericht; `open_probe.elf` probiert Varianten durch (`docs/firmware-porting.md`). Auf 13.00 geprüft: Der neue Host öffnet wie bisher und streamt H.264 und HEVC, der Probe läuft durch. Auf 10.40 ist beides ungeprüft.

**Testclient als Windows-Programm (2026-10-31):** `tools/moonlight/build_windows.sh` baut `stream_test.exe`; `NATIVE=1` nutzt es.

- **Docker im Vergleich:** Gleiche Szene (Home-Bildschirm), 20 s, Bitrate-Anforderung 80 Mbit/s (der Host klemmt auf 50). Windows: H.264 1195 und 1196 Frames, HEVC 1180 Frames, jeweils 0 Lücken. Docker: 300 und 224 Frames (H.264), 365 (HEVC), längster Aussetzer bis 3,7 s. Aussagen zu Verlust und hohen Bitraten aus dem Docker-Client gelten also für dessen Netz, nicht für den Host.
- **Fehlstarts:** Mit dem alten Schlüsselbild-Abstand 100 Starts in Folge (je 5 s, abwechselnd H.264 und HEVC, Home-Bildschirm, kein Fehlschlag, 0 Lücken, H.264 295–299 Frames, HEVC 266–269), mit dem neuen 50 Starts (kein Fehlschlag, 0 Lücken, H.264 295–297 Frames, HEVC 267–287). Zusammen mit den Messungen unten: Im Docker-Client 1 Fehlschlag in 86 Starts, im Windows-Client 0 in 150. Das spricht gegen den Host als Ursache, belegt es aber nicht; die Stichprobe könnte bei dieser Rate auch zufällig fehlerfrei sein (rund 17 %). HEVC beginnt rund 0,4 s später als H.264.

**HEVC im Host (P9.11, 2026-10-31):** Der Host bietet auf Firmware 13.00 HEVC an (`ServerCodecModeSupport` 257) und öffnet H.264 und HEVC jetzt mit der Bitrate des Clients in der Konfiguration. Grenze 50 Mbit/s statt 15. Alles gemessen mit `stream_test` (`--hevc`), Bildquelle meist die Demo von Mario 64. Echter Client siehe „Nutzertest“.

- **Nutzertest (2026-10-31):** Moonlight am PC, GTA III: „lief gut“. Laut Host-Log HEVC, 1080p60, 15 Mbit/s, über 8 Minuten ohne Capture-Ausfall; dann zwei Entzüge (`0x81950009`) kurz hintereinander, nach 0,6 und 0,35 s wieder da. Ob sie vom Spielstart oder -ende kamen, ist nicht festgehalten. Moonlight auf dem iPhone: „ging auch gut“. Laut Host-Log wählte es von selbst HEVC, bat um 720p60, bekam 1080p60 bei 7,3 Mbit/s, rund 2 Minuten; kurz vor dem Ende dieselben zwei Entzüge kurz hintereinander, nach 0,6 und 0,4 s wieder da. Moonlight am PC mit GTA III bei hoher Bitrate: „hat gut geklappt“. Laut Host-Log HEVC, 1080p60, 49,5 Mbit/s, rund 5,5 Minuten ohne Capture-Ausfall; rund 9 s vor dem Ende wieder die zwei Entzüge, nach 0,6 und 0,3 s wieder da. Dieses Paar trat in allen drei Sitzungen auf, passt also vermutlich zum Schließen des Spiels. Die tatsächlich erzeugte Datenrate und die Bildqualität sind nicht gemessen, das Urteil ist der Eindruck des Nutzers.

- **Stream:** HEVC und H.264 bei 2, 15 und 31 Mbit/s (HEVC-Werte 2 und 31, dazu 15 bei 30 fps; H.264 alle drei und 15 bei 30 fps), je 10 s: 0 Frame-Lücken, `ffmpeg -xerror` fehlerfrei. Ein Lauf (HEVC 2 Mbit/s) begann rund 2 s später als die anderen, die Ursache ist nicht geklärt.
- **Dauer:** HEVC 31 Mbit/s über 120 s: 7082 Frames decodiert, 5 Lücken mit zusammen 20 fehlenden Frames (je 68–72 Pakete angekommen, passend zur Docker-Grenze unten; der Client holte sich jeweils ein IDR). H.264 31 Mbit/s über 60 s: 3595 Frames, 0 Lücken.
- **Spiel schließen und neu starten im Stream:** HEVC läuft 1,7 s nach dem Entzug weiter (`0x81950009`, reopened), 2425 Frames in 45 s, 0 Lücken, Ton ohne Fehler.
- **Codec-Wechsel:** H.264 → HEVC → HEVC → H.264 in aufeinanderfolgenden Sitzungen fehlerfrei. Öffnet der Ton den Kanal zuerst, öffnet ihn das Bild mit dem richtigen Modus neu (`ps_capture_mode_current`).
- **Encoder ohne Netz (`hevc_spike`, Mario 64, 3 s je Stufe):** HEVC 6–100 Mbit/s und H.264 15–50 Mbit/s ohne Fehler; bei 100 Mbit/s entstanden 33 Mbit/s, größter Frame 239 KB. Eine Szene, die 50 Mbit/s voll ausschöpft, wurde nicht gemessen.
- **Netz (`udp_burst.py`, je 180 Bursts zu 1024 Byte, 60 pro Sekunde):** Windows nativ empfängt 60, 100, 150 und 250 Pakete je Burst vollständig (bis 122 Mbit/s). Im Docker-Container kommen 60 vollständig an, von größeren Bursts nur 67–93 Pakete. Bei 55 Mbit/s angefordert und viel Bewegung bricht der Testclient daher ab (6 von 600 Frames), obwohl der Host fehlerfrei sendet. Über 31 Mbit/s liegt daher nur der Nutzertest vor.
- **Nicht geprüft:** das Verhalten auf anderer Firmware (nur im Code).

**Release 0.1.2:** App aus dem Release-Build installiert (`01.000.003`, in der App angezeigt), „Restart host“ ausgelöst, das Log meldet „H.264 and HEVC“. Danach mit Homebrew UI Lab je 15 s Stream über `stream_test`: HEVC 879 und H.264 897 Frames, fehlerfrei decodierbar, 0 Frame-Lücken, je 6 von 6 Rumble-Pulsen, Lichtleiste `0,0,255` und `127,240,216`. Das Archiv prüft `sha256sum -c` ohne Fehler, im ELF stehen keine lokalen Pfade oder Konsolenadresse (per `grep -a`). Veröffentlicht am 2026-10-31: Die Store-API (`/api/v1/apps/PPSA47989.json`) meldet `0.1.2`, `01.000.003` und die sha256 des Release-Zips. Die Installation dieser Version aus dem Store ist nicht geprüft.

**Diagnose und Nachprüfungen (2026-10-31, nach 0.1.2, noch in keinem Release):**

- **Diagnosebericht:** Die Seite „Diagnostics“ zeigt den Bericht auf der Konsole; per `curl http://<console>:47996/` kam er mit HTTP 200 an, während die Seite offen war. `grep` auf Konsolenadresse und Gerätenamen: 0 Treffer. Er nennt Firmware `13.00 (0x13000040), verified`.
- **„Start at boot“ bei fremder Datei (Issue 2):** `autoload.txt` per FTP als uid 0, Modus 0600 nachgestellt, wie sie der Payload Manager laut Quelle anlegt. Mit laufendem Host liest die App den Stand über den Host, der Schalter klappt, die Datei hat danach Modus 0666 und den Eintrag. Ohne Host meldet die App `errno 13` und bittet, den Host zu starten. Das ist die Bedingung der gemeldeten Meldung; dass auf 10.40 dieselbe Ursache vorliegt, ist nicht belegt. Der Moonlight-Fehler aus Issue 2 bleibt ungeklärt.
- **Hinweise zur Capture:** `avcap_spike.elf` hielt die Capture, ein Stream endete mit `stream_test` Exit 4; der Bericht zeigte `open failed`, Fehler -2 und den Hinweis auf eine belegte Capture. Nach einem normalen Stream steht dort `last open ok`. Die Zeile auf der Startseite ist für diesen Fall nicht fotografiert (Screenshot in dem Zustand verweigert). Die Fälle „failed“ und „abgewiesene Firmware“ wurden mit einer gefälschten `host_diag.log` angezeigt, HDR nur im Unit-Test.
- **`hevc_mode = 1`:** `ServerCodecModeSupport` ist 1 (sonst 257), ein H.264-Stream läuft, eine HEVC-Anfrage liefert keine Daten.
- **`back_button_timeout = -1`:** Mit dem Standard öffnet Select 800 ms halten das Kontrollzentrum (Frame 600 des Streams), ein zweiter Halt schließt es. Mit `-1` öffnet es sich nicht, oben rechts erscheint das Capture-Symbol, der kurze Druck „Create“ wirkt also weiter.
- **Bildreaktion und Takt (`av_sync.py`, 600 s, Home-Bildschirm):** Bild 380/380, Median 96,0 ms (86,2–135,9, sd 6,3). Ton 221/380, Median 105,2 ms, also 8,6 ms nach dem Bild, Trend +0,00 ms/min. Bild und Ton driften beide +2,39 ms/min gegen die Uhr des PCs, gegeneinander +0,00. Die 265 ms vom 2026-10-29 kamen mit `--no-script` nicht wieder. Das passt zur Startsequenz als Ursache (Korrektur oben), direkt belegt ist sie weiterhin nicht.
- **Fehlstart:** Der ENet-Fehler nach dem Schließen des ProsperoStore kam in 6 von 6 Versuchen nicht wieder. In einer Reihe von 20 Starts scheiterte Lauf 7 anders: RTSP `SETUP streamid=video` lief nach dem erfolgreichen Audio-SETUP in den Fehler 110, der Host beendete „PS5 Screen“ nach seinen 10 s ohne Sitzung (Patch 0007). 60 weitere Starts mit Debug-Log liefen fehlerfrei. Zusammen 1 Fehlschlag in 86 Starts; ob Host oder Testclient in Docker schuld war, ist offen.
- **Adaptive Trigger (P9.10, Host-Log auf Stufe `debug`, Nutzertest mit Moonlight am PC und DualSense):** Der Host reicht das Trigger-Feld des Pads an den Client weiter. Ruhewert `030500000000000000000000050000000000000000000000`. Teardown setzt im Auto beide Blöcke auf `21 fe03 …`: Laut Nutzer werden L2 und R2 spürbar härter, das Log passt dazu. Mit dem Werkzeug setzt es nur den zweiten Block (`21 fe03 48922409 …`), laut Nutzer wird dann nur R2 hart; die Zuordnung stimmt also. GTA III sendete in zwei Sitzungen nichts, die Szene ist nicht festgehalten. Änderungen kommen im Abstand von Sekunden. **Grenze:** Nach dem Beenden des Streams bleibt der Trigger hart, beim nächsten Verbinden wird er weich (beides Nutzertest); der Host schickt beim Anlegen des Pads „aus“, am Testclient als `flags=0xc types=5/5` gemessen. Ursache laut Quelle: Moonlight PC v6.2.0 setzt die Effekte beim Beenden nicht zurück (`docs/re/adaptive-triggers.md`).
- **Eingabe:** Solange das RemoteEye-Testpad neben dem Host-Pad verbunden war, meldete der Nutzer in Teardown keine Eingabe; nach `ps5_detach` ging sie. Mit zwei Client-Controllern hängen zwei Host-Pads, Teardown nutzte Pad 0.
- **Ruhemodus, erster Test, Host ohne Erholung (mit Freigabe des Nutzers, `ps5_rest`, nach rund 90 s `ps5_wake`): Der Host überlebte ihn nicht brauchbar.** Im Ruhemodus antwortet die Konsole auf Ping, nicht auf Port 47989. Das Aufwecken meldete nach 11 s „on“, die Konsole stand dann auf „Press the PS button“ und der Nutzerauswahl. Der Host-Prozess läuft danach noch (gleiche PID), aber die Ports 47989, 47984, 48010 und 47996 bleiben zu, auch über 4 Minuten nach dem Einloggen. Sein Log endet mit `Couldn't accept incoming connections: error_code=0x000000a3` aus dem RTSP-Listener. ELF-Loader, FTP und klogsrv antworten dagegen weiter. Danach hingen auch die Werkzeuge: `ps5_launch` und Screenshot liefen in den Timeout, das RemoteEye-Pad meldete den Port als belegt. Auch der ELF-Loader nahm einen neu gesendeten Host an, ohne dass der hochkam, und FTP antwortete rund 15 Minuten nach dem Aufwecken nur noch mit Timeouts, klogsrv und 9021 waren zeitweise zu. Ob das am Ruhemodus selbst, an der Homebrew-Umgebung oder am alten Host-Prozess lag, ist offen. Am Fernseher stand laut Nutzer nur noch das Hintergrundbild der App, die Konsole musste hart ausgeschaltet werden. Nach dem Neustart und dem Laden der Homebrew-Umgebung lief der Host wieder über den Autostart.
- **Neustart der Dienste ohne Ruhemodus (`relisten_probe.elf`, Befehl `RELISTEN`):** Der Host startet seine Dienste im selben Prozess neu, das Log zeigt die neue Steuer-Socket-Zeile 0,25 s nach „Starting the services anew“. Danach `serverinfo` frei, Stream 10 s: 594 Frames H.264, Exit 0. Mitten im Stream: Sitzung vom Host beendet (`terminated_by_host`, Exit 0), Pad getrennt, danach HEVC 10 s: 598 Frames, Exit 0. „Restart host“ in der App läuft danach normal. Im normalen Betrieb gab es keine Fehlauslösung (30 s Stream: 1794 Frames). Im Log steht je Neustart einmal `Couldn't accept incoming connections: Operation canceled`, das ist das Beenden des alten Listeners. 
- **Ruhemodus mit 0.1.3 (zwei Tests mit Freigabe des Nutzers, Home-Bildschirm, kein Spiel offen, kein Stream, je rund 90 s, Aufwecken über das Netz):** Nach dem Aufwecken antwortet `serverinfo` beim ersten Abruf (`SUNSHINE_SERVER_FREE`), FTP sofort. Im zweiten Test, mit dem endgültigen Stand, steht im Log `Starting the services anew`, das Log vor dem Ruhemodus liegt als `sunshine.log.1`. Nach dem Einloggen HEVC 15 s: 895 Frames, H.264 15 s: 896 Frames, beide Exit 0; die App startet normal. Beobachtung danach alle 30 s `serverinfo` und FTP: erster Test 20 Minuten, zweiter 10 Minuten, jeweils 0 Fehler; danach noch ein Stream mit 597 Frames. Der Hänger des ersten Tests kam nicht wieder, seine Ursache bleibt offen. **Nicht geprüft:** Aufwecken per Controller, Ruhemodus mit offenem Spiel oder laufendem Stream, längerer Ruhemodus, Stream vor dem Einloggen.
- **Release 0.1.3 (2026-10-31):** Vor dem Release auf der Konsole installiert (`01.000.004`, in der App angezeigt), HEVC und H.264 je 15 s fehlerfrei, `sha256sum -c` ohne Fehler, CI grün. Die Digest-Angabe des GitHub-Assets `PPSA47989.zip` stimmt mit dem lokalen Archiv überein. Der Katalog-PR ist gemergt; die Store-API meldet `0.1.3` mit derselben SHA-256 wie das Release-Archiv. Die Installation aus dem Store ist nicht geprüft.
- **Tests:** `scripts/test.sh` und `app/tests/run.sh` bestanden. Letzter Stand der App auf der Konsole, 8 s Stream: 473 Frames, `stream_test` Exit 0.

Noch offen:

1. **Auflösung (P9.3):** Avcap2 liefert in der jetzigen Konfiguration nur 1080p (`src/mode_spike.cpp`): 30 und 60 fps gehen (90 bzw. 180 Frames in 3 s, fehlerfrei decodierbar), mit 360p, 540p, 720p, 1440p und 4K scheitert das Öffnen, und `sceAvcap2ChangeScanInResolution` lehnt ab (`0x81950001`, `0x8195000A`). Der Host beachtet daher nur die Bildrate (bis 30 fps gewünscht: 30 fps); Moonlight skaliert 1080p selbst herunter.
2. **Mehrere Controller am selben Nutzer:** Die Oberfläche der Konsole nimmt Eingaben von allen an, die App aber nur von einem. Beobachtet mit dem RemoteEye-Test-Pad: War es zuerst verbunden, ignorierte die App das Stream-Pad (44 Drücke, keine Bildänderung); nach `ps5_detach` reagierte sie auf alle 44. Das passt zu P9.8 in umgekehrter Reihenfolge. Mit einem echten DualSense verhielt es sich anders (Nutzertest, iPhone, 2026-10-31): Er trennte sich beim Verbinden, die Moonlight-Steuerung ging sofort, auch in Mario 64. Wieder eingeschaltet während des Streams reagierte nur Moonlight, nach Stream-Ende arbeitete der DualSense normal. Im Klog meldet die Konsole beim Anmelden des Stream-Pads `DEVICE_REMOVED` für den DualSense; im Host-Code gibt es dafür keinen Aufruf, der Grund liegt also auf Konsolenseite und ist unbekannt. Für den Stream ist das kein Hindernis, daher bleibt es bei der Beschreibung in der README.
3. **Payload-Manager-Fenster:** Ein offenes Fenster des Payload Managers liegt über Apps und nimmt Eingaben an sich, auch wenn es nicht reagiert. In dem Zustand lässt es sich nur am echten Controller schließen. Beim Fernsteuern vorher prüfen.
4. **Eigene Werkzeuge vom PC:** `PIN`/`QUIT` und `pair.py status` über den Steuer-Socket gehen vom PC aus nur noch mit der leeren Datei `/data/prosperosunshine/control_lan` (danach Host neu starten). Sonst geht der Host-Neustart über die App.
5. **Store-Installation (P8.9):** Die Installation aus dem ProsperoStore klappt (Nutzerbericht, 2026-10-31).
6. **Rumble je Spiel:** Im Nutzertest (2026-10-31) vibrierte der Controller in Moonlight beim Streamen. Welche Spiele den klassischen Modus nutzen und welche nur die Haptik, ist offen.
7. **Seltene Fehlstarts:** Ein Stream direkt nach dem Schließen des ProsperoStore scheiterte (`stream_test` Exit 3: `Failed to establish ENet connection on UDP port 47999`, Fehler 11; im Host-Log `Ping Timeout` und `Initial Ping Timeout`). Der Host blieb betriebsbereit, die Wiederholung lief fehlerfrei. Nachgeprüft: 1 anderer Fehlschlag in 86 Starts (oben). Die Ursachen sind nicht geklärt; im Windows-Client kam in 150 Starts kein Fehlstart vor (oben).
8. **Weiter offen wie bisher:** Ruhemodus in den oben genannten Fällen (Tests nur mit Rückfrage), Android/TV-Clients, andere Firmwares (Checkliste in `docs/firmware-porting.md`), Wake-on-LAN (unten).

- **MAC-Adresse und Wake-on-LAN:** Der Host meldet die MAC gepaarten Clients: `/serverinfo` über HTTPS mit Client-Zertifikat liefert die Adresse, die auch die ARP-Tabelle des PCs zeigt (`pair.py info`). Die frühere Angabe „MAC ist 0“ war ein Messfehler: Über HTTP gibt Sunshine für alle Clients absichtlich einen Platzhalter aus Nullen zurück (`nvhttp.cpp`). Ein Einmal-Probe auf der Konsole bestätigte die Quellen: `sceNetGetMacAddress(…, 0)`, `sceNetCtlGetInfo` und `SIOCGIFCONF` liefern dieselbe Adresse. **Nicht geprüft:** ob ein Magic Packet, wie Moonlight es sendet, die Konsole aus dem Ruhemodus weckt; das braucht einen Ruhemodus-Test.
- **Boost.Filesystem `create_directories`** scheitert bei neuen verschachtelten Pfaden, wenn `errno` vorher belegt ist (ein Libc-Aufruf setzt kein `errno`). Der Host nutzt nur `std::filesystem` und ist nicht betroffen.
- **Zweite RTSP-Verbindung** wird ohne erklärende Fehlermeldung geschlossen (Upstream-Verhalten).
- **Eingaben nach hartem Verbindungsabbruch** wirkten mit Sunshines Standard (`ping_timeout` 10 s) ≈ 5,6 s nach. Mit dem neuen Host-Standard von 2 s (Moonlight pingt alle 100 ms) erkennt der Host den Abbruch nach 1,8 s und setzt das Pad dann neutral (gemessen mit `tools/moonlight/hold_kill_test.sh`).
- **Entzug der Capture-Ressource** passiert bei Spielstart/-ende (immer) und sporadisch wenige Sekunden nach Anmeldung des virtuellen Pads (2 von 7 Sitzungsstarts; in vier gezielten Wiederholungen mit vollem Kernel-Log nicht reproduziert). Der Host stellt Video und Ton jeweils nach ≈ 2 s wieder her; der Client sieht eine kurze Pause.
- **Nur eine Sitzung gleichzeitig:** Eine zweite Streaming-Sitzung (z. B. Moonlight Web, das während einer laufenden Sitzung erneut verbindet) wird beendet, weil sonst zwei Leser denselben Avcap2-Kanal leeren.
- **Avcap2-Symbolauflösung war nicht threadsicher:** `kernel_dynlib_dlsym` aus mehreren Threads gleichzeitig ließ Ton und Keyframe-Anforderung scheitern; alle Funktionen werden jetzt einmal beim Öffnen aufgelöst.
- **Log-Stufe:** `min_log_level` in `/data/prosperosunshine/sunshine.conf` gilt auch für das Kernel-Log (z. B. `min_log_level = debug` für Diagnose).
- **Steuer-Socket** ist im LAN offen und nur für Entwicklung gedacht; die Host-API für die PS5-App (P2.5) ist noch nicht abgestimmt.
- **Kein HDR, keine Maus/Tastatur** (v1.0-Umfang).
- **Rumble nur klassisch:** Die Haptik des DualSense kommt nicht an (`docs/re/pad-output.md`). Adaptive Trigger gehen nur mit Moonlight am PC und bleiben nach dem Stream hart, bis zum nächsten Verbinden (`docs/re/adaptive-triggers.md`).

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
