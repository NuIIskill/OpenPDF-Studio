#pragma once

#include <QImage>
#include <QObject>
#include <QRect>
#include <QSizeF>

#include <functional>

QT_BEGIN_NAMESPACE
class QAbstractScrollArea;
class QLabel;
QT_END_NAMESPACE

class PageCanvas;

/// Carries a signature under the cursor until a click drops it onto a page.
class SignaturePlacement : public QObject
{
    Q_OBJECT

public:
    using FieldPlaced = std::function<void(int page, const QRectF &pdfBounds)>;

    SignaturePlacement(PageCanvas *canvas, QAbstractScrollArea *view,
                       QObject *parent = nullptr);

    void start(const QImage &image);
    void startField(const QImage &preview, FieldPlaced onPlaced);
    void cancel();

Q_SIGNALS:
    void placed(const QImage &image, const QRect &canvasRect);

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
    QRect ghostRect(const QPoint &canvasPos) const;
    void  moveGhost(const QPoint &canvasPos);
    void  setListening(bool on);

    PageCanvas          *m_canvas { nullptr };
    QAbstractScrollArea *m_view   { nullptr };
    QLabel              *m_ghost  { nullptr };
    QImage               m_image;
    FieldPlaced          m_onField;
    QSizeF               m_sizePt;
    bool                 m_listening      { false };
    bool                 m_active         { false };
    bool                 m_swallowRelease { false };
    bool                 m_canvasTracked  { false };
    bool                 m_viewportTracked { false };
};
