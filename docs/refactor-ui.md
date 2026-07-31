# Refactoring-Plan: `src/ui/`

> **Status: Schritte 0–4 umgesetzt** (2026-08-01, Commits `dffa916`…`b7ff8f8`).
> `DocumentView.cpp` ist von 3420 auf 2081 Zeilen und von 171 auf 120
> Präprozessor-Direktiven geschrumpft. Wo die Umsetzung vom Plan abweicht, steht das
> unten beim jeweiligen Schritt unter **Umgesetzt**. Der Abschnitt „Bewusst nicht in
> diesem Plan" gilt unverändert — die Edit-Session-Logik ist nicht angefasst.

Stand: 2026-07-31. Ausgangslage: `DocumentView.cpp` hat 3420 Zeilen, 68 Methoden und
171 Präprozessor-Direktiven. Ziel ist ein Koordinator von ~400 Zeilen, der vier
Controller besitzt, statt alles selbst zu tun.

Der Plan ist in zwei Teile geschnitten: **Schritt 0** ist reine Dateiverschiebung ohne
Verhaltensrisiko, **Schritte 1–4** sind je ein eigener Commit mit grünem Build dazwischen.

---

## Schritt 0 — `src/ui/` konsistent gliedern

Es gibt bereits `widgets/`, `tools/`, `organizer/`, `theme/` — aber 13 Klassen liegen
trotzdem flach daneben. Entweder Unterordner sind die Regel oder es gibt keine.

### Zielbaum

```
src/ui/
├── DocumentView.{hpp,cpp}          bleibt (Koordinator)
├── MainWindow.{hpp,cpp}            bleibt (Top-Level)
├── PresentationWindow.{hpp,cpp}    bleibt (Top-Level)
├── bars/
│   ├── TopToolbar.{hpp,cpp}
│   ├── FormatBar.{hpp,cpp}
│   └── StatusBar.{hpp,cpp}
├── panels/
│   ├── LeftSidebar.{hpp,cpp}
│   ├── RightSidebar.{hpp,cpp}
│   ├── ToolPanel.{hpp,cpp}
│   ├── SettingsPanel.{hpp,cpp}
│   └── TextPropertiesPanel.{hpp,cpp}
├── dialogs/
│   ├── ExportDialog.{hpp,cpp}
│   └── PdfOrganizerDialog.{hpp,cpp}   ← aus organizer/, Ordner entfällt
├── view/                              ← neu, siehe Schritte 1–3
├── widgets/                           unverändert
├── tools/                             unverändert
└── theme/                             unverändert
```

`organizer/` löst sich auf — ein Ordner für eine Klasse trägt nichts, `dialogs/` ist die
passende Kategorie.

### Durchführung

```bash
git mv src/ui/TopToolbar.hpp src/ui/bars/    # usw.
```

Danach drei Stellen anfassen:

1. **Include-Pfade** in den verschobenen Dateien und ihren Nutzern. Da alle Includes
   projektrelativ von `src/` aus laufen, ist das ein mechanisches Suchen/Ersetzen:
   `#include "ui/TopToolbar.hpp"` → `#include "ui/bars/TopToolbar.hpp"`.
2. **`CMakeLists.txt`** Zeilen 61–103: Pfade in der expliziten Source-Liste anpassen.
   (Die Liste ist explizit statt `file(GLOB ...)` — das bleibt so, sie ist genau deshalb
   der einzige Ort, an dem gepflegt werden muss.)
3. **AUTOMOC** braucht nichts — Qt findet die `Q_OBJECT`-Klassen über die Source-Liste.

Kein Verhaltensrisiko: Wenn es kompiliert, ist es richtig. Ein Commit.

---

## Der gemeinsame Nenner: `PageCanvas`

Alle vier Controller brauchen dieselben drei Dinge von der View: das Canvas-Widget als
Parent für Overlays, die Seiten-Labels für Positionsrechnung und den Zoomfaktor. Ohne
eine gemeinsame Abstraktion bekommt jeder Controller einen `DocumentView*` zurück-
gereicht — dann hat man die Kopplung nur umbenannt, nicht aufgelöst.

Deshalb zuerst ein schmales Interface, `src/ui/view/PageCanvas.hpp`:

