#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

namespace Theme {

// ── Runtime dark-mode flag (set once from main before any widget is created) ──
extern bool DarkMode;

// ── Colors (mutable so dark-mode startup can override them) ───────────────
extern QColor Primary;
extern QColor IconNormal;
extern QColor IconMuted;
extern QColor IconChecked;
extern QColor IconDisabled;

// ── Stylesheet ────────────────────────────────────────────────────────────
QString loadStyleSheet();

// ── Theme application ─────────────────────────────────────────────────────
/// Apply palette + QSS for the given mode ("system"|"light"|"dark").
void apply(const QString &mode);

// ── SVG icon helpers ──────────────────────────────────────────────────────

/// Render an SVG from Qt resources at the given color and size, scaling its
/// viewBox to fit. The SVG must be at :/icons/<name>.svg. `color` replaces
/// currentColor (Lucide icons); SVGs with fixed fills ignore it.
QPixmap renderSvg(const QString &name, const QColor &color,
                  int size, qreal dpr = 1.0);

/// Build a QIcon with Normal/Off, Normal/On (checked=blue), Disabled states,
/// at 1× and 2× for HiDPI.
QIcon makeIcon(const QString &name,
               const QColor &normal   = IconNormal,
               const QColor &checked  = IconChecked,
               const QColor &disabled = IconDisabled,
               int size = 20);

} // namespace Theme
