# OpenPDF Studio — Architecture

## Overview

OpenPDF Studio is a Qt 6 / C++20 desktop PDF editor targeting Wayland on
Linux.  The current codebase is UI-only; PDF rendering and editing
functionality will be added incrementally.

---

## Directory layout

```
src/
├─ main.cpp               Entry point.  Sets QT_QPA_PLATFORM=wayland,
│                         constructs QApplication, applies the global
│                         stylesheet, then delegates to App::startup().
│
├─ app/
│  ├─ App                 Application controller.  Owns MainWindow and
│  │                      AppSettings.  Orchestrates startup/shutdown
│  │                      and bridges QApplication ↔ UI.
│  └─ AppSettings         Thin QSettings wrapper with typed accessors for
│                         all persisted keys (geometry, zoom, last file …).
│
├─ ui/
│  ├─ MainWindow          QMainWindow subclass.  Builds the root layout:
│  │                        TopToolbar / QSplitter / StatusBar
│  │                      Wires zoom, tool-select, and page-click signals.
│  │
│  ├─ TopToolbar          Fixed-height (56 px) widget.  Logo, file name,
│  │                      zoom controls, tool buttons (Cursor/Text/
│  │                      Annotate/Forms), settings button.
│  │
│  ├─ LeftSidebar         Dark (1E293B) fixed-width (220 px) panel.
│  │                      Header + QScrollArea of ThumbnailItem widgets.
│  │
│  ├─ DocumentView        QScrollArea subclass.  Hosts PagePlaceholder
│  │                      widgets stacked vertically.  Applies zoom scale.
│  │
│  ├─ RightSidebar        White fixed-width (260 px) properties panel.
│  │                      Contains ToolPanel.
│  │
│  ├─ ToolPanel           Section-based placeholder property inspector
│  │                      (Dokument / Seite / Objekt).
│  │
│  ├─ StatusBar           Fixed-height (28 px) bottom bar.  Page counter,
│  │                      zoom level, file-size placeholder.
│  │
│  ├─ widgets/
│  │  ├─ IconButton       36×36 QPushButton.  Transparent bg, blue-tint
│  │  │                   hover, optional checkable/toggle mode.
│  │  ├─ PagePlaceholder  595×842 px white card with QPainter mock content.
│  │  └─ ThumbnailItem    Sidebar thumbnail: painted mini-page, page number,
│  │                      hover + selected states (blue left-edge indicator).
│  │
│  └─ theme/
│     ├─ Theme            Namespace with QColor constants and
│     │                   loadStyleSheet() factory.
│     └─ Style.qss        Comprehensive QSS covering all widgets.
│
resources/
└─ openpdf-studio.qrc     Qt resource file.  Embeds Style.qss at
                          :/theme/Style.qss.  Icons prefix reserved.
```

---

## Signal / slot topology

```
TopToolbar::zoomInRequested   → MainWindow::onZoomIn
TopToolbar::zoomOutRequested  → MainWindow::onZoomOut
TopToolbar::toolSelected      → MainWindow::onToolSelected
LeftSidebar::pageClicked      → MainWindow λ → StatusBar::setCurrentPage
DocumentView::zoomChanged     → (reserved for future toolbar sync)
```

---

## Design tokens

| Token          | Value     |
|----------------|-----------|
| Background     | #F8FAFC   |
| Surface        | #FFFFFF   |
| Primary        | #2563EB   |
| PrimaryHover   | #3B82F6   |
| TextPrimary    | #0F172A   |
| TextSecondary  | #64748B   |
| Border         | #E2E8F0   |
| SidebarBg      | #1E293B   |
| SidebarText    | #F1F5F9   |
| SidebarHover   | #334155   |

Border radii: 12 px cards, 8 px buttons, 6 px inputs.

---

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
./build/OpenPDFStudio
```

Requires: CMake ≥ 3.20, Qt 6.4+, C++20-capable compiler, Wayland runtime.
