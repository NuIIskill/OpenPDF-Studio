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

    [[nodiscard]] QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray &geometry);

    [[nodiscard]] QByteArray windowState() const;
    void setWindowState(const QByteArray &state);

    [[nodiscard]] QString lastOpenedFile() const;
    void setLastOpenedFile(const QString &path);

    [[nodiscard]] int  zoomLevel() const;
    void setZoomLevel(int percent);

    [[nodiscard]] QString theme() const;
    void setTheme(const QString &name);

    [[nodiscard]] QString language() const;
    void setLanguage(const QString &lang);

    [[nodiscard]] static QString systemDefaultLanguage();

    [[nodiscard]] QKeySequence shortcut(const QString &actionKey,
                                        const QKeySequence &defaultSeq) const;
    void setShortcut(const QString &actionKey, const QKeySequence &seq);

    [[nodiscard]] int     zoomStep()      const;
    void setZoomStep(int step);

    [[nodiscard]] bool    ctrlWheelZoom() const;
    void setCtrlWheelZoom(bool enabled);

    [[nodiscard]] bool    zoomToPointer() const;
    void setZoomToPointer(bool enabled);

    [[nodiscard]] QString wheelAction()   const;
    void setWheelAction(const QString &action);

    [[nodiscard]] QStringList toolOrder() const;
    void setToolOrder(const QStringList &ids);

    [[nodiscard]] QStringList hiddenTools() const;
    void setHiddenTools(const QStringList &ids);

    void resetToolLayout();

    [[nodiscard]] bool preservePanelLayout() const;
    void setPreservePanelLayout(bool enabled);

    [[nodiscard]] bool rightPanelCollapsed() const;
    void setRightPanelCollapsed(bool collapsed);

    [[nodiscard]] QByteArray splitterState() const;
    void setSplitterState(const QByteArray &state);

    [[nodiscard]] QString mediaPlayback() const;
    void setMediaPlayback(const QString &mode);

    [[nodiscard]] QString customPlayerCommand() const;
    void setCustomPlayerCommand(const QString &command);

    [[nodiscard]] bool autoUpdateCheck() const;
    void setAutoUpdateCheck(bool enabled);

    [[nodiscard]] QString updateInterval() const;
    void setUpdateInterval(const QString &interval);

    [[nodiscard]] QDateTime lastUpdateCheck() const;
    void setLastUpdateCheck(const QDateTime &when);

    [[nodiscard]] bool hardwareAcceleration() const;
    void setHardwareAcceleration(bool enabled);

    [[nodiscard]] bool limitMemoryUsage() const;
    void setLimitMemoryUsage(bool enabled);

    [[nodiscard]] bool debugLogging() const;
    void setDebugLogging(bool enabled);

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
