# HEVC über Avcap2 (P9.7)

Stand 2026-10-31, Firmware 13.00. `src/hevc_spike.cpp` ist der Spike, der Host nutzt den Weg über `video_ps5.cpp`; die Messwerte stehen in `STATUS.md`.

## Ergebnis

Avcap2 liefert HEVC Main (8 Bit, 4:2:0, Level 5.0, BT.709, nur I- und P-Bilder) in 1080p mit 30 und 60 fps. Der Host bietet es auf Firmware 13.00 an (`ps_capture_hevc_verified`), auf anderen nur H.264.

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
  `+0x58` TSC-Stempel (beim alten `ReadVideo` `+0x48`), `+0x6c` Bytes des Bildes.
* `sceAvcap2GetNonVclNalUnitStream` liefert die Parameter in der Reihenfolge **SPS, PPS, VPS** (53, 11 und 28 Byte,
  inklusive Startcode). `ffmpeg` decodiert erst, wenn das VPS vorn steht, und `moonlight-common-c` erkennt daran den
  Anfang eines IDR. `ps_capture_headers` sortiert sie um.
* IDR-Bilder (NAL-Typ 20) tragen keine Parameter-Sets. Eine `RequestKeyFrame`-Anforderung ergab teils zwei IDR hintereinander.

## Grenzen

* **Bitrate nur senken:** `SetVideoBitrate` über den Wert beim Öffnen hinaus tötet den Kanal, das nächste `ReadVideo2`
  liefert `0x80c78000`. Gemessen: 20 → 40 Mbit/s bricht ab; 30 → 10 → 30 Mbit/s läuft durch.
* **1440p und 4K:** Schon die Speicher-Reservierung scheitert mit `0x81950003`, auf allen getesteten RA-Profilen (180, 177, 185, 175, 182).
  H.264 öffnet dort mit den Profilen 177 und 182, nicht mit denen für 2K (`docs/re/avcap2-exports.md`, Abschnitt 8.1).
* **Obergrenze bei extremem Bild:** H.264 hält die Bitrate bei Rauschen über die ganze Fläche nicht ein (1080p 35–39, 4K rund 132 Mbit/s, unabhängig
  von der Vorgabe); bei Rauschen auf einem Viertel der Fläche folgt sie der Vorgabe. HEVC ist so nicht gemessen.
* **Last nur begrenzt gemessen:** Getestet mit dem Home-Bildschirm und der Demo von Mario 64. Mario erzeugte bei 100 Mbit/s
  nur rund 33 Mbit/s. Ob der Encoder 100 Mbit/s in einem aufwendigen Spiel hält, ist offen; der Host klemmt daher auf 50 Mbit/s.

## Schlüsselbild-Abstand und ausgelassene Bilder

`cfg+0x94` ist der Abstand periodischer IDR in Bildern. Gemessen mit einem Spike, der das Feld überschrieb (je Kanal 8 oder 45 s
Home-Bildschirm, in der Mitte ein `RequestKeyFrame`): 120 ergibt 120, 600 ergibt 600. 30 ergibt 60, also den bisherigen Takt von
einer Sekunde. Bei 0 und 3600 kam im Messfenster kein periodisches IDR. `RequestKeyFrame` wird bei jedem Wert nach 1–3 Bildern bedient.

Der HEVC-Encoder lässt nach einem IDR Bilder aus, um dessen Bytes auszugleichen; die Bildnummern laufen dabei lückenlos weiter.
Mit dem alten Abstand von einer Sekunde, gemessen über die Zeitstempel am Client: bei 15 und 20 Mbit/s ein Bild je IDR (Abstand
33 ms, 59 statt 60 fps), bei 5 Mbit/s fünf Bilder (100 ms, 54 fps). H.264 lässt keine aus. Das Überschreiben von `cfg+0x10c`
(Byte je Bild) mit dem Vierfachen beseitigte die Lücken im selben Spike, auch bei 5 Mbit/s, ebenso `cfg+0xd4 = 51` bei 15, nicht
bei 5 Mbit/s; beide bleiben ungenutzt, weil die Wirkung auf Bilder in Bewegung nicht gemessen ist.

Der Host setzt deshalb `cfg+0x94` auf zehn Sekunden (`fps * 10`), für beide Codecs, und fordert zu Beginn des Streams ein IDR an, weil
der Tonkanal den Kanal schon früher geöffnet haben kann. Clients fordern nach Verlust ohnehin selbst eines an.

## H.264

Das Öffnen mit der Bitrate in `cfg+0xc8/0xcc/0xd0` hilft auch H.264: 20, 30 und 50 Mbit/s liefen im Spike ohne Fehler, während
`SetVideoBitrate` ab 16 Mbit/s den Encoder stoppte. Für H.264 reichen diese drei Felder, Speicher-Reservierung und die übrigen
Felder braucht es nicht. Der Host öffnet daher beide Codecs so und ruft `SetVideoBitrate` nicht mehr auf.

## `AvActivation`

`libSceVenc` ruft beim Anlegen eines HEVC-Encoders (`0xa680`) `AvActivation` mit dem Typ `HwHevcEncode` (`0x10001`) und einer
Titel-ID auf, die aus der Voreinstellung folgt (`0xbac0`; `cfg+0x14 = 2` ergibt `NPXS40102`, Remote Play). Das Ergebnis wertet
die Funktion nicht aus (`0xba07`). Es ist also keine Sperre, nur eine Meldung. Ob es Folgen außerhalb der Konsole hat, ist
nicht bekannt.
