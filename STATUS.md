# Arbeitsstand und Übergabe

Stand: 28. Oktober 2026. Auf Wunsch des Nutzers wegen fast aufgebrauchter Usage angehalten.

## Auftrag und Aufteilung

Codex übernimmt den nativen Sunshine-kompatiblen Host-Core auf der PS5: Capture, Moonlight-Protokoll, Streaming, Audio und Eingaben. Die andere Claude-Session kann die PS5-App, Oberfläche und deren Anbindung bauen. Es wurde keine Nachricht an eine andere Session gesendet.

**ProsperoSunshine ist noch kein funktionierender Moonlight-Host.** Fertig sind ein getestetes natives Capture-Backend, Build-Skripte, ein Laufzeittest und erste Regeln für die Video-Warteschlange. Pairing, RTSP, Medienübertragung, Audio und Moonlight-Controller-Anbindung fehlen noch.

Der zusätzliche Auftrag, die neue Screenshot-Methode in ps5-mcp einzubauen, ist implementiert und live geprüft.

## 1. MCP-Screenshots funktionieren jetzt direkt über Avcap2

Repository: `../ps5-mcp`

`ps5_screenshot` verwendet standardmäßig den Backend-Modus `auto`. Bei vorhandenem Payload und Decoder auf Firmware 13.00 mit SDR nimmt es den zusammengesetzten Bildschirm über Avcap2 auf. Es drückt dabei keine Taste, verbindet keinen virtuellen Controller und legt keinen Galerieeintrag an. Crop und Größenanpassung bleiben erhalten.

Technischer Ablauf:

1. Server patcht eine zufällige 32-stellige Request-ID in den ELF-Argumentslot `PS5MCP-CAPTURE-V1:`.
2. Payload erfasst ungefähr eine halbe Sekunde H.264 einschließlich SPS/PPS.
3. Payload schließt den Capture-Channel und gibt die ResourceArbitrator-Registrierung frei.
4. Erst danach veröffentlicht es die eindeutig benannte `.h264`-Datei und eine `.status`-Datei unter `/data/ps5mcp-avcap-<request>`.
5. Server holt die Dateien per FTP, löscht sie und decodiert das erste vollständige Bild mit FFmpeg.

`auto` fällt bei fehlenden Voraussetzungen oder nicht unterstützter Firmware/HDR auf die bestehende Galerie-Methode zurück. Ein belegtes Capture-Profil oder ein tatsächlicher Capture-Fehler führt zu einer Fehlermeldung, nicht zu einer automatischen Tastenbetätigung.

Konfiguration:

- `PS5_SCREENSHOT_BACKEND=avcap`: direkte Methode verlangen.
- `PS5_SCREENSHOT_BACKEND=gallery`: alte Methode erzwingen.
- `PS5_AVCAP_ELF`: optionaler Pfad zum Payload; ansonsten dieselbe Suchlogik wie bei den anderen Payloads.
- `PS5_FFMPEG`: optionaler Decoderpfad. Normalerweise liefert die neue Python-Abhängigkeit `imageio-ffmpeg` den Decoder.
- `PS5_SAVE_DIR`: speichert bei direkter Aufnahme das decodierte PNG in voller Auflösung lokal.

Wichtige Dateien und Änderungen:

- `../ps5-mcp/server/ps5mcp/screen.py`: Avcap2-Backend, Request-Zuordnung, Decoder, Validierung und Backend-Auswahl.
- `../ps5-mcp/server/ps5mcp/tools.py`: Screenshot benötigt bei direkter Aufnahme kein RemoteEye; reine Screenshot-/Wait-Sequenzen ebenfalls nicht.
- `../ps5-mcp/server/ps5mcp/config.py`: neue Konfigurationsvariablen.
- `../ps5-mcp/payload/avcap_probe/avcap_probe.c`: Request-Slot und atomare Ergebnisveröffentlichung nach Cleanup. Ohne Request-ID bleibt der bisherige eigenständige Zwei-Sekunden-Probe nutzbar.
- `../ps5-mcp/server/ps5mcp/fetch.py`: lädt das neue ELF aus Releases mit, wenn vorhanden; ältere Releases bleiben nutzbar.
- `../ps5-mcp/.github/workflows/payloads.yml`: neuer Build-/Release-Job für `avcap_probe.elf`.
- `../ps5-mcp/server/test_server.py`: acht neue Tests für direkte Screenshots; bestehende Galerie-Fixtures bleiben explizit beim Galerie-Backend.
- README, CLI-Hilfe, Python-Projekt und requirements wurden angepasst.

