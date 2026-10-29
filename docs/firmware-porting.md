# Firmware-Porting

Stand: 29. Oktober 2026. **Verifiziert ist ausschließlich Firmware 13.00.** Dieses Dokument beschreibt,
was am Host firmwareabhängig ist, wie die ResourceArbitrator-Funktionen zur Laufzeit gefunden werden und
was vor einer Freigabe weiterer Versionen auf einer echten Konsole geprüft werden muss. Es ist keine
Zusage, dass andere Versionen funktionieren.

## Was firmwareabhängig ist

| Bereich | Datei | Annahme aus 13.00 | Wie es gefunden wird |
| --- | --- | --- | --- |
| ResourceArbitrator: Register, Unregister, Request, List | `src/platform/ps5/ra_resolve.c`, `ra_nids.h` | Modulrelative Adressen `0xdb0`, `0xee0`, `0x10b0`, `0x11f0` | Laufzeit-Suche per NID, Abgleich mit Tabelle (siehe unten) |
| Avcap2-Funktionen | `capture.c` | Exportiert unter bekannten Namen | `kernel_dynlib_dlsym` per Name, unkritisch |
| Avcap2-Video-Konfiguration | `capture.c` `configure()` | 0x2E0 Bytes, Unterblock-Größen, `0x2A4 = 2` (Bildschirm inkl. Home-Menü), RA-Profil 180 bei `0x28` | Fest, auf 13.00 gemessen (`docs/re/avcap2-exports.md`) |
| Avcap2-Audio-Konfiguration | `capture.c` | 0x38 Bytes, `cfg+0x14 = 2` (Remote-Play-Mix), `cfg+0x1C = 180` | Fest, auf 13.00 gemessen |
| Frame-Descriptor | `video_ps5.cpp`, `docs/avcap2-descriptor.md` | 0x70 Bytes, TSC-Zeitstempel in Wort 9 | Fest, auf 13.00 gemessen |
| Bitrate | Host-Konfiguration | Encoder stoppt über 15 Mbit/s, Host klemmt darauf | Auf 13.00 gemessen |
| Virtuelles Pad | `pad.c`, `pad.h` | ioctls auf `/dev/hid`: add `0xC018482A`, insert `0x8018482C`, delete `0x80104850`; Blöcke 0xA8 Bytes, Layout im Kopfkommentar von `pad.c` | Fest, auf 13.00 gemessen; unverändert |
| Kernel-Zugriffe des SDK | PS5 Payload SDK v0.45 | `kernel_dynlib_*`, `kernel_get_fw_version` nutzen SDK-interne Kernel-Offsets je Firmware | Vom SDK abgedeckt, sofern es die Version kennt |

## ResourceArbitrator: Suche per NID

`libSceResourceArbitrator.sprx` exportiert die vier Funktionen, aber nur unter NIDs, deren
Klartextnamen nicht bekannt sind. Deshalb findet `kernel_dynlib_dlsym` (Name → NID) sie nicht. Früher
standen feste Adressen relativ zur Modulbasis im Code, abgesichert durch eine harte 13.00-Prüfung.

Jetzt löst `ps_ra_resolve()` die vier NIDs direkt mit `kernel_dynlib_resolve()` auf:

| Funktion | NID | 13.00 |
| --- | --- | --- |
| register `(id, callback, user)` | `kJvjeet+f9I` | `0xdb0` |
| unregister `(id)` | `LqmYZGB-UEQ` | `0xee0` |
| request `(id, state, data, pid)` | `CYjfezkWcaE` | `0x10b0` |
| list `(int32_t active[32])` | `fBX+DBOwNis` | `0x11f0` |

NIDs sind Hashes der Symbolnamen und bleiben gleich, solange Sony die Funktion nicht umbenennt; die
Adressen dagegen verschieben sich mit jedem Build des Moduls. Die Entscheidungsregel steht portabel in
`ps_ra_choose()` (`ra_nids.h`, Test `tests/ra_choose_test.cpp`):

- **Bekannte Firmware** (Eintrag in `ps_ra_known_firmware`): Das NID-Ergebnis muss der Tabelle
  entsprechen, sonst wird abgelehnt. Fehlt ein NID, gilt der Tabellenwert als Rückfall.
