#pragma once

#include <QObject>
#include <QByteArray>
#include <QKeySequence>
#include <QString>

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

    [[nodiscard]] QString language() const;
    void setLanguage(const QString &lang);

    // ── Shortcuts ─────────────────────────────────────────────────────────
    [[nodiscard]] QKeySequence shortcut(const QString &actionKey,
                                        const QKeySequence &defaultSeq) const;
    void setShortcut(const QString &actionKey, const QKeySequence &seq);

    // ── Zoom ──────────────────────────────────────────────────────────────
    [[nodiscard]] int     zoomStep()      const;
    void setZoomStep(int step);

    [[nodiscard]] bool    ctrlWheelZoom() const;
    void setCtrlWheelZoom(bool enabled);

    [[nodiscard]] bool    zoomToPointer() const;
    void setZoomToPointer(bool enabled);

    [[nodiscard]] QString wheelAction()   const;  // "scroll" | "zoom"
    void setWheelAction(const QString &action);

    void sync();

private:
    static constexpr auto kWindowGeometry = "window/geometry";
    static constexpr auto kWindowState    = "window/state";
    static constexpr auto kLastOpenedFile = "document/lastOpenedFile";
    static constexpr auto kZoomLevel      = "view/zoomLevel";
    static constexpr auto kTheme          = "appearance/theme";
    static constexpr auto kLanguage       = "appearance/language";
};