```cpp
#pragma once
#include <QRectF>
#include <QPoint>
#include <utility>

QT_BEGIN_NAMESPACE
class QWidget;
class QLabel;
QT_END_NAMESPACE

/// Was die Controller vom Seiten-Canvas brauchen — mehr nicht.
/// DocumentView implementiert das; die Controller kennen DocumentView nicht.
class PageCanvas
{
public:
    virtual ~PageCanvas() = default;

    /// Parent-Widget für Overlays (Highlights, Rahmen, Editor).
    virtual QWidget *canvasWidget() const = 0;
    /// Label der Seite, oder nullptr wenn nicht gebaut/außerhalb.
    virtual QLabel  *pageLabel(int page) const = 0;
    virtual int      pageCount() const = 0;
    /// Skalierung PDF-Punkte → Bildschirmpixel beim aktuellen Zoom.
    virtual qreal    screenScale() const = 0;
    /// Seite + Label unter einer Canvas-Position; page == -1 wenn daneben.
    virtual std::pair<int, QLabel *> pageAtCanvasPos(const QPoint &p) const = 0;
};
```

`DocumentView : public QScrollArea, public PageCanvas` — die fünf Methoden sind
Einzeiler auf bestehende Member. Das ist der einzige strukturelle Vorgriff; danach ist
jeder Schritt für sich abgeschlossen.

---

## Schritt 1 — `TextSelectionController`

**Quelle:** `DocumentView.cpp` Z. 730–1119, `DocumentView.hpp` Z. 136–151, 207–218.

Erster Schritt, weil die Kopplung nach außen minimal ist: die Selektion redet mit
niemandem außer dem Extractor und ihren eigenen Overlay-Widgets. Guter Ort, um das
Muster einmal sauber durchzuspielen, bevor es an die Bug-behafteten Teile geht.

**Was mitwandert:** `selectionAnchorAt`, `pageLineRects` + `m_lineRectCache`,
`updateTextSelection`, `updateSelectionOverlays`, `clearTextSelection`, `selectedText`,
`copySelectedText`, `struct TextSelectionPart`, `m_textSelection`, `m_selectionOverlays`,
`m_selectTracking`, `m_selectDragging`, `m_selectDragStart`.

`src/ui/view/TextSelectionController.hpp`:

```cpp
#pragma once
#include <QHash>
#include <QList>
#include <QObject>
#include <QRectF>
#include "ui/view/PageCanvas.hpp"

class PdfTextExtractor;

/// Text-Markierung mit dem Select-Tool. Aktiv im normalen UND im Edit-Modus.
/// Anker kommen in Canvas-Koordinaten herein, die Geometrie wird in PDF-Punkten
/// gehalten, damit sie Zoomwechsel und Relayouts überlebt.
class TextSelectionController : public QObject
{
    Q_OBJECT
public:
    explicit TextSelectionController(PageCanvas *canvas, QObject *parent = nullptr);

    /// Nach openFile()/clearDocument(): Caches verwerfen.
    void setDocumentSource(PdfTextExtractor *extractor);   // ggf. Backend-Handle
    void invalidateCaches();

    // Maus-Handling: gibt true zurück, wenn das Event verbraucht wurde.
    bool handlePress(const QPoint &canvasPos);
    bool handleMove(const QPoint &canvasPos);
    bool handleRelease(const QPoint &canvasPos);

    /// Nach Zoom/Relayout: Overlays neu positionieren (Selektion bleibt).
    void relayout();
    void clear();

    bool    hasSelection() const { return !m_parts.isEmpty(); }
    QString selectedText() const;
    void    copyToClipboard() const;

Q_SIGNALS:
    void selectionChanged(bool hasSelection);   // → MainWindow: Copy-Action enablen

private:
    struct Part { int page; QList<QRectF> rects; QString text; };

    bool anchorAt(const QPoint &canvasPos, int *page, QPointF *pdfPt) const;
    void updateSelection(const QPoint &from, const QPoint &to);
    void updateOverlays();
    const QList<QRectF> &lineRects(int page);

    PageCanvas       *m_canvas    { nullptr };
    PdfTextExtractor *m_extractor { nullptr };

    QList<Part>       m_parts;
    QList<QWidget *>  m_overlays;
    QHash<int, QList<QRectF>> m_lineRectCache;

    bool   m_tracking { false };
    bool   m_dragging { false };
    QPoint m_dragStart;
};
```

**Was in `DocumentView` übrig bleibt:**

```cpp
QString DocumentView::selectedText() const   { return m_selection->selectedText(); }
void    DocumentView::copySelectedText()     { m_selection->copyToClipboard(); }
```

und im `eventFilter` drei Weiterleitungen statt der bisherigen Inline-Blöcke:

```cpp
case QEvent::MouseButtonPress:
    if (m_tool == Tool::Select && m_selection->handlePress(canvasPos)) return true;
    break;
```

Der Overlay-Deckel (`kMaxOverlays = 600`, Z. 977) und der Kommentar dazu wandern
unverändert mit — das ist eine bewusste Entscheidung, keine Altlast.