Nachweise:

- **59 Python-Tests bestanden**, Laufzeit 68,737 Sekunden.
- **Live-MCP-Screenshot mit erzwungenem Avcap-Backend bestanden**; das Bild des Home-Menüs wurde visuell geprüft und ist nicht schwarz.
- **Live-`ps5_selftest(screenshot=true)` mit erzwungenem Avcap-Backend: alle elf Prüfungen bestanden.**
- Das ELF wurde mit SDK v0.45 und `-Wall -Wextra -Werror -O2` neu gebaut.
- Das lokale Python-Paket wurde erneut mit `py -3.15 -m pip install -e .` installiert; `imageio-ffmpeg 0.7.0` ist installiert.

Das neue Payload liegt lokal unter `../ps5-mcp/payload/avcap_probe/out/avcap_probe.elf`. Es wurde kein Release veröffentlicht. Bereits laufende MCP-Serverprozesse laden Python-Module nicht automatisch neu; die Live-Prüfung erfolgte mit frisch gestarteten MCP-Prozessen über das echte stdio-Protokoll.

Restpunkte: Der Kommentar ganz oben in `avcap_probe.c`, wonach noch keine MCP-Integration bestehe, ist jetzt veraltet und muss korrigiert werden. Nach einem PC-Abbruch oder Timeout können Request-Dateien unter `/data` liegen bleiben; durch die eindeutigen IDs werden sie nicht als neue Aufnahme zurückgegeben. Der neue optionale Release-Download ist noch nicht gegen einen tatsächlich veröffentlichten Release geprüft; der CI-Job wurde lokal nicht auf GitHub ausgeführt.

## 2. ProsperoSunshine: vorhandener nativer Code

Projekt: dieses Repository

| Datei | Aktueller Inhalt |
| --- | --- |
| `src/platform/ps5/capture.h` | Bereits implementierte C-Schnittstelle: `ps_capture_open`, `ps_capture_headers`, `ps_capture_read`, `ps_capture_close`. |
| `src/platform/ps5/capture.c` | Avcap2-/ResourceArbitrator-Backend mit Firmware-Prüfung, exklusiver Sitzung, Ressourcenentzug und Cleanup. |
| `src/runtime_probe.cpp` | C++23, `std::thread`, Atomics, Vector, monotone Uhr sowie TCP-/UDP-Sockets. |
| `src/capture_probe.cpp` | Zehn-Sekunden-Aufnahme über die neue Bibliothek; prüft auch das Ablehnen eines zweiten Open im selben Prozess. |
| `src/video/annex_b.hpp` | IDR-Erkennung in Annex-B-Daten. |
| `src/video/frame_queue.hpp` | Begrenzung nach Framezahl, Bytes und Alter; bei Verlust/Überlauf bis zum nächsten IDR synchronisieren. Noch nicht an einen Netzwerktransport angeschlossen. |
| `tests/video_test.cpp` | Tests für Decoderstart, Frameverlust, Grenzen und IDR-Wiederaufnahme. |
| `scripts/Dockerfile` | Lokales Debian-Buildimage mit LLVM 18, SDK v0.45, FFmpeg und Sanitizer-Laufzeit. SDK-ZIP wird per SHA256 geprüft. |
| `scripts/build.ps1`, `scripts/build.sh` | Bauen die nativen Probes und `libprospero_capture.a`. |
| `scripts/test.sh` | Führt die portablen Videotests unter AddressSanitizer und UndefinedBehaviorSanitizer aus. |
| `PLAN.md` | Ursprünglicher Gesamtplan. Seine Einleitung und einige Statusangaben stammen noch von vor diesem Implementierungsschritt; dieses Statusdokument ist aktueller. |

