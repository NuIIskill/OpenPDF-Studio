#pragma once

#include <QAbstractTextDocumentLayout>

/// Lays out a multi-line field's editor in the lines, line height and baselines PDFium will draw.
class FormEditLayout : public QAbstractTextDocumentLayout
{
    Q_OBJECT

public:
    explicit FormEditLayout(QTextDocument *document);

    void setMetrics(qreal size, qreal scale, qreal width, int alignment, const QPointF &plate);

    void draw(QPainter *painter, const PaintContext &context) override;
    int hitTest(const QPointF &point, Qt::HitTestAccuracy accuracy) const override;
    int pageCount() const override { return 1; }
    QSizeF documentSize() const override;
    QRectF frameBoundingRect(QTextFrame *frame) const override;
    QRectF blockBoundingRect(const QTextBlock &block) const override;

protected:
    void documentChanged(int from, int charsRemoved, int charsAdded) override;

private:
    void relayout();

    qreal m_size { 10 };
    qreal m_scale { 1 };
    qreal m_width { 100 };
    qreal m_height { 0 };
    int   m_alignment { 0 };
    QPointF m_plate;
};
