# ProsperoSunshine Umsetzungsplan

Stand: 28. Oktober 2026. Dieses Dokument beschreibt Ziel, technischen Ansatz und verbindliche Regeln. Die konkreten Arbeitspakete, ihre Reihenfolge und Abnahmen stehen in `ROADMAP.md`. Im Projektordner ist bislang kein Streaming-Host implementiert.

## Ziel

ProsperoSunshine soll direkt auf einer PS5 einen Sunshine-kompatiblen Streaming-Host bereitstellen. Ein unveränderter Moonlight-Client verbindet sich mit der PS5, empfängt Bild und Ton und überträgt Controller-Eingaben zurück. Ein PC darf für Entwicklung und Diagnose nötig sein, soll im späteren Streaming-Betrieb aber keine Vermittlerrolle übernehmen.

Der erste vollständige Funktionsumfang umfasst einen Client im lokalen Netzwerk, H.264 mit 1920 × 1080 in SDR, Stereo-Audio und einen Controller. Zuerst werden dauerhaft 30 FPS nachgewiesen; 60 FPS sind ein anschließendes Mess- und Optimierungsziel. Beides sind Entwicklungsziele, keine bereits gemessenen Eigenschaften des vorhandenen Probes. Manuelles Hinzufügen der PS5-IP in Moonlight reicht für den ersten Meilenstein.

HDR, HEVC, 4K, Internetzugriff, mehrere Clients, mehrere Controller, Mikrofon, Rumble und adaptive Trigger folgen erst nach dem stabilen Grundbetrieb.

## Gesicherter Ausgangspunkt

Die Vorarbeit liegt unter `../ps5-mcp` und wurde auf einer PS5 mit Firmware 13.00 geprüft.

| Bereich | Nachgewiesen | Noch offen |
| --- | --- | --- |
| Bildschirmaufnahme | Avcap2 liefert decodierbares H.264 mit 1920 × 1080, einschließlich sichtbarem Home-Menü und Homebrew. | Dauerbetrieb, tatsächliche Bildrate, Latenz, Verhalten in kommerziellen Spielen. |
| Schwarze Bilder | Die Quellenauswahl bei Konfigurationsoffset `0x2A4` wurde von `3` auf `2` geändert. Damit wird der zusammengesetzte Bildschirm erfasst. | Verhalten bei weiteren Ausgabe- und Spielsituationen. |
| Capture-Datei | Der letzte geprüfte Lauf lieferte 100 decodierbare Frames und 291.551 Bytes einschließlich SPS/PPS. | Das ist ein kurzer Funktionsnachweis, kein Leistungsnachweis. |
| Ressourcen | Profil 180 lässt sich registrieren, anfordern und sauber freigeben. | Dauerhafte Konfliktbehandlung und Wiederanlauf im Host. |
| Eingabe | RemoteEye kann über ein virtuelles Pad Tasten, Sticks und Trigger an die PS5 senden. | Übersetzung der Moonlight-Pakete und Absicherung bei Verbindungsabbruch. |
| Audio | Kein integrierter PS5-Audiostream nachgewiesen. | Capture-Schnittstelle, PCM-Format, Opus und Synchronisation. |
| Moonlight | Noch keine Verbindung mit ProsperoSunshine implementiert. | Pairing, Sitzungsaufbau, Video-/Audio-Transport und Steuerkanal. |

Der Avcap2-Probe ist eigenständig. Der bestehende MCP-Befehl für Screenshots verwendet weiterhin die Galerie; ein neuer produktiver MCP-Screenshot-Backend ist damit noch nicht fertiggestellt. Die zehn MCP-Selbstprüfungen bestanden nach den Capture-Versuchen.

Lokale Referenzen:

- Capture und vollständiges Konfigurationslayout: `../ps5-mcp/payload/avcap_probe/avcap_probe.c`
- Build: `../ps5-mcp/payload/avcap_probe/build.sh`
- Recherche: `../ps5-mcp/docs/how-it-works.md`
- Pad-Implementierung: `../ps5-mcp/payload/RemoteEye/Program.cs`
- Sichtprüfung: `../ps5-mcp/payload/avcap_probe/out/verified-screen.png`

## Technischer Ansatz