- **Unbekannte Firmware:** Alle vier NIDs müssen auflösen (innerhalb von 1 MiB hinter der Modulbasis),
  sonst lehnt der Host mit `PS_CAPTURE_FIRMWARE` ab. Gelingt es, meldet klog ausdrücklich, dass die
  Firmware unverifiziert ist.

Die Suche läuft einmal pro Host-Prozess beim ersten `ps_capture_open` und ruft nichts im Modul auf.
klog zeigt eine Zeile pro Funktion, z. B. auf 13.00:

```text
ProsperoSunshine: ra firmware=0x13000040 known=1
ProsperoSunshine: ra register   nid=kJvjeet+f9I -> 0xdb0 table=0xdb0 use=0xdb0 (nid)
```

Byte-Signaturen über den Modulcode wurden erwogen und verworfen: Der Code ist nur über den Kernel
lesbar (direktes Lesen beendet den Prozess, Ausführen-only-Speicher), die Muster wären Auszüge aus
Sony-Code, und die NIDs sind robuster.

### Prüf-Payload

`out/ra_nid_probe.elf` lädt nur `libSceIpmi` und `libSceResourceArbitrator`, löst die NIDs auf und
schreibt das Ergebnis nach klog. Es registriert sich nicht beim ResourceArbitrator, öffnet kein Avcap2
und fasst das Pad nicht an; es kann also neben einem laufenden Host gestartet werden.
`result=PASS` bedeutet: alle vier per NID gefunden und, bei bekannter Firmware, gleich der Tabelle.

Ergebnis auf 13.00 (29. Oktober 2026):

```text
ProsperoSunshine: ra_nid result=PASS rc=0 firmware=0x13000040 known=1 register=0xdb0 unregister=0xee0 request=0x10b0 list=0x11f0
```

## Checkliste für eine neue Firmware

Nur auf einer Konsole mit dieser Firmware, Schritt für Schritt, mit klog:

1. **SDK:** Läuft ein einfaches Payload (`runtime_probe.elf`)? Kennt das SDK die Kernel-Offsets der
   Version? Ohne das scheitern `kernel_dynlib_*` und damit alles Weitere.
2. **ResourceArbitrator:** `ra_nid_probe.elf` senden. Erwartet: vier aufgelöste Offsets, `rc=0`. Die
   Offsets in `ps_ra_known_firmware` eintragen (Schlüssel `Version & 0xffff0000`).
3. **Avcap2-Video:** `capture_probe.elf` bzw. `avcap_spike.elf`: `OpenVideo` muss mit der 0x2E0-Konfiguration
   0 liefern, das Bild muss decodierbar sein (`ffprobe`). Liefert `OpenVideo` einen Fehler, hat sich die
   Konfigurationsstruktur geändert: mit dem entschlüsselten `libSceAvcap2` neu vermessen
   (`docs/re/avcap2-exports.md`, Werkzeuge in `tools/re/`).
4. **Descriptor:** TSC-Zeitstempel in Wort 9 und IDR-Kennung prüfen (`capture_soak.elf`,
   `docs/avcap2-descriptor.md`).
5. **Audio:** `audio_spike.elf`: `OpenAudio` mit `cfg+0x1C = 180` muss PCM liefern.
6. **Bitrate:** `bitrate_spike.elf`: Grenze, ab der der Encoder stoppt (13.00: über 15 Mbit/s).
7. **Pad:** `pad_probe.elf` (fügt ein neutrales virtuelles Pad hinzu und entfernt es wieder; vorher
   klären, dass niemand die Konsole gerade bedient). Prüfen: ioctl-Nummern, 0xA8-Blockgröße,
   Feldlagen von Tasten, Sticks und Triggern, Zuordnungsdialog. Das Pad hat keinen eigenen
   Firmware-Schalter; sein Verhalten auf anderer Firmware ist ungeprüft.
8. **Ende-zu-Ende:** Moonlight-Stream mit Bild, Ton, Eingabe; `tools/moonlight/` Smoke-Test.
9. Erst danach: Version in `README.md` und `STATUS.md` als geprüft eintragen.

Solange Schritte 3 bis 8 für eine Version nicht belegt sind, gilt sie als nicht unterstützt, auch wenn
die NID-Suche gelingt.
