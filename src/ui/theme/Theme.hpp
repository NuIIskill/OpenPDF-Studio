#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

namespace Theme {

// ── Colors ────────────────────────────────────────────────────────────────
inline const QColor Primary      { "#2563EB" };
inline const QColor IconNormal   { "#374151" };
inline const QColor IconMuted    { "#6B7280" };
inline const QColor IconChecked  { "#2563EB" };
inline const QColor IconDisabled { "#D1D5DB" };

// ── Stylesheet ────────────────────────────────────────────────────────────
QString loadStyleSheet();

// ── SVG icon helpers ──────────────────────────────────────────────────────

/// Render a Lucide SVG from Qt resources at the given color and size.
/// The SVG must be at :/icons/<name>.svg.
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
