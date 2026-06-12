#pragma once

#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
QT_END_NAMESPACE

class IconButton;

/// The horizontal toolbar strip at the top of the main window.
///
/// Fixed height: 56 px.
/// Left: app name | separator | file name.
/// Right: save, print | separator | zoom controls.
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
    void saveRequested();
    void printRequested();

private:
    void buildLayout();
    QWidget *makeSeparator();

    QLabel     *m_fileNameLabel { nullptr };
    QLabel     *m_zoomLabel     { nullptr };
    IconButton *m_zoomInBtn     { nullptr };
    IconButton *m_zoomOutBtn    { nullptr };
    IconButton *m_saveBtn       { nullptr };
    IconButton *m_printBtn      { nullptr };
};