Die Projektstruktur enthält bislang weder eine App-Oberfläche noch eine vollständige Host-API für Start/Stop/Pairing. Claude sollte gegen die dokumentierte C-Capture-Schnittstelle arbeiten beziehungsweise die spätere Host-API abstimmen, keine bereits fertige Moonlight-Verbindung voraussetzen. Getestet ist ein privilegierter ELF-Payload, nicht das Einbetten in einen beliebigen App-Prozess.

### Build und Tests

Im Projektordner:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
docker run --rm --mount "type=bind,source=$PWD,target=/repo" prosperosunshine-sdk:v0.45 bash scripts/test.sh
```

Das Dockerimage `prosperosunshine-sdk:v0.45` ist bereits lokal vorhanden. Es laufen keine von dieser Arbeit gestarteten dauerhaften Container. Buildprodukte liegen unter `out/` und sind ignoriert:

- `runtime_probe.elf`
- `jthread_probe.elf`
- `deps_probe.elf`
- `deps/`: statische PS5-Bibliotheken, Header und Lizenztexte aus `scripts/deps.sh`
- `capture_probe.elf`
- `libprospero_capture.a`
- `capture.o`
- `video_test`
- `capture.h264`: von der Konsole heruntergeladene Aufnahme des neuen Backends.

### Verifizierte Ergebnisse auf der PS5

Firmware: **13.00**, Rohwert `0x13000040`. Entwicklungskonsole im lokalen Netz. Loader 9021, FTP 2121, Klog 3232.

Der native Laufzeittest startete erfolgreich und meldete:

```text
runtime probe firmware=0x13000040 cplusplus=202302
runtime result=PASS threads=6 sockets=1 elapsed_us=306
```

Der neue Capture-Probe startete ebenfalls erfolgreich und meldete:

```text
capture result=PASS frames=601 bytes=1583714 idrs=11 duration_ms=10023 read_gap_us=2929..18298
Stop rc=0
Close rc=0
Terminate rc=0
STOP rc=0
unregister rc=0
```

Die Ergebnisdatei wurde mit **1.583.754 Bytes** nach `out/capture.h264` heruntergeladen. **Am 28.10.2026 mit ffprobe/FFmpeg (Docker-Image) geprüft: 601 Frames, H.264 Main, 1920×1080, yuv420p, 11 I-Frames, 590 P-Frames; vollständige Decodierung ohne einen einzigen Fehler.** Der rohe Annex-B-Strom trägt keine Zeitstempel; die von ffprobe gemeldete Bildrate ist geraten und kein Nachweis. Der ältere Avcap2-Probe und die neuen MCP-Screenshots sind dagegen bereits erfolgreich decodiert. Die 601 Reads belegen noch keinen stabilen 60-FPS-Stream über längere Zeit oder in Spielen.

Der portable Videotest bestand mit AddressSanitizer und UndefinedBehaviorSanitizer. Beide live gestarteten Probes beendeten sich mit Exit-Code 0. Das Capture-Log zeigt nach dem Abbau keine aktive eigene Ressource 180 und „No kicking Apps“. Die Firmware protokollierte beim Prozessende außerdem eine `fdescfree/BUDGET_FD_FILE`-Warnung, wie zuvor bei Payloads; deren Ursache wurde nicht untersucht.

### Kritische ABI-Erkenntnisse

- Firmware-spezifische RA-Offsets ausschließlich hinter der 13.00-Prüfung verwenden.
- Avcap2-Konfiguration ist `0x2E0` Bytes groß. Alte SharpProspero-Bindings mit `0x2B8` Bytes nicht unverändert einsetzen.
- Quellenauswahl `0x2A4 = 2` liefert den zusammengesetzten Bildschirm. `3` war app-only und verursachte schwarze Home-Menü-Aufnahmen.
- ResourceArbitrator-Profil **180** ist keine Benutzer-ID. Bei bestehender Belegung ablehnen.
- `ReadVideo` benötigt Zielpuffer, Kapazität und einen Descriptor von `0x70` Bytes. Positiv bedeutet Bytezahl; `0x81950002` bedeutet noch kein Frame.
- `ps_capture_read` normalisiert „noch kein Frame“ auf 0. Negative Ergebnisse sind die Fehlercodes aus `capture.h`.
- SPS/PPS werden separat gelesen und mit Annex-B-Startcodes versehen.
- Die Bedeutung der Zeitstempelfelder im Descriptor ist noch nicht nachgewiesen. Aktuelle Zeitmessungen sind lokale Lesezeiten.
- Erzwungene IDR-Anforderung, dynamische Bitrate und Bildratensteuerung sind noch nicht implementiert oder nachgewiesen.

## 3. Sunshine-Portierung: Recherche und offene Arbeit

Als Referenz heruntergeladen wurde Sunshine-Commit:

`a679b059096bcdd502e2192ce4f6a3c3732fbaae`

Die unveränderten Quellen liegen nur im ignorierten Cache:

`.cache/Sunshine-a679b059096bcdd502e2192ce4f6a3c3732fbaae`

ZIP-SHA256: `dcc1e7d4a3e59dcaf81a2f01b00440b51c3ea22f53189c87cc8a57d1d2bb217c`.

Relevante festgehaltene Unterprojekt-Commits aus dem Git-Baum dieses Sunshine-Commits:

- moonlight-common-c: `04053843c90c4268583f2496b997d54d59f03957`
- Simple-Web-Server: `546895a93a29062bb178367b46c7afb72da9881e`
- build-deps: `d0ac5ef4b7673a8d8d2b03981ab5226cbf25810c`

### Abhängigkeiten für PS5 (Stand 28.10.2026)

Pins mit SHA256 stehen jetzt in `deps.lock`. `scripts/deps.sh` lädt sie nach `.cache/deps/dl`, prüft die Hashes, baut statisch nach `out/deps` (lib, include, licenses) und wird von `scripts/build.sh` automatisch aufgerufen, falls `out/deps` fehlt. Das Dockerimage enthält jetzt zusätzlich `make`.

| Bibliothek | Version/Pin | Build |
| --- | --- | --- |
| OpenSSL | 3.5.9 (LTS, Hash = offizieller Upstream-Hash) | `BSD-x86_64`, nur libcrypto/libssl, `no-async no-dso no-engine no-module` |
| Opus | 1.6.1 (Hash = offizieller Upstream-Hash) | CMake mit `prospero-cmake`, Float, Laufzeit-CPU-Erkennung |
| ENet | `cgutman/enet` aca87840… (Submodul von moonlight-common-c) | eigene CMake-Feature-Checks |
| nanors | b1e3c22c… (Submodul von moonlight-common-c) | dieselben drei Quellen wie Sunshine |
| moonlight-common-c | 04053843… | nur Quellen/Lizenz bereitgestellt (RtspParser etc. später) |

Zwei SDK-Eigenheiten bei nanors, beide im Prelude-Header in `deps.sh` gelöst: Clangs `immintrin.h` blendet AVX2/AVX-512-Typen auf `__SCE__`-Zielen aus, wenn das Feature nicht global aktiv ist (Makro nur um dieses Include herum maskiert). `__builtin_cpu_supports` braucht compiler-rt, das das SDK nicht linkt; ersetzt durch eigene CPUID-/XCR0-Abfrage. **`-U__SCE__` global wäre falsch**, die Sysroot-Header (`sys/cdefs.h`, `x86/_types.h` …) hängen davon ab.

Live-Probe `src/deps_probe.cpp` (`out/deps_probe.elf`, 8 MB statisch) auf 13.00 bestanden, Exit-Code 0:

```text
deps tls handshake=1 version=TLSv1.3 cipher=TLS_AES_256_GCM_SHA384
deps openssl version="OpenSSL 3.5.9 29 Sep 2026" keygen_ms=53 tls=1 aes_gcm=1 aes_gcm_mib_s=2515
deps opus version="libopus 1.6.1" packets=400 rms=0.177 encode_us_per_packet=46
deps enet server=1 connected=1 received=1 elapsed_ms=10
deps nanors encode_rc=0 recovered=1 encode_us=56 simd_align=32
deps result=PASS openssl=1 opus=1 enet=1 nanors=1
```

Bedeutung: Zufallsquelle von OpenSSL funktioniert auf der Konsole; RSA-2048-Schlüssel und selbstsigniertes Zertifikat (wie fürs Pairing) entstehen; TLS 1.3 über BIO-Paar; AES-128-GCM mit ~2,5 GiB/s (Hardware-AES aktiv). Opus-Multistream 48 kHz Stereo, 5-ms-Pakete, 46 µs Encodezeit pro Paket. ENet-Verbindung auf Loopback-Port 47999. nanors wählt AVX2 (32) und rekonstruiert 10 von 30 verlorenen Shards. Wieder nur die bekannte `fdescfree/BUDGET_FD_FILE`-Warnung.

**Noch nicht gebaut:** Boost (Sunshine verlangt ≥ 1.89 mit filesystem, log, program_options, locale; Simple-Web-Server braucht Asio), Simple-Web-Server, nlohmann_json (Header-only). Vorschlag, noch nicht entschieden: curl, miniupnpc, FFmpeg und libdisplaydevice werden auf der PS5 nicht gebraucht (Video kommt fertig kodiert aus Avcap2). Ob Boost.Log/Locale portiert oder in den übernommenen Teilen ersetzt werden, gehört in die Architekturentscheidung. Sunshine-Code wurde noch nicht in das eigene Backend übernommen.

**Gelöst am 28.10.2026:** `std::jthread`, `std::stop_token`, `std::stop_callback` und `condition_variable_any::wait` mit Stop-Token funktionieren mit `-fexperimental-library`. `src/jthread_probe.cpp` baut damit ohne Warnungen, ist in `scripts/build.sh` aufgenommen und lief live auf 13.00: `jthread result=PASS iterations=46 callback=1 wait_interrupted=1 elapsed_ms=50`, Exit-Code 0 (wieder mit der bekannten `fdescfree/BUDGET_FD_FILE`-Warnung). Ohne das Flag scheitert der Build weiterhin. Für den Sunshine-Port also `-fexperimental-library` global setzen.

Historisch: Sunshine nutzt C++23 sowie `std::jthread` unter anderem in `stream.cpp` und `audio.cpp`. Der erste Compileversuch mit `std::jthread` scheiterte mit der Standardkonfiguration des SDK. Deshalb verwendet der bestandene Laufzeittest derzeit `std::thread`. Danach wurde in den SDK-Headern festgestellt, dass Stop-Token/jthread durch `_LIBCPP_HAS_NO_EXPERIMENTAL_STOP_TOKEN` gesperrt sind, solange der experimentelle Bibliotheksmodus fehlt. **Als nächstes `-fexperimental-library` beziehungsweise den passenden libc++-Modus inklusive Linken und Live-Test prüfen. Das ist noch nicht ausprobiert.** Nicht vorschnell behaupten, dass jthread grundsätzlich unmöglich sei.

Upstream bietet bereits `video::packet_raw_generic` neben dem FFmpeg-Pakettyp in `src/video.h`. Dieser Ansatzpunkt sollte für direkt aus Avcap2 kommende H.264-Pakete geprüft werden. Der Header selbst enthält weiterhin FFmpeg-/Plattform-Abhängigkeiten; ein fertiger Adapter existiert noch nicht.

## 4. Nächste Schritte in sinnvoller Reihenfolge

Die ausführliche Roadmap mit Arbeitspaketen (P0.x bis P7.x), Risiko-Spikes (R1 bis R6) und Abnahmen steht in `ROADMAP.md`. Die folgende Liste ist der ursprüngliche Kurzplan.

1. ~~Neue `out/capture.h264` mit ffprobe/FFmpeg vollständig prüfen~~ (erledigt, fehlerfrei). Offen: längeren Lauf und wiederholtes Open/Close testen; M1 ist mit einem einzelnen Zehn-Sekunden-Test noch nicht abgeschlossen.
2. ~~`std::jthread` prüfen~~ und ~~OpenSSL, Opus, ENet, nanors bauen und live prüfen~~ (erledigt). Offen für M0: Boost (Asio, ggf. log/locale/filesystem/program_options) und Simple-Web-Server bauen oder ersetzen.
3. ~~Upstream-Pins in eine Lockdatei~~ (`deps.lock`, Sunshine selbst noch ergänzen) und eine nachvollziehbare Architekturentscheidung überführen. Wiederverwendung von Pairing (`nvhttp.cpp`), RTSP und Streaming gegen die tatsächliche Abhängigkeitskette prüfen. Lizenztexte und Herkunft vor Übernahme erhalten.
4. Eine kleine öffentliche Host-C-Schnittstelle für Claudes App festlegen: Start, Stop, Status, Pairing-Freigabe und Fehler. Aktuell existiert nur die darunterliegende Capture-Schnittstelle. Keine Dummy-Funktion als fertigen Host ausgeben.
5. Erst danach echte Moonlight-Sitzung implementieren: Pairing, RTSP, Paketierung/FEC/Verschlüsselung, Eingaben, Audio und Synchronisation. Der vorhandene Python-MCP-Server bleibt ein Entwicklungswerkzeug und gehört nicht in den Medienpfad.

Aktuell wird keine Anwendung auf der PS5 durch diese Arbeit ausgeführt. Es laufen keine neuen Dauerstreams. Die vorhandene RemoteEye-Instanz blieb bestehen. Die letzten Probes haben ihre eigenen Ressourcen freigegeben.

## 5. Reproduzieren der Live-Prüfungen und Git-Stand

Es gibt einen bereits vorhandenen Hilfsclient `mcp_call.py` für echte MCP-stdio-Aufrufe. Er liegt nur lokal im Scratchpad einer früheren Arbeitssitzung am ps5-mcp-Repository und ist nicht Teil dieses Repositorys.

Aufruf mit `py -3.15 <Pfad-zum-Hilfsclient> <Toolname> '<JSON-Argumente>'`. Er startet `python -m ps5mcp` mit der Entwicklungs-IP. Für strikte direkte Screenshots vorher `PS5_SCREENSHOT_BACKEND=avcap` setzen. Beispiele für Toolargumente:

```json
{"max_width":960}
```

für `ps5_screenshot`, beziehungsweise:

```json
{"path":"<absoluter Projektpfad>/out/capture_probe.elf","log_seconds":13}
```

für `ps5_send_payload`.

Der Regressionstest im MCP-Repository ist:

```powershell
py -3.15 -m unittest discover -s server -p test_server.py -v
```

Es wurden **keine Commits, Pushes, Pull Requests oder Releases** erstellt. ProsperoSunshine wurde nicht als neues Git-Repository initialisiert. Die Änderungen in ps5-mcp liegen lokal im bestehenden Checkout; vorherige Änderungen an `docs/how-it-works.md` und der neue Avcap-Probe gehörten bereits zur laufenden Vorarbeit und wurden erhalten. Buildprodukte und Cache enthalten lokal verfügbare Dateien, sind aber nicht eingecheckt.
