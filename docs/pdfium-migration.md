# Migrationsplan: ein PDF-Backend (PDFium)

Stand: 2026-08-19.

## Ausgangslage

Die beiden Builds laufen auf verschiedenen Engines — nicht aus Absicht, sondern
weil `CMakeLists.txt` nimmt, was es findet: `Qt6::Pdf`, sonst Poppler. Einen
Schalter gibt es nicht.

| | Backend | qpdf |
| --- | --- | --- |
| `build/` (Linux) | `HAVE_QT_PDF` | ja |
| `build-win/` (MinGW) | `HAVE_POPPLER` | **nein** |

Ursache: **`mingw64-qt6-qtpdf` existiert nicht.** Der Installationshinweis in
`build-win.sh:6-7` nennt ein Paket, das es in Fedora nie gab. Deshalb ist der
Windows-Build auf den Poppler-Fallback gerutscht, und niemand hat es gemerkt.

Was dadurch im Windows-Build stumm fehlt:

- **PDF-Export mit Optionen komplett** — `pdfExportAvailable()` hängt an
  `HAVE_QPDF` und ist dort `false`
- **Organizer speichert rasternd** (150 dpi statt vektoriell)
- **Speichern rastert das ganze Dokument** (`writePopplerRaster`, 300 dpi)
- **Präsentationsmodus zeigt Schwarz** — `PresentationWindow` ist Qt6::Pdf-only
- **DOCX-Export ohne dekodierten Text** — `decodedTextItems` ist Qt6::Pdf-only

Und: die Kombination Poppler + qpdf **kompiliert heute nicht**. `saveVector`
steht unter `#ifdef HAVE_QPDF`, nimmt aber ein `QPdfDocument*`, das nur unter
`HAVE_QT_PDF` deklariert wird. Nachgestellt mit dem Compiler:

```
EditSession.hpp:211:21: error: 'QPdfDocument' has not been declared
```

## Ziel

Ein Backend: **PDFium**, auf beiden Plattformen dieselbe Engine in derselben
Version. qpdf bleibt für das, was PDFium nicht kann — Export-Optionen und
Verschlüsselung. Qt6::Pdf und Poppler verschwinden samt aller `#ifdef`-Paare.

Lizenzseitig fällt damit das einzige Copyleft-Bauteil aus dem PDF-Stack:
PDFium ist BSD-3-Clause, qpdf Apache-2.0. Es gibt danach nur noch einen Build,
und der ist kommerziell verteilbar — die Poppler-Fallunterscheidung in
`package-win.sh` entfällt ersatzlos.

## Grundprinzip

Jeder Schritt ist ein Commit mit grünem Build auf **beiden** Plattformen. Kein
Schritt darf das Programm in einen Zustand bringen, in dem eine Funktion fehlt
— außer Schritt 10, der nur noch Totes entfernt.

Schritte 0–3 ändern kein Verhalten. Ab Schritt 4 ändert sich Gerendertes, und
genau dafür existiert Schritt 0.

---

## Schritt 0 — Sicherheitsnetz

Ohne Vorher/Nachher-Vergleich ist ein Engine-Tausch Blindflug. Das Werkzeug ist
da — `--shot-window`, `--export-pdf`, `--export-docx`, `tests/shotcmp.py` —,
was fehlt, ist der Korpus.

**Zuerst ein Widerspruch auflösen:** CLAUDE.md sagt „Test fixtures are versioned
under `tests/`; do not put them in ignored directories". `.gitignore:22`
ignoriert aber genau `tests/`. Damit überlebt kein Fixture und keine Baseline
einen frischen Clone. Solange das so ist, hat dieser Plan kein Netz.

Korpus, 10–15 Dokumente, jedes für ein Problem:

- CID-Fonts / CJK — der Fall, für den Poppler `share/poppler` braucht
- Subset-Fonts mit eigener Kodierung — der Grund, warum `decodedTextItems` existiert
- AcroForm mit ausgefüllten Feldern
- verschlüsselt (User-Passwort)
- Scan ohne Textlayer (OCR-Pfad)
- gemischte Seitengrößen inkl. Querformat
- Annotationen und Links
- vektorgrafiklastig (Diagramme, dünne Linien)
- defekt/abgeschnitten (Fehlerpfad)