**Prüfen:** Markieren über Seitengrenzen, Zoomwechsel bei aktiver Selektion, Kopieren,
Selektion im Edit-Modus.

**Umgesetzt** (`a92a89c`) — wie geplant. Zwei Anpassungen an der API-Skizze:
`handlePress` gibt `void` zurück statt `bool`, weil der Press das Event nie verbraucht;
`PageCanvas` hat zusätzlich `pageLabelCount()`, damit die Anker-Suche exakt über die
gebauten Labels läuft und nicht über die Dokument-Seitenzahl.

---

## Schritt 2 — `ImageAnnotationLayer`

**Quelle:** `DocumentView.cpp` Z. 1862–2458, `DocumentView.hpp` Z. 113–121, 171–180,
225–229.

**Was mitwandert:** `placeImage`, `placeImageInRect`, `connectImageAnnotation`,
`showImageContextMenu`, `showGeneralContextMenu`, `updateImageOverlayPositions`,
`clearDetectedImageFrames`, `scanCurrentPageForImages`, `struct PlacedImage`,
`m_placedImages`, `m_detectedImageFrames`, `m_imageClipboard` und der komplette
Drag-to-Frame-State.

Die beiden Kontextmenüs gehen explizit mit — `showGeneralContextMenu` heißt zwar
"general", ist aber das Menü für den Image-Modus (Einfügen/Scannen).

`src/ui/view/ImageAnnotationLayer.hpp`:

```cpp
#pragma once
#include <QImage>
#include <QList>
#include <QObject>
#include <QRectF>
#include "ui/view/PageCanvas.hpp"

class ImageAnnotation;
class QUndoStack;

/// Platzierte Bilder + erkannte Bildregionen als Overlay-Ebene über dem Canvas.
/// Positionen werden in PDF-Punkten gehalten, Widgets nur daraus abgeleitet.
class ImageAnnotationLayer : public QObject
{
    Q_OBJECT
public:
    ImageAnnotationLayer(PageCanvas *canvas, QUndoStack *undo, QObject *parent = nullptr);

    void place(const QImage &img, const QPoint &canvasPos);
    void placeInRect(const QImage &img, const QRect &viewportRect);

    bool handlePress(const QPoint &canvasPos);      // Drag-to-Frame
    bool handleMove(const QPoint &canvasPos);
    bool handleRelease(const QPoint &canvasPos);
    bool handleContextMenu(const QPoint &globalPos, const QPoint &canvasPos);

    void relayout();                                 // nach Zoom/Relayout
    void clear();

    /// Erkannte (bereits im PDF vorhandene) Bilder der Seite hervorheben.
    void highlightDetected(int page);
    void clearDetectedHighlights();

    /// Für den Save-Pfad: alles, was gezeichnet werden muss.
    struct Placed { int page; QRectF pdfBounds; QImage image; };
    QList<Placed> placedImages() const;

private:
    void connectAnnotation(ImageAnnotation *ann);
    void showImageMenu(ImageAnnotation *ann, const QPoint &globalPos);
    void showGeneralMenu(const QPoint &globalPos);

    PageCanvas *m_canvas { nullptr };
    QUndoStack *m_undo   { nullptr };
    /* ... State wie in DocumentView.hpp Z. 171–180, 225–229 ... */
};
```

Der `QUndoStack` wird durchgereicht, nicht besessen — die Undo-Commands in
`ui/tools/UndoCommands.hpp` bleiben, wo sie sind.

**Achtung beim Save-Pfad:** `saveToFile` liest heute direkt `m_placedImages`. Das wird zu
`m_imageLayer->placedImages()`. Die Struktur `Placed` lässt bewusst das `QWidget*` weg,
das `PlacedImage` mitführt — der Save-Pfad hat mit Widgets nichts zu tun.

**Prüfen:** Bild per Drop, per Drag-Rahmen, Verschieben/Skalieren, Kontextmenü
Kopieren/Ausschneiden/Einfügen, Undo/Redo, Speichern mit platzierten Bildern.

**Umgesetzt** (`6bee7ad`) mit zwei Abweichungen:

- **`showGeneralContextMenu` bleibt in `DocumentView`.** Der Plan hat es wegen des
  Namens und seiner Position im Image-Block als Bild-Menü gelistet. Es arbeitet aber
  auf dem offenen Editor und der markierten Seitentext-Auswahl und weiß nichts von
  Bildern.
- **Der Layer rechnet ausschließlich in Canvas-Koordinaten.** Vorher wurde
  Canvas → Viewport umgerechnet, nur damit `placeImageInRect` direkt wieder
  zurückrechnet. Die Umrechnung passiert jetzt einmal an der Aufrufgrenze.

