#pragma once

#include <QColor>
#include <QString>

namespace Theme {

// ── Background & Surface ──────────────────────────────────────────────────
inline const QColor Background  { "#F8FAFC" };   // main app background
inline const QColor Surface     { "#FFFFFF" };   // panels / cards

// ── Brand ─────────────────────────────────────────────────────────────────
inline const QColor Primary      { "#2563EB" };
inline const QColor PrimaryHover { "#3B82F6" };

// ── Text ──────────────────────────────────────────────────────────────────
inline const QColor TextPrimary   { "#0F172A" };
inline const QColor TextSecondary { "#64748B" };

// ── Structural ────────────────────────────────────────────────────────────
inline const QColor Border { "#E2E8F0" };

// ── Left Sidebar ──────────────────────────────────────────────────────────
inline const QColor SidebarBg    { "#1E293B" };
inline const QColor SidebarText  { "#F1F5F9" };
inline const QColor SidebarHover { "#334155" };

// ── Helpers ───────────────────────────────────────────────────────────────

/// Load the compiled-in QSS stylesheet.
QString loadStyleSheet();

} // namespace Theme