Ergebnis: `tests/parity.sh` fährt Korpus × {Render-Shot, `--export-pdf`,
`--export-docx`} und legt Baselines ab — einmal mit dem heutigen Linux-Build
(Qt6::Pdf), einmal mit dem heutigen Windows-Build (Poppler) unter Wine.

Aufwand: ~1 Tag. **Ohne diesen Schritt nicht weitermachen.**

## Schritt 1 — qpdf für MinGW cross-bauen

Unabhängig von PDFium und mit dem besten Verhältnis von Aufwand zu Ertrag: es
schaltet im Windows-Build zwei komplette Funktionen frei, die heute stumm
fehlen.

- qpdf cross-bauen (normales CMake-Projekt, braucht zlib und libjpeg aus dem
  mingw-Sysroot)
- `libqpdf*.dll` in die Deploy-Liste in `build-win.sh`
- falschen Paket-Hinweis in `build-win.sh:6-7` korrigieren

**Achtung:** damit ist im Windows-Build erstmals `HAVE_POPPLER` *und*
`HAVE_QPDF` gesetzt — die Kombination, die nicht kompiliert. Die Save-Signaturen
müssen in diesem Schritt von `QPdfDocument` entkoppelt werden. Das ist die
einzige Stelle, an der der Plan Altcode anfasst, bevor PDFium überhaupt da ist.

Verifikation: `wine build-win/OpenPDFStudio.exe --export-pdf fixture.pdf out.pdf`
liefert eine Datei statt Exit 3; Organizer-Save ist vektoriell.

Aufwand: 1–2 Tage, der meiste davon im Cross-Build.

## Schritt 2 — PDFium beschaffen und einhängen

- Prebuilt **ohne V8** — Formular-JavaScript braucht ihr nicht, und die
  Variante ist ein Vielfaches kleiner
- Version pinnen, Prüfsumme ins Repo, Artefakt selbst archivieren (nicht auf
  einen fremden Release-Link verlassen)
- Windows: DLL + Import-Lib. Ist das Prebuilt MSVC-ABI, Import-Lib mit `gendef`
  + `dlltool` aus der DLL erzeugen — das geht, weil PDFiums öffentliche API
  reines C ist
- Linux: `.so` mitliefern
- CMake: `HAVE_PDFIUM` + imported target für beide Toolchains

Kein Code benutzt es. Build bleibt grün, Binary unverändert.

Aufwand: ~1 Tag, plus Klärung der Prebuilt-Frage.

## Schritt 3 — Backend-Interface einziehen

`engine/render/PdfBackend.hpp` — ein Interface über genau das, was `ui/` heute
per `#ifdef` mal von Qt und mal von Poppler holt:

```
öffnen(pfad, passwort) → dokument
seitenzahl(), seitengrößePt(seite)
rendern(seite, skalierung) → QImage
zeichenboxen(seite), textInBereich(seite, rect), zeilen(seite)
```

Die beiden vorhandenen Implementierungen ziehen darunter, ohne dass sich
Verhalten ändert. Das entfernt die ifdef-Paare aus `ui/` und ist die
Voraussetzung dafür, PDFium als dritte Implementierung im **selben Binary**
gegen die alte laufen zu lassen — die A/B-Messung in Schritt 4.

Betrifft: `DocumentSource`, `PdfRenderer`, `TextSelectionController`,
`EditController`, `DocumentExporter`, `PresentationWindow`,
`PdfOrganizerDialog`.

Aufwand: 3–4 Tage. Kein Verhaltensrisiko, aber breit.

## Schritt 4 — PdfiumBackend: öffnen, Seitengrößen, rendern