Zusätzlich mitgewandert: der ~300-zeilige qpdf-Content-Stream-Detektor, dessen einziger
Aufrufer der Seiten-Scan war.

---

## Schritt 3 — `PageLayoutEngine`

**Quelle:** `DocumentView.cpp` Z. 1612–1862, `DocumentView.hpp` Z. 104–111, 182–187.

**Was mitwandert:** `buildPages`, `rerenderAll`, `rerenderPage`,
`rerenderPageWithBlank`, `buildGridItems`, `relayoutGrid`, `struct GridItem`,
`m_pageLabels`, `m_gridCanvas`, `m_gridItems`, `m_gridCardIndex`, `m_viewMode`.

Hier zieht die Klasse die Seiten-Labels zu sich — d.h. `DocumentView::pageLabel()` aus
`PageCanvas` delegiert ab jetzt an die Engine. Deshalb kommt dieser Schritt **nach** 1
und 2: die beiden Controller reden dann bereits über das Interface und merken nichts
davon.

`src/ui/view/PageLayoutEngine.hpp`:

```cpp
#pragma once
#include <QHash>
#include <QList>
#include <QObject>
#include <QPixmap>

QT_BEGIN_NAMESPACE
class QLabel;
class QVBoxLayout;
class QWidget;
QT_END_NAMESPACE

class PdfRenderer;
class EditSession;

/// Baut und pflegt die Seiten-Widgets — Single-Column und Grid.
/// Kennt Renderer und Session, aber keine Maus-Interaktion.
class PageLayoutEngine : public QObject
{
    Q_OBJECT
public:
    enum class Mode { Single, Grid };

    PageLayoutEngine(QWidget *canvas, QVBoxLayout *layout, QObject *parent = nullptr);

    void setSource(PdfRenderer *renderer, EditSession *session, int pageCount);
    void setZoom(int percent);
    void setMode(Mode mode);
    Mode mode() const { return m_mode; }

    void buildPages();
    void rerenderAll();
    void rerenderPage(int page);
    /// Seite neu rendern und dabei den Bereich `pdfBoundsPts` weiß überdecken
    /// (Live-Vorschau während eines Edits).
    void rerenderPageWithBlank(int page, const QRectF &pdfBoundsPts);

    QLabel *pageLabel(int page) const { return m_pageLabels.value(page, nullptr); }
    int     pageCount() const { return m_pageCount; }

Q_SIGNALS:
    void layoutChanged();      // → Controller: relayout() ihrer Overlays

private:
    void buildGridItems();
    void relayoutGrid();

    struct GridItem { QWidget *card; QLabel *thumb; QLabel *label; QPixmap original; };
    /* ... */
};
```

Das Signal `layoutChanged()` ersetzt die heute verstreuten manuellen Aufrufe von
`updateSelectionOverlays()` / `updateImageOverlayPositions()` nach jedem Relayout. In
`DocumentView` einmal verdrahtet:

```cpp
connect(m_layoutEngine, &PageLayoutEngine::layoutChanged, this, [this] {
    m_selection->relayout();
    m_imageLayer->relayout();
});
```

Das ist der Punkt, an dem sich der Umbau selbst bezahlt macht: bisher muss man an jeder
neuen Relayout-Stelle daran denken, beide Overlay-Sätze nachzuziehen. Vergessene
Aufrufe dieser Art sind die typische Ursache für "Highlight klebt an der alten
Position".

**Prüfen:** Zoom in beiden Modi, Moduswechsel Single↔Grid, Fenster-Resize im Grid,
Rerender nach Edit-Commit, Undo/Redo (ruft `rerenderPage` von außen).

**Umgesetzt** (`44c281d`). `layoutChanged()` funktioniert wie vorgesehen. Drei
Abweichungen, alle weil die `QScrollArea` nicht mit in die Engine kann:

- **`setViewMode` bleibt in `DocumentView`** — es tauscht das Widget der Scroll-Area.
  Die Engine baut nur die Grid-Items auf und ab.
- **`relayoutGrid(int availableWidth)`** bekommt die Viewport-Breite übergeben, statt
  `viewport()->width()` selbst zu lesen.
- **`rerenderPageWithBlank`** bekommt die Erase-Rects als Parameter; sie gehören zur
  Edit-Session, die vorerst in `DocumentView` bleibt.

