#pragma once

#include <QObject>
#include <QByteArray>
#include <QDateTime>
#include <QKeySequence>
#include <QString>
#include <QStringList>

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

    // ── Toolbar ───────────────────────────────────────────────────────────
    // The left sidebar's tools, as the user arranged them. An empty order
    // means "never customised" - the sidebar then uses its built-in one, so
    // tools added in a later version show up instead of being silently
    // dropped for everybody who ever opened the dialog.
    [[nodiscard]] QStringList toolOrder() const;
    void setToolOrder(const QStringList &ids);

    [[nodiscard]] QStringList hiddenTools() const;
    void setHiddenTools(const QStringList &ids);

    /// Forget both - back to the built-in order with nothing hidden.
    void resetToolLayout();

    // ── Panels ────────────────────────────────────────────────────────────
    // When off, the window always starts with the default panel layout and
    // the stored state below is left untouched.
    [[nodiscard]] bool preservePanelLayout() const;
    void setPreservePanelLayout(bool enabled);

    [[nodiscard]] bool rightPanelCollapsed() const;
    void setRightPanelCollapsed(bool collapsed);

    [[nodiscard]] QByteArray splitterState() const;
    void setSplitterState(const QByteArray &state);

    // ── Media ─────────────────────────────────────────────────────────────
    // How embedded video is played:
    //   "inapp"  in the program, over the page (falls back to "system" when
    //            this build has no Qt Multimedia)
    //   "system" the system's player
    //   "custom" the command from customPlayerCommand()
    [[nodiscard]] QString mediaPlayback() const;
    void setMediaPlayback(const QString &mode);

    // Command line for "custom". "%1" is replaced by the file path; without
    // the placeholder the path is appended.
    [[nodiscard]] QString customPlayerCommand() const;
    void setCustomPlayerCommand(const QString &command);

    // ── Advanced ──────────────────────────────────────────────────────────
    [[nodiscard]] bool autoUpdateCheck() const;
    void setAutoUpdateCheck(bool enabled);

    // "startup" | "daily" | "weekly" | "monthly"
    [[nodiscard]] QString updateInterval() const;
    void setUpdateInterval(const QString &interval);

    // When the last update check got an answer, UTC. Invalid when there has
    // never been one - that is what makes the first start check.
    [[nodiscard]] QDateTime lastUpdateCheck() const;
    void setLastUpdateCheck(const QDateTime &when);

    [[nodiscard]] bool hardwareAcceleration() const;
    void setHardwareAcceleration(bool enabled);

    [[nodiscard]] bool limitMemoryUsage() const;
    void setLimitMemoryUsage(bool enabled);

    [[nodiscard]] bool debugLogging() const;
    void setDebugLogging(bool enabled);

    // "error" | "warning" | "info" | "debug"
    [[nodiscard]] QString logLevel() const;
    void setLogLevel(const QString &level);

    void sync();

private:
    static constexpr auto kWindowGeometry = "window/geometry";
    static constexpr auto kWindowState    = "window/state";
    static constexpr auto kLastOpenedFile = "document/lastOpenedFile";
    static constexpr auto kZoomLevel      = "view/zoomLevel";
    static constexpr auto kTheme          = "appearance/theme";
    static constexpr auto kLanguage       = "appearance/language";
    static constexpr auto kToolOrder      = "toolbar/order";
    static constexpr auto kToolHidden     = "toolbar/hidden";
};