`FPDF_LoadDocument` (plus `FPDF_GetLastError() == FPDF_ERR_PASSWORD` für den
Passwortdialog), `FPDF_GetPageSizeByIndexF`, `FPDF_RenderPageBitmap` in einen
`FPDFBitmap_BGRA`-Puffer — vorher weiß füllen, dann stellt sich die
Premultiplied-Frage gar nicht.

A/B über `OPENPDF_BACKEND=pdfium|legacy` gegen den Korpus aus Schritt 0.
Erwartung: Linux nahezu deckungsgleich (Qt6::Pdf *ist* PDFium), Windows sichtbar
anders — das ist das Ziel, aber die Abweichungen einmal ansehen, nicht nur die
Prozentzahl von `shotcmp.py` abnicken.

Aufwand: 2–3 Tage.

## Schritt 5 — Text: Extraktion und Auswahl

`FPDFText_LoadPage`, `FPDFText_CountChars`, `FPDFText_GetCharBox`,
`FPDFText_CountRects`/`GetRect`, `FPDFText_GetText`.

Ersetzt zwei Dinge auf einmal: Qts `getAllText`/`getSelection` (die genau darauf
aufsetzen) und die Poppler-Wortlisten-Rekonstruktion in
`TextSelectionController` — eine Näherung, die damit ersatzlos entfällt.

Verifikation: Auswahl-Rechtecke und kopierter Text über den Korpus, besonders
das Subset-Font-Fixture.

Aufwand: 3–4 Tage.

## Schritt 6 — Content-Modell auf PDFiums Objekt-API

`FPDFPage_CountObjects`/`GetObject`, `FPDFPageObj_GetType`/`GetBounds`/
`GetFillColor`, `FPDFTextObj_GetFontSize`, `FPDFAnnot_*` für Annotationen und
Widgets. Ersetzt `QpdfContentProvider` **und** `PopplerContentProvider`.

Verifikation: `--export-docx` gegen Baseline, Hover-Highlight-Shots.

Aufwand: 4–5 Tage. Die meiste Detailarbeit, weil hier die Erkennungsheuristiken
hängen.

## Schritt 7 — Speichern auf Objektebene

Betroffenes Textobjekt suchen, `FPDFPage_RemoveObject`, neues über
`FPDFPageObj_CreateTextObj` + `FPDFText_SetText` einsetzen,
`FPDFPage_GenerateContent`, `FPDF_SaveWithVersion`. Bildbearbeitungen werden
`FPDFPageObj_NewImageObj` + `FPDFImageObj_SetBitmap`.

Was verschwindet: der Content-Stream-Tokenizer (~880 Zeilen), `verifyVectorSave`
samt Overlay-Fallback, `writePopplerRaster`, und der Raster-Fallback, der heute
das **ganze** Dokument rastert, sobald eine einzige Bildbearbeitung existiert.

**Verschlüsselung:** PDFiums API kann sie lesen, nicht schreiben. War die Quelle
verschlüsselt, läuft danach qpdf über das Ergebnis und legt sie wieder an —
dieselbe Fähigkeit, die `PdfExportOptions::userPassword` schon nutzt. Ohne
diesen Nachlauf verliert ein geschütztes Dokument beim Speichern stillschweigend
seinen Schutz. Hier darf der Plan nicht abkürzen.

Verifikation: Öffnen → Bearbeiten → Speichern → Öffnen über den Korpus; das
verschlüsselte Fixture behält sein Passwort; Text bleibt selektierbar.

Aufwand: 5–7 Tage. Der Kern des Umbaus.

## Schritt 8 — Organizer

`FPDF_CreateNewDocument` + `FPDF_ImportPagesByIndex` + `FPDFPage_SetRotation`.
Der 150-dpi-Raster-Fallback entfällt.

Der Organizer trägt schon heute keine Verschlüsselung mit (`out.emptyPDF()`
baut ein neues Dokument). Das bleibt so — oder ihr hängt denselben
qpdf-Nachlauf wie in Schritt 7 an. Bewusst entscheiden, nicht nebenbei.

Aufwand: ~2 Tage.

## Schritt 9 — Präsentationsmodus

