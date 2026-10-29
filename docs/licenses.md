# Lizenzen und Herkunft (P7.2)

Stand: 28. Oktober 2026. ProsperoSunshine steht unter GPL-3.0-only (`LICENSE`), wie Sunshine. Alle Fremdquellen sind mit SHA256 in `deps.lock` festgehalten; `scripts/deps.sh` legt ihre Lizenztexte beim Build in `out/deps/licenses/` ab, und der CI-Workflow packt sie zusammen mit den ELFs ins Artefakt.

| Komponente | Version / Pin | Lizenz | Einbindung | Mit GPL-3.0 vereinbar |
| --- | --- | --- | --- | --- |
| Sunshine | `a679b059` + 4 Patches (`patches/sunshine/`) | GPL-3.0 | Quellen kompiliert, Teile ersetzt | ja (gleiche Lizenz) |
| moonlight-common-c | `04053843` | GPL-3.0 | `RtspParser.c`, Header | ja |
| ENet (Moonlight-Fork) | `aca87840` | MIT | statisch | ja |
| nanors | `b1e3c22c` | MIT | statisch | ja |
| Simple-Web-Server | `546895a9` | MIT | Header | ja |
| nlohmann_json | 3.11.3 | MIT | Header | ja |
| libdisplaydevice (nur `common`) | `a2afc043` | MIT | statisch | ja |
| Boost | 1.92.0 + 1 Patch (`patches/boost/`) | Boost Software License 1.0 | statisch | ja |
| OpenSSL | 3.5.9 | Apache-2.0 | statisch | ja, für GPL-3.0 (nicht für GPL-2.0-only) |
| Opus | 1.6.1 | BSD-3-Clause | statisch | ja |
| FFmpeg (avcodec, avutil, swscale; ohne `--enable-gpl`) | 9.0.2 + 1 Patch (`patches/ffmpeg/`, aus LizardByte/build-deps, MIT) | LGPL-2.1-or-later | statisch | ja; als Teil eines GPL-Werks mit vollständigem Quelltext verteilt |
| PS5 Payload SDK | v0.45 (Hash im `scripts/Dockerfile`) | Werkzeuge GPL-3.0; Laufzeit aus FreeBSD-libc (BSD) und LLVM libc++/libc++abi/libunwind (Apache-2.0 mit LLVM-Ausnahme) | statisch gelinkte Laufzeit | ja |

## Pflichten bei Veröffentlichung eines Releases

- Quelltext des Releases vollständig verfügbar halten (Repository-Tag), inklusive `deps.lock`, `patches/` und Build-Skripten, damit die statisch gelinkten Bibliotheken nachgebaut werden können (GPL-3.0 §6, LGPL-2.1 §6).
- Lizenztexte aus `out/deps/licenses/` und `LICENSE` dem Release beilegen (macht der CI-Workflow).
- Änderungen an Fremdcode nur als Patches mit Herkunft und Begründung (bereits so umgesetzt).

## Nicht enthalten

- Keine Sony-Binärdateien oder -Daten. Die entschlüsselten Systemmodule für die Analyse liegen ausschließlich im ignorierten `.cache/re`; `docs/re/` beschreibt Offsets und Verhalten ohne Code-Auszüge, `tools/re/` liest die Dateien nur lokal.
- Keine Bilder oder Assets von Sunshine (die Web-UI wird nicht gebaut).

## Offene Punkte aus der Lizenzprüfung (WIP, P7.2 nicht abgeschlossen)

- **Lizenztext der SDK-Laufzeit fehlt im Release-Artefakt.** `scripts/deps/*.sh` legen für elf Komponenten (boost, enet, ffmpeg, libdisplaydevice, moonlight-common-c, nanors, nlohmann-json, openssl, opus, simple-web-server, sunshine) einen Text in `out/deps/licenses/` ab (am gebauten Verzeichnis geprüft). Für das PS5 Payload SDK und seine statisch gelinkte Laufzeit (FreeBSD-libc, LLVM libc++/libc++abi/libunwind, compiler-rt-Builtins) gibt es keinen Text; der CI-Schritt „Collect licences“ bündelt nur diese Dateien plus `LICENSE`. Die BSD-Lizenz der libc verlangt den Copyright-Hinweis bei Binärverteilung. Das Release muss die Lizenz- und Copyright-Texte aus `ps5-payload-sdk-v0.45.zip` (Hash im `scripts/Dockerfile`) beilegen. Noch nicht gegen den Inhalt des SDK-Archivs verifiziert.
- Noch zu erledigen: NOTICE/THIRD_PARTY-Liste für das Release-Archiv, Patch-Liste mit Herkunft je Datei, Sunshine-Unterverzeichnis `third-party` auf weitere eingebundene Lizenzen prüfen, englische README (P7.3).
