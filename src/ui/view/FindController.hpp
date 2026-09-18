#pragma once

#include <QList>
#include <QObject>
#include <QRectF>

class DocumentSource;
class IconButton;
class PageCanvas;

QT_BEGIN_NAMESPACE
class QEvent;
class QAction;
class QFrame;
class QLabel;
class QLineEdit;
class QTimer;
class QWidget;
QT_END_NAMESPACE

/// Controls document search, its floating bar, and page-anchored highlights.
class FindController : public QObject
{
    Q_OBJECT

public:
    FindController(PageCanvas *canvas, QWidget *viewport,
                   DocumentSource *source, QObject *parent = nullptr);

    void open();
    void close();
    void documentChanged();
    void relayout();
    void retranslateUi();
    void refreshTheme();

Q_SIGNALS:
    void matchActivated(int page, const QRectF &bounds);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Match {
        int           page { -1 };
        QList<QRectF> rects;
    };

    void scheduleSearch();
    void performSearch();
    void activateRelative(int delta);
    void activate(int index);
    void updateCounter();
    void updateOverlays();
    void positionPanel();

    PageCanvas    *m_canvas   { nullptr };
    QWidget       *m_viewport { nullptr };
    DocumentSource *m_source  { nullptr };
    QWidget       *m_panel    { nullptr };
    QFrame        *m_bar      { nullptr };
    QLineEdit     *m_edit     { nullptr };
    QAction       *m_searchAction { nullptr };
    QLabel        *m_counter  { nullptr };
    IconButton    *m_previous { nullptr };
    IconButton    *m_next     { nullptr };
    IconButton    *m_close    { nullptr };
    QTimer        *m_timer    { nullptr };

    QList<Match>   m_matches;
    QList<QWidget *> m_overlays;
    int            m_active { -1 };
};
