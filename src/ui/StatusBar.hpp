#pragma once

#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
QT_END_NAMESPACE

class IconButton;

/// Bottom bar of the main window.
///
/// Fixed height: 44 px.
/// Left: page indicator.
/// Right: save, print, zoom controls, settings.
class StatusBar : public QWidget
{
    Q_OBJECT

public:
    explicit StatusBar(QWidget *parent = nullptr);

    void setCurrentPage(int page, int total);
    void setZoom(int percent);

Q_SIGNALS:
    void zoomInRequested();
    void zoomOutRequested();
    void saveRequested();
    void printRequested();
    void settingsRequested();

private:
    void buildLayout();
    QWidget *makeSeparator();

    QLabel     *m_pageLabel  { nullptr };
    QLabel     *m_zoomLabel  { nullptr };
    IconButton *m_zoomOutBtn { nullptr };
    IconButton *m_zoomInBtn  { nullptr };
    IconButton *m_saveBtn    { nullptr };
    IconButton *m_printBtn   { nullptr };
    IconButton *m_settingsBtn{ nullptr };
};
