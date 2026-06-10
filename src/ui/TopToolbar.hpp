#pragma once

#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
QT_END_NAMESPACE

class IconButton;

/// The horizontal toolbar strip at the top of the main window.
///
/// Fixed height: 56 px.
/// Contains (left→right):
///   - App logo/name
///   - Separator
///   - Current file name
///   - Stretch
///   - Zoom controls (−, label, +)
///   - Separator
///   - Tool buttons (Cursor, Text, Annotate, Forms)
///   - Separator
///   - Settings button
class TopToolbar : public QWidget
{
    Q_OBJECT

public:
    explicit TopToolbar(QWidget *parent = nullptr);

    void setFileName(const QString &name);
    void setZoom(int percent);

Q_SIGNALS:
    void zoomInRequested();
    void zoomOutRequested();
    void toolSelected(const QString &tool);
    void settingsRequested();

private:
    void buildLayout();
    QWidget *makeSeparator();

    QLabel      *m_fileNameLabel { nullptr };
    QLabel      *m_zoomLabel     { nullptr };
    IconButton  *m_zoomInBtn     { nullptr };
    IconButton  *m_zoomOutBtn    { nullptr };
    IconButton  *m_cursorBtn     { nullptr };
    IconButton  *m_textBtn       { nullptr };
    IconButton  *m_annotateBtn   { nullptr };
    IconButton  *m_formsBtn      { nullptr };
    IconButton  *m_settingsBtn   { nullptr };
};