Ebenfalls beachtet: `tr("Page %1")` war unter dem Kontext `DocumentView` übersetzt. Der
verschobene Aufruf nutzt deshalb explizit
`QCoreApplication::translate("DocumentView", …)` — sonst wären die Übersetzungen in
allen 11 `.ts`-Dateien verwaist. Gleiches gilt für das Bild-Kontextmenü aus Schritt 2.

---

## Schritt 4 — `DocumentExporter` → nach `src/engine/`

**Quelle:** `DocumentView.cpp` Z. 1286–1567 (`allPageContent`, `exportPagesToImages`).

Anders als 1–3 ist das keine UI-Zerlegung, sondern eine falsch einsortierte Datei. Beide
Methoden brauchen ausschließlich `m_renderer`, `m_contentProvider`, `m_session` und
`m_pageCount` — kein Widget, kein Event, kein Zoom. Sie gehören zu `DocxExporter` in
`src/engine/edit/`, nicht in eine `QScrollArea`.

Ziel: `src/engine/edit/DocumentExporter.{hpp,cpp}`

```cpp
#pragma once
#include <QList>
#include <QString>
#include "engine/edit/DocxExporter.hpp"

class PdfRenderer;
class EditSession;
class ContentProvider;

/// Zieht Seiteninhalte für DOCX/Bild-Export aus Renderer + Session.
/// Bewusst ohne UI-Abhängigkeit: direkt testbar.
class DocumentExporter
{
public:
    DocumentExporter(PdfRenderer *renderer, ContentProvider *provider,
                     EditSession *session, int pageCount);

    QList<DocxPage> allPageContent() const;
    bool exportPagesToImages(const QString &outputPath, int quality = 85) const;

private:
    PdfRenderer     *m_renderer  { nullptr };
    ContentProvider *m_provider  { nullptr };
    EditSession     *m_session   { nullptr };
    int              m_pageCount { 0 };
};
```

`DocumentView` behält die beiden Methoden als Einzeiler-Fassade (`MainWindow` und
`ExportDialog` rufen sie so auf) — oder ruft besser gleich der `ExportDialog` den
Exporter direkt. Das ist die einzige offene Design-Frage im Plan; sie lässt sich
entscheiden, wenn der Schritt drankommt.

**Warum zuletzt, obwohl am einfachsten:** Der Schritt ist unabhängig von 1–3 und kann
jederzeit vorgezogen werden. Er steht hier hinten, weil er der erste ist, dessen Ergebnis
sich ohne UI testen lässt — ein guter Anlass, direkt danach das Test-Setup aufzusetzen.

**Umgesetzt** (`b7ff8f8`). Der Bedarf war größer als in der Skizze: neben Renderer,
Provider und Session braucht `allPageContent()` auch die OCR-Engine und — im
Qt-PDF-Pfad — `QPdfDocument` und `PdfTextExtractor`. Statt eines Konstruktors mit sechs
Parametern, dessen Signatur sich je Backend unterscheidet, bündelt
`DocumentExporter::Sources` die geliehenen Zeiger. `DocumentView` behält beide Methoden
als Fassade; die offene Frage, ob `ExportDialog` direkt auf den Exporter geht, ist
weiterhin offen.

---

## Endstand

```
src/ui/
├── DocumentView.{hpp,cpp}          ~400 Zeilen: Koordinator, eventFilter-Dispatch,
│                                   Edit-Session-Steuerung, PageCanvas-Impl
└── view/
    ├── PageCanvas.hpp              Interface, ~30 Zeilen
    ├── TextSelectionController.*   ~400 Zeilen
    ├── ImageAnnotationLayer.*      ~600 Zeilen
    └── PageLayoutEngine.*          ~300 Zeilen

src/engine/edit/
└── DocumentExporter.*              ~300 Zeilen
```

## Bewusst nicht in diesem Plan

- **Die Edit-Session-Logik** (`handleEditClick` 390 Zeilen, `commitCurrentEdit`,
  `createTextFrame`, Z. 2563–3125). Da sitzen die offenen Bugs — Dupes und der
  Edit-Box-Umbruchbug. Erst Bugs fixen, dann umbauen; sonst weiß man bei der nächsten
  Regression nicht, ob sie vom Fix oder vom Refactoring kommt.
- **Die `#ifdef`-Flut** (46× `HAVE_PDF_RENDERING` in einer Datei). Die Kapselung hinter
  ein `IPdfBackend`-Interface in `src/engine/view/` ist ein eigener Plan. Nach dieser
  Zerlegung verteilen sich die Direktiven immerhin auf fünf Dateien statt einer.
- **`SettingsPanel.cpp`** (1318 Zeilen). Groß, aber überwiegend gerader UI-Aufbaucode
  ohne verschränkten Zustand — niedrige Priorität.