Sunshine ist bereits ein Host für Moonlight. Für ProsperoSunshine sollen möglichst dessen Protokollbausteine übernommen und um PS5-spezifische Aufnahme und Eingabe ergänzt werden. Die genaue Grenze zwischen Übernahme und Anpassung wird zuerst durch einen Build- und Abhängigkeitstest festgelegt. Die vorhandene FreeBSD-Unterstützung von Sunshine beweist keine Kompatibilität mit der PS5-Laufzeit. [Sunshine](https://docs.lizardbyte.dev/projects/sunshine/latest/)

Die native Anwendung besteht aus einem Sitzungsmanager, einem Protokollteil und einer kleinen PS5-Plattformschicht. Der vorhandene Python-MCP-Server dient beim Entwickeln zum Deployment und zur Diagnose. Er transportiert im fertigen Host keine Medien.

```text
PS5 Avcap2 → H.264-Frames → begrenzte Warteschlange → Video-Transport → Moonlight
PS5 Audio → PCM → Opus → Audio-Transport → Moonlight
PS5 virtuelles Pad ← Eingabeübersetzung ← Steuerkanal ← Moonlight
                        Sitzungsmanager und gemeinsame Zeitbasis
```

Avcap2 liefert bereits komprimiertes Video. Der Host soll diese Frames direkt verpacken. Ein erneutes Decodieren und Encodieren wäre zusätzliche Last und Latenz. Ob dafür Sunshine-Paketschnittstellen unverändert verwendbar sind und welche FFmpeg-Abhängigkeiten verbleiben, muss am gewählten Upstream-Stand geprüft werden. [Videoschnittstellen](https://github.com/LizardByte/Sunshine/blob/master/src/video.h)

### Wiederverwendung prüfen

| Upstream-Bereich | Aufgabe im Projekt |
| --- | --- |
| `nvhttp.cpp` | Hostinformationen, Pairing, Anwendungsliste und Sitzungsaktionen. |
| `rtsp.cpp` | Aushandlung der Sitzung und ihrer Medienparameter. |
| `stream.cpp` | Video-/Audio-Paketierung, Steuerkanal, Fehlerkorrektur und ausgehandelte Verschlüsselung. |
| `input.cpp` | Empfang und Auswertung von Eingaben; PS5-Ausgabe separat anbinden. |
| `audio.cpp` | Opus-Verarbeitung und Zuordnung zur Sitzung; PS5-Aufnahme ersetzen. |

Quellen: [Pairing und HTTP](https://github.com/LizardByte/Sunshine/blob/master/src/nvhttp.cpp), [RTSP](https://github.com/LizardByte/Sunshine/blob/master/src/rtsp.cpp), [Streaming](https://github.com/LizardByte/Sunshine/blob/master/src/stream.cpp), [Audio](https://github.com/LizardByte/Sunshine/blob/master/src/audio.cpp).

Vor dem ersten Port einen konkreten Sunshine-Commit und dessen Unterprojekte festhalten. Der derzeitige Master verlangt C++23; Compiler, Standardbibliothek und Thread-Unterstützung müssen deshalb auf dem PS5-Toolchain-Pfad praktisch geprüft werden. Ebenso früh die tatsächlich benötigten Teile von OpenSSL, Boost, ENet, Opus, Fehlerkorrektur und FFmpeg erfassen. Upstream kennzeichnet das Projekt als `GPL-3.0-only`; Lizenztexte, Herkunft und Änderungen bei übernommenem Code dokumentieren. [Builddefinition](https://github.com/LizardByte/Sunshine/blob/master/CMakeLists.txt)

## Verbindliche Regeln für das PS5-Backend

- Zunächst ausschließlich Firmware 13.00 unterstützen und vor Verwendung der gemessenen Moduloffsets prüfen. Andere Versionen kontrolliert ablehnen.
- Das funktionierende Avcap2-Layout mit `0x2E0` Bytes übernehmen. Ältere SharpProspero-Strukturen mit `0x2B8` Bytes passen nicht unverändert.
- `0x2A4 = 2` für den bisher bestätigten zusammengesetzten SDR-Bildschirm beibehalten. Wert `3` erfasste nur die App und führte im Home-Menü zu schwarzen Bildern.
- Profil-ID 180 ist eine ResourceArbitrator-Ressource, keine Benutzer-ID. Wenn sie bereits belegt ist, den Start ablehnen und die Belegung melden.
- Beim Anfordern auf die Freigabe warten, Zeitüberschreitung begrenzen und einen späteren Ressourcenentzug behandeln. Keine fremde Sitzung oder Systemprozesse beenden.
- Speicherverwaltung zunächst beim Dienst belassen. Externe Frame-Puffer sind für den vorhandenen Nachweis nicht erforderlich.
- Alle Erfolgs- und Fehlerpfade müssen eigene Channels stoppen und schließen, Avcap2 terminieren und die eigene Ressourcenregistrierung freigeben. Bei Teilinitialisierung nur tatsächlich erworbene Ressourcen aufräumen.
- Bei Disconnect oder Sitzungsfehler alle gedrückten Eingaben freigeben und das eigene virtuelle Pad sauber lösen.

Für `sceAvcap2ReadVideo` gilt im Probe: positiver Rückgabewert ist die kopierte Bytezahl, `0x81950002` bedeutet noch kein Frame. Der Descriptor ist `0x70` Bytes groß. SPS/PPS kommen zusätzlich über `sceAvcap2GetNonVclNalUnitStream`; im Diagnoseformat werden sie mit Annex-B-Startcodes vorangestellt. Weitere ABI-Details aus der lokalen C-Datei übernehmen, statt sie neu zu erraten.

## Umsetzung in Meilensteinen

### M0: Build und Abhängigkeiten nachweisen

Ein minimales natives Projekt mit reproduzierbarem Build und Logausgabe anlegen. Den vorhandenen SDK-Pfad als Ausgangspunkt nutzen; der Probe wurde mit PS5 Payload SDK v0.45 gebaut. C++23, Threads, monotone Uhr, Sockets und die benötigten Bibliotheken jeweils auf der Konsole erproben. Danach in einer kurzen Architekturentscheidung festhalten, welche Sunshine-Teile direkt gebaut und welche Schnittstellen ersetzt werden.

**Abnahme:** Ein ELF startet auf der PS5 und führt die benötigten Laufzeit- und Bibliotheksproben erfolgreich aus. Toolchain-Versionen und Upstream-Commits sind dokumentiert. Falls eine zentrale Abhängigkeit scheitert, ist die Alternative vor dem großen Protokollport entschieden.

### M1: Capture als dauerhafte Komponente

Den funktionierenden Probe in eine `CaptureSession` mit explizitem Start, Lesen und Stop zerlegen. Das bisherige kurze Aufnahmefenster durch einen abbrechbaren Dauerbetrieb ersetzen. Frame-Grenzen, Größen, SPS/PPS, Zeitstempel, erkennbare Keyframes und Fehler erfassen. Die Bedeutung der Capture-Zeitstempel und des Timing-Feldes separat untersuchen.

Bitrate, Bildrate und erzwungenes IDR-Bild sind eigene Prüfpunkte. Ein funktionierender SPS/PPS-Aufruf beweist keine Möglichkeit, einen neuen Keyframe anzufordern. Erforderliche Encoderfunktionen müssen gemessen und anschließend über eine klare Schnittstelle angeboten werden.

**Abnahme:** Mindestens zehn Minuten decodierbares Video aus Home-Menü und Homebrew, gemessene Frame-Abstände und Speicherverbrauch, fünf saubere Start-/Stop-Zyklen. Ein einfacher Diagnoseempfänger darf dazu dienen; dieser Zwischenstand ist noch keine Moonlight-Kompatibilität.

### M2: Pairing und Sitzungsaufbau

Hostidentität und Zertifikate dauerhaft speichern. Serverinformationen, Pairing und eine feste Anwendung „PS5-Bildschirm“ anbieten. Diese Anwendung bedeutet den aktuellen PS5-Bildschirm, nicht das Starten eines PC-Desktops. Launch, Resume und Cancel sowie die RTSP-Aushandlung implementieren. Nur tatsächlich unterstützte Medienmodi anbieten; zunächst einen gepairten Client gleichzeitig zulassen.

Die vorhandenen Upstream-Authentifizierungs- und Verschlüsselungsabläufe erhalten. Medien und Eingaben erst für eine autorisierte Sitzung freigeben. Nicht unterstützte Modi oder eine zweite Sitzung mit verständlichem Fehler ablehnen.

**Abnahme:** Ein unveränderter Moonlight-Client kann die PS5 manuell hinzufügen, pairen und die Bildschirm-Anwendung starten. Ein ungepairter Client kann keinen Stream öffnen. Ein Hostneustart erhält die autorisierte Clientidentität.

### M3: Video in Moonlight

Die Avcap2-Frames an den übernommenen Transport anbinden. Paketgrößen, Sequenzen, Zeitstempel, Frame-Markierungen, Fehlerkorrektur, Steuerkanal und ausgehandelte Verschlüsselung gemäß dem festgehaltenen Upstream-Stand behandeln. Start und Wiederaufnahme benötigen einen decodierbaren Einstieg mit Parameter-Sets und geeignetem Keyframe.

Die Warteschlange zeitlich und nach Speicher begrenzen. Bei Rückstau keine beliebigen Bytes oder einzelne Referenzteile entfernen: vollständige Frames verwerfen, die Abhängigkeiten berücksichtigen und nötigenfalls bis zu einem neuen Keyframe synchronisieren. Den Wiederherstellungsweg zunächst nachweisen und erst dann Überlastbehandlung darauf aufbauen.

**Abnahme:** Moonlight zeigt Home-Menü und Homebrew kontinuierlich. Verbindungsabbruch und erneuter Start liefern wieder ein Bild. Paketverlust und künstlicher Rückstau führen zu begrenzter Verzögerung oder einem kontrollierten Sitzungsende.

### M4: Controller anbinden

Die bewährten RemoteEye-HID-Strukturen als Referenz für eine native Pad-Komponente verwenden. Moonlight-Tasten, Sticks und Trigger übersetzen, Wertebereiche und Totzonen prüfen und PS-Taste beziehungsweise deren verfügbare Clientbelegung dokumentieren. Benutzerzuordnung ausdrücklich behandeln: Ein neuer virtueller Controller kann einen Zuordnungsdialog auslösen. Keine pauschale Bestätigung während eines laufenden Spiels senden.

Ein Watchdog neutralisiert das Pad nach ausbleibender Eingabe oder Verbindungsverlust. Gleichzeitige schreibende MCP-Eingaben während einer Streaming-Sitzung verhindern oder über dieselbe Eingabeinstanz koordinieren.

**Abnahme:** Menünavigation und ein Spiel sind mit einem Moonlight-Controller bedienbar. Tests mit gehaltenen Tasten und anschließendem Netzwerkausfall hinterlassen keine gedrückten Eingaben.

### M5: Audio und Synchronisation

Die PS5-Audioaufnahme zunächst unabhängig nachweisen: Schnittstelle, Zugriffsrechte, Sample-Format, Kanäle, Rate und Zeitstempel erfassen. Falls Audio ebenfalls Ressourcen benötigt, deren Zusammenspiel mit Video vor der Integration prüfen. Den Capture-Pfad anschließend an den vorhandenen Opus- und Audiotransport anbinden. Stereo bei 48 kHz ist das erste Ausgabeziel; Paketdauer passend zur Aushandlung behandeln.

Audio und Video auf eine gemeinsame monotone Zeitbasis beziehen. Drift über längere Sitzungen messen und die Korrektur festlegen. Ein stummer Stream kann M3 testen, erfüllt aber nicht den vollständigen Funktionsumfang.

**Abnahme:** Spielton erreicht Moonlight verständlich und ohne dauerhafte Aussetzer. Bei einem reproduzierbaren Bild-/Ton-Ereignis bleibt der Versatz während 30 Minuten stabil; Messverfahren und tolerierter Grenzwert sind vor dem Test festgelegt.

### M6: Stabilität und Bedienbarkeit

Alle Komponenten über einen gemeinsamen Sitzungszustand steuern: bereit, Aufbau, aktiv, Abbau und Fehler. Trennung, Clientabsturz, belegte Capture-Ressource, Ressourcenentzug und Teilfehler testen. Begrenzte Logs mit Sitzungs-ID, Bildrate, Bitrate, Warteschlangendauer, Paketverlust, Audiofehlern und Abbaugrund bereitstellen; keine Schlüssel oder vollständigen Pairing-Geheimnisse protokollieren.

**Abnahme:** Eine 30-Minuten-Sitzung mit Bild, Ton und Controller, zehn erfolgreiche Wiederverbindungen sowie fünf vollständige Host-Start-/Stop-Zyklen. Kein fortlaufender Speicheranstieg, keine hängen gebliebenen Eingaben und keine eigene Capture-Ressource nach dem Abbau. Anschließend denselben Ablauf mit einem zweiten Moonlight-Client auf einer anderen Plattform prüfen, nacheinander.

## Messungen und praktische Tests

1. Zunächst per LAN testen und Clientversion, Auflösung, angeforderte Bildrate und Bitrate festhalten.
2. Home-Menü, Homebrew und mindestens ein reguläres Spiel prüfen. Die bisherigen Capture-Ergebnisse belegen das Spielverhalten noch nicht.
3. Tatsächlich empfangene und dargestellte Frames messen. SPS-Metadaten allein sind kein Nachweis für eine stabile Bildrate.
4. Capture-, Queue- und Transportzeiten getrennt erfassen. Ende-zu-Ende-Latenz über eine geeignete gemeinsame Messung bestimmen, etwa eine schnelle Kamera mit lokalem und entferntem Bild; Zeitstempel verschiedener Geräte nicht ungeprüft voneinander abziehen.
5. Paketverlust, Bandbreitenbegrenzung, Kabelziehen und Clientabbruch gezielt auslösen. Wiederherstellung und Ressourcenfreigabe kontrollieren.
6. Erst nach bestandenem Grundbetrieb 60 FPS erproben und Engpässe anhand der Messwerte bearbeiten.

Die vorhandenen MCP-Werkzeuge unterstützen Deployment, Logs und Zustandsprüfungen. Galerie-Screenshots eignen sich zur Sichtkontrolle, nicht zur Streaming-Latenzmessung.

## Vorgeschlagene Projektstruktur

Die folgende Struktur ist ein Vorschlag für die Implementierung und noch nicht angelegt:

```text
src/
  main.cpp
  session/          Sitzungszustand, Konfiguration, Identität und Abbau
  protocol/         Adapter zu den übernommenen Sunshine-Bausteinen
  platform/ps5/     Avcap2, ResourceArbitrator, Audio und virtuelles Pad
third_party/        Festgehaltene Upstream-Stände beziehungsweise Build-Verweise
scripts/            Reproduzierbarer Build und Deployment
tests/              Paketfixtures, Fehlerpfade und Eingabeübersetzung
docs/               ABI-Erkenntnisse, Architekturentscheidung und Messprotokolle
```

Die Firmware-spezifischen Offsets in genau einer Plattformdatei bündeln. Protokollcode soll weder Moduloffsets noch HID-Layouts kennen. Tests besonders für Paketparser, Grenzwerte, Sitzungswechsel und Cleanup schreiben; Capture-Qualität und Konsolenressourcen zusätzlich auf echter Hardware prüfen.

## Erste konkrete Arbeitspakete

1. M0 durchführen und eine Entscheidung zum Umfang der Sunshine-Übernahme dokumentieren.
2. Die funktionierende Capture-Konfiguration unverändert in ein natives Daueraufnahme-Modul übernehmen und dessen Lebenszyklus absichern.
3. Parallel im technischen Ablauf, aber vor dem vollständigen Streaming-Port, die beiden größten offenen Plattformfragen klären: Keyframe-Anforderung und Audio-Capture. Scheitert einer dieser Punkte, den erreichbaren Zwischenstand und die Einschränkung ausdrücklich dokumentieren.
4. Danach Pairing und Video bis zum ersten echten Moonlight-Bild umsetzen, Controller und Audio anbinden und die Abnahmetests abschließen.

Der erste vollständige Stand ist erst erreicht, wenn ein unveränderter Moonlight-Client die PS5 selbstständig pairen und einen Stream mit Bild, Ton und Controller öffnen kann, der ohne PC-Vermittler läuft und nach Verbindungsende alle eigenen Ressourcen freigibt.