`PresentationWindow` auf das Backend statt auf `QPdfDocument`. Behebt nebenbei
den schwarzen Bildschirm im heutigen Windows-Build.

Aufwand: ~2 Stunden.

## Schritt 10 — Abriss

Erst hier fliegt etwas raus: Qt6::Pdf und Poppler aus `CMakeLists.txt`,
`PdfTextExtractor.*`, `PopplerTextLookup.*`, die Poppler-Zweige in
`DocumentSource`/`ContentModel`/`TextSelectionController`, sämtliche
`HAVE_QT_PDF`- und `HAVE_POPPLER`-Paare.

Die Deploy-Liste in `build-win.sh` schrumpft um 13 DLLs — `libpoppler-156`,
`libpoppler-qt6-3`, `lcms2`, `openjp2`, `tiff` und der ganze curl-Rattenschwanz
mit openssl, ssh2, idn2, psl, unistring. `share/poppler` entfällt ebenfalls,
PDFium bringt seine CMap-Daten mit.

Aufwand: ~1 Tag, überwiegend Löschen.

## Schritt 11 — Lizenz und Packaging nachziehen

- `LICENSES/PDFIUM.txt` — die Notice-Sammlung des **konkreten** Prebuilds, nicht
  eine generische BSD-Datei. PDFium vendort FreeType, libjpeg-turbo, lcms2,
  OpenJPEG, zlib/libpng
- `LICENSES/README.md`: Poppler-Zeile raus, PDFium rein; der Abschnitt
  „The Poppler backend is GPL-only" entfällt
- `GPL_ONLY`-Verzweigung in `package-win.sh` entfällt — es gibt nur noch einen
  Build, und der ist kommerziell verteilbar
- CPack: `libqt6pdf6` aus den Recommends; PDFium-`.so` nach
  `/usr/lib/openpdf-studio/` mit RPATH, da es dafür kein System-Paket gibt —
  weder in Fedora noch in Debian

Aufwand: ~1 Tag.

---

## Zeitrahmen

Grob 25–30 Arbeitstage, davon der Kern (Schritte 4–7) etwa die Hälfte. Schritte
0 und 1 lohnen sich auch dann, wenn der Rest nie kommt.

## Zwei Entscheidungen vor Schritt 2

1. **Prebuilt oder eigener Build.** Ein fremdes Binary in einem kommerziell
   ausgelieferten Produkt ist eine Lieferketten-Entscheidung. Prebuilt ist
   pragmatisch — dann aber gepinnt, geprüft, selbst archiviert, und im Wissen,
   dass ein eigener Build ein Chromium-Build wäre, falls je ein Patch nötig wird.
2. **Verschlüsselung beim Organizer-Save** — mitziehen oder wie heute
   fallenlassen.

## Was sich trotz allem ändert

Der Plan hält die Funktion gleich, nicht die Bytes. Ehrlich benannt:

1. **Windows rendert sichtbar anders.** Andere Kantenglättung, andere
   Font-Substitution bei fehlenden Schriften. Im Sinne von „gleich wie Linux"
   ist das das Ziel — aber Referenzbilder verschieben sich.
2. **Unbearbeitete Seiten sind nicht mehr byte-identisch.** qpdf kopiert sie
   heute unangetastet durch; PDFium schreibt die Dokumentstruktur neu. Vektor
   bleibt Vektor, Text bleibt selektierbar, aber die Datei ist nicht mehr
   Original plus Delta.

## Bewusst nicht in diesem Plan

- **MSVC-Build und CI.** Der Plan hält am Cross-Compile von Fedora fest. PDFium
  verbaut einen späteren MSVC-Weg nicht, im Gegenteil.
- **PDFium selbst bauen.**
- **Die Heuristiken der Edit-Session.** Schritt 7 tauscht die Schreibebene, nicht
  die Frage, was ersetzt wird.
- **`modules/rich-media/`.** Wird durch die neue Lizenzlage erst möglich, gehört
  aber nicht in diesen Umbau.
