#pragma once

#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
QT_END_NAMESPACE

class IconButton;

/// Horizontal top bar — 56 px.
///
/// Left:   logo | app name | separator | document tab | + new-tab button
/// Right:  save | print | sep | undo | redo | sep | zoom-out | zoom% | zoom-in | sep | view-single | view-grid
class TopToolbar : public QWidget
{
    Q_OBJECT

public:
    explicit TopToolbar(QWidget *parent = nullptr);

    void setFileName(const QString &name);
    void setZoom(int percent);
    void refreshTheme();

Q_SIGNALS:
    void zoomInRequested();
    void zoomOutRequested();
    void undoRequested();
    void redoRequested();
    void saveRequested();
    void printRequested();

private:
    void buildLayout();
    QWidget *makeSeparator();

    QLabel     *m_tabLabel      { nullptr };
    QLabel     *m_zoomLabel     { nullptr };
    IconButton *m_zoomInBtn     { nullptr };
    IconButton *m_zoomOutBtn    { nullptr };
    IconButton *m_saveBtn       { nullptr };
    IconButton *m_printBtn      { nullptr };
    IconButton *m_undoBtn       { nullptr };
    IconButton *m_redoBtn       { nullptr };
    IconButton *m_viewSingleBtn { nullptr };
    IconButton *m_viewGridBtn   { nullptr };
};
