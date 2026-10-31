# HEVC über Avcap2 (P9.7)

Stand 2026-10-31, Firmware 13.00. `src/hevc_spike.cpp` ist der Spike; die Messwerte stehen in `STATUS.md`.

## Ergebnis

Avcap2 liefert HEVC Main (8 Bit, 4:2:0, Level 5.0, BT.709, nur I- und P-Bilder) in 1080p60. Der Host nutzt es noch nicht.

## Was nötig war

Nur `cfg+0x24 = 1` reicht nicht: `sceAvcap2OpenVideo` meldet 0, `sceAvcap2Start` dann `0x80c78000`. `ps_capture_open`
macht für HEVC zusätzlich drei Dinge wie Remote Play (`SceRemotePlay 0x6f7f0`, Bitraten `0x70130`):

1. **Encoder-Speicher reservieren** (Memory Mode 1): `cfg+0x198 = 0x50`, `cfg+0x1a0 = 1` und vor dem Öffnen der Import
   `yN0w4tWivqs(in*0x90, out*0x20, out*0x20)`. Die Eingabe enthält die Größe `0x90`, ab `+8` eine Kopie von `cfg+8..+0x30`,
   ab `+0x30` eine Kopie von `cfg+0x30..+0x88`. Die beiden Ausgaben landen in `cfg+0x1a8` und `cfg+0x1c8` (je Größenfeld,
   `0x3c`, Länge, Adresse; gemessen `0x3e8000` und `0x1fec000` Byte). Freigabe: `96UQhNnBlxc` nach `Close`, vor `Terminate`.
2. **Bitrate in die Konfiguration:** `cfg+0xc8` Mittel, `+0xcc` Spitze, `+0xd0` Bit je Bild, `+0x10c` Byte je Bild
   (Spitze × 1001 / (fps × 1000)).
3. **Die übrigen Felder von Remote Play**, siehe `configure()`. Das ist nicht die kleinste nötige Menge. Bekannt ist nur,
   dass der Start ohne diese Felder scheitert, auch mit reserviertem Speicher: Die Kombinationen „nur HEVC-Felder“ und
   „alle Felder außer den Quantisierer- und Endwerten“ (`+0xd4`, `+0x220..`, `+0x2b8..`) ergaben beide `0x80c78000`.

Das Ressourcen-Profil ist nicht die Ursache: 175, 177, 180, 182 und 185 scheiterten ohne die Punkte 1–3 gleich.

## Lesen und Header

* Lesen geht nur mit `sceAvcap2ReadVideo2`; `sceAvcap2ReadVideo` ist AVC-only. Im Descriptor (0x88 Byte) gemessen: `+0x2c`
  Bildtyp (1 = IDR, 3 = P), `+0x30` Bildnummer ab 1, `+0x38`/`+0x40` Sekunden/Nanosekunden, `+0x4c`/`+0x50` Breite/Höhe,
  `+0x6c` Bytes des Bildes.
* `sceAvcap2GetNonVclNalUnitStream` liefert die Parameter in der Reihenfolge **SPS, PPS, VPS** (53, 11 und 28 Byte,
  inklusive Startcode). `ffmpeg` decodiert erst, wenn das VPS vorn steht. Der Host muss sie also umsortieren.
* IDR-Bilder (NAL-Typ 20) tragen keine Parameter-Sets. Eine `RequestKeyFrame`-Anforderung ergab teils zwei IDR hintereinander.

## Grenzen

* **Bitrate nur senken:** `SetVideoBitrate` über den Wert beim Öffnen hinaus tötet den Kanal, das nächste `ReadVideo2`
  liefert `0x80c78000`. Gemessen: 20 → 40 Mbit/s bricht ab; 30 → 10 → 30 Mbit/s läuft durch.
* **1440p und 4K:** Schon die Speicher-Reservierung scheitert mit `0x81950003`. Mit H.264 scheitert das Öffnen dort ebenfalls.
* **Unter Last nicht gemessen:** Alle Läufe zeigten den Home-Bildschirm mit wenig Bewegung (3 s bis 1,3 MB). Dass 100 Mbit/s
  öffnen, sagt nichts darüber, ob der Encoder sie in einem Spiel hält.

## `AvActivation`

`libSceVenc` ruft beim Anlegen eines HEVC-Encoders (`0xa680`) `AvActivation` mit dem Typ `HwHevcEncode` (`0x10001`) und einer
Titel-ID auf, die aus der Voreinstellung folgt (`0xbac0`; `cfg+0x14 = 2` ergibt `NPXS40102`, Remote Play). Das Ergebnis wertet
die Funktion nicht aus (`0xba07`). Es ist also keine Sperre, nur eine Meldung. Ob es Folgen außerhalb der Konsole hat, ist
nicht bekannt.
