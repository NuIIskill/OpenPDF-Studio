#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>

/// Thin wrapper around QSettings for persistent application preferences.
///
/// All keys are centralised here to avoid magic strings scattered across
/// the codebase.
class AppSettings : public QObject
{
    Q_OBJECT

public:
    explicit AppSettings(QObject *parent = nullptr);

    // ── Window ────────────────────────────────────────────────────────────
    [[nodiscard]] QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray &geometry);

    [[nodiscard]] QByteArray windowState() const;
    void setWindowState(const QByteArray &state);

    // ── Document ──────────────────────────────────────────────────────────
    [[nodiscard]] QString lastOpenedFile() const;
    void setLastOpenedFile(const QString &path);

    // ── View ──────────────────────────────────────────────────────────────
    [[nodiscard]] int  zoomLevel() const;
    void setZoomLevel(int percent);

    // ── Appearance ────────────────────────────────────────────────────────
    [[nodiscard]] QString theme() const;
    void setTheme(const QString &name);

    /// Persist all settings to disk immediately.
    void sync();

private:
    // Key constants
    static constexpr auto kWindowGeometry = "window/geometry";
    static constexpr auto kWindowState    = "window/state";
    static constexpr auto kLastOpenedFile = "document/lastOpenedFile";
    static constexpr auto kZoomLevel      = "view/zoomLevel";
    static constexpr auto kTheme          = "appearance/theme";
};
