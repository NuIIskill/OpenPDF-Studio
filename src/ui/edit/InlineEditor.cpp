#include "ui/edit/InlineEditor.hpp"

#include <QApplication>
#include <QDebug>
#include <QPainter>
#include <QAbstractTextDocumentLayout>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QWheelEvent>
#include <QTextBlock>
#include <QTextLayout>
#include <QTextLine>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextListFormat>
#include <QGraphicsOpacityEffect>
#include <QTimer>

InlineEditor::InlineEditor(QWidget *parent)
    : QTextEdit(parent)
{
    setObjectName(QStringLiteral("InlineEditor"));
    setFrameShape(QFrame::NoFrame);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    setLineWrapMode(QTextEdit::WidgetWidth);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setContextMenuPolicy(Qt::NoContextMenu);
    auto *opacity = new QGraphicsOpacityEffect(this);
    opacity->setOpacity(1.0);
    setGraphicsEffect(opacity);
    // No inner offsets: the text must sit exactly on the original PDF text
    // position (the frame aligns its inner rect with the block bounds).
    document()->setDocumentMargin(0);
    m_caretTimer = new QTimer(this);
    m_caretTimer->setInterval(530);
    connect(m_caretTimer, &QTimer::timeout, this, [this]() {
        m_caretOn = !m_caretOn;
        viewport()->update();
    });
    m_caretTimer->start();
    connect(this, &QTextEdit::textChanged, this, [this]() {
        const QString jetzt = toPlainText();
        if (jetzt != m_lastText && !m_caretPinned) {
            m_caretOn = true;
            m_caretTimer->start();
        }
        m_lastText = jetzt;
        Q_EMIT changed(jetzt);
        QTimer::singleShot(0, this, &InlineEditor::updateVerticalAlignment);
    });
}

void InlineEditor::applyStyle()
{
    // Strip quote characters from the family so it can't break the stylesheet.
    QString family = m_family;
    family.remove(u'\'').remove(u'"');
    const QString familyList = family.isEmpty()
        ? QStringLiteral("Helvetica, Arial, 'Liberation Sans', sans-serif")
        : QStringLiteral("'%1', Helvetica, Arial, 'Liberation Sans', sans-serif")
              .arg(family);
    const qreal pad = qMax(0.0, m_box.paddingPt * m_scale);
    const qreal tracking = m_box.characterSpacingPt * m_scale;
    QFont doc = styledFont(m_currentFontPx);
    if (!qFuzzyIsNull(tracking))
        doc.setLetterSpacing(QFont::AbsoluteSpacing, tracking);
    setFont(doc);
    document()->setDefaultFont(doc);
    const QString ink = m_glyphs ? m_currentColor.name()
                                 : QStringLiteral("transparent");
    setStyleSheet(QString(
        "QTextEdit#InlineEditor {"
        "  background: transparent;"
        "  border: none;"
        "  font-family: %1;"
        "  font-weight: %2;"
        "  font-style: %3;"
        "  padding: %5px;"
        "  letter-spacing: %6px;"
        "  color: %4;"
        "  selection-color: %4;"
        "  selection-background-color: %7;"
        "}")
        .arg(familyList)
        .arg(m_bold ? 700 : 400)
        .arg(m_italic ? QStringLiteral("italic") : QStringLiteral("normal"))
        .arg(ink)
        .arg(pad, 0, 'f', 1)
        .arg(tracking, 0, 'f', 2)
        .arg(m_glyphs ? QStringLiteral("rgba(59,130,246,255)")
                      : QStringLiteral("rgba(59,130,246,38)")));
    setCursorWidth(m_glyphs ? 1 : 0);
}

void InlineEditor::applyParagraphSpacing()
{
    const qreal spacing = qMax(0.0, m_box.paragraphSpacingPt * m_scale);
    QTextCursor cursor(document());
    cursor.beginEditBlock();
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        QTextCursor blockCursor(block);
        QTextBlockFormat fmt = block.blockFormat();
        fmt.setBottomMargin(block.next().isValid() ? spacing : 0.0);
        Qt::Alignment alignment = Qt::AlignLeft;
        if (m_box.horizontalAlign == TextBoxProperties::HorizontalAlign::Center) alignment = Qt::AlignHCenter;
        else if (m_box.horizontalAlign == TextBoxProperties::HorizontalAlign::Right) alignment = Qt::AlignRight;
        else if (m_box.horizontalAlign == TextBoxProperties::HorizontalAlign::Justify) alignment = Qt::AlignJustify;
        fmt.setAlignment(alignment);
        fmt.setIndent(m_box.indentLevel);
        if (m_box.lineSpacingMultiplier > 0.0)
            fmt.setLineHeight(m_box.lineSpacingMultiplier * 100.0,
                              QTextBlockFormat::ProportionalHeight);
        else if (m_lineSpacingPt > 0.0)
            fmt.setLineHeight(m_lineSpacingPt * m_scale,
                              QTextBlockFormat::FixedHeight);
        else
            fmt.setLineHeight(100.0, QTextBlockFormat::ProportionalHeight);
        blockCursor.setBlockFormat(fmt);
    }
    QTextCursor all(document());
    all.select(QTextCursor::Document);
    if (m_box.listStyle == TextBoxProperties::ListStyle::None) {
        QTextBlockFormat fmt = all.blockFormat();
        fmt.setObjectIndex(-1);
        all.setBlockFormat(fmt);
    } else {
        QTextListFormat list;
        list.setStyle(m_box.listStyle == TextBoxProperties::ListStyle::Numbered
                          ? QTextListFormat::ListDecimal
                          : QTextListFormat::ListDisc);
        list.setIndent(qMax(1, m_box.indentLevel + 1));
        all.createList(list);
    }
    cursor.endEditBlock();
}

void InlineEditor::setBoxProperties(const TextBoxProperties &properties, qreal scale)
{
    m_box = properties;
    m_scale = qMax<qreal>(0.01, scale);
    applyStyle();
    applyParagraphSpacing();
    if (auto *effect = qobject_cast<QGraphicsOpacityEffect *>(graphicsEffect()))
        effect->setOpacity(qBound(0.0, m_box.opacity, 1.0));
    QTimer::singleShot(0, this, &InlineEditor::updateVerticalAlignment);
}

void InlineEditor::updateVerticalAlignment()
{
    int extra = 0;
    if (m_box.verticalAlign != TextBoxProperties::VerticalAlign::Top) {
        const int content = qCeil(document()->documentLayout()->documentSize().height());
        const int free = qMax(0, height() - content);
        extra = m_box.verticalAlign == TextBoxProperties::VerticalAlign::Center
                    ? free / 2 : free;
    }
    setViewportMargins(0, extra, 0, 0);
}

void InlineEditor::resizeEvent(QResizeEvent *e)
{
    QTextEdit::resizeEvent(e);
    QTimer::singleShot(0, this, &InlineEditor::updateVerticalAlignment);
}

QFont InlineEditor::styledFont(qreal pixelFontSize) const
{
    QFont f(m_family.isEmpty() ? QStringLiteral("Helvetica") : m_family);
    f.setStyleHint(QFont::SansSerif);
    f.setPointSizeF(qMax(0.5, pixelFontSize) * 72.0 / screenDpi());
    f.setHintingPreference(QFont::PreferNoHinting);
    f.setBold(m_bold);
    f.setItalic(m_italic);
    return f;
}

void InlineEditor::paintEvent(QPaintEvent *e)
{
    QTextEdit::paintEvent(e);
    if (m_glyphs || !m_caretOn || !hasFocus()) return;

    QRect caret = cursorRect();
    if (m_advance) {
        const QTextCursor c = textCursor();
        const QTextBlock blk = c.block();
        const double breite = m_advance(blk.text().left(c.positionInBlock()));
        if (breite >= 0.0)
            caret.moveLeft(qRound(breite * m_scale));
    }
    caret.setWidth(qMax(1, qRound(m_currentFontPx / 11.0)));
    QPainter p(viewport());
    p.fillRect(caret, m_currentColor.isValid() ? m_currentColor : QColor(Qt::black));
}

QString InlineEditor::laidOutText() const
{
    QString out;
    for (QTextBlock block = document()->begin(); block.isValid();
         block = block.next()) {
        if (!out.isEmpty()) out += QLatin1Char('\n');
        const QTextLayout *layout = block.layout();
        if (!layout || layout->lineCount() <= 1) { out += block.text(); continue; }
        const QString text = block.text();
        for (int i = 0; i < layout->lineCount(); ++i) {
            const QTextLine line = layout->lineAt(i);
            if (i > 0) out += QLatin1Char('\n');
            out += QStringView(text).mid(line.textStart(), line.textLength())
                       .toString();
        }
    }
    return out;
}

qreal InlineEditor::firstBaselineOffset() const
{
    const QTextBlock block = document()->firstBlock();
    if (block.isValid() && block.layout() && block.layout()->lineCount() > 0) {
        const QTextLine line = block.layout()->lineAt(0);
        return block.layout()->position().y() + line.y() + line.ascent();
    }
    return QFontMetricsF(styledFont(m_currentFontPx)).ascent();
}

void InlineEditor::present(const QString &text, qreal pixelFontSize, const QColor &color,
                           const QString &family, bool bold, bool italic)
{
    m_currentColor  = color.isValid() ? color : QColor(0x11, 0x11, 0x11);
    m_currentFontPx = qMax(0.5, qreal(pixelFontSize));
    m_family        = family;
    m_bold          = bold;
    m_italic        = italic;
    applyStyle();
    setPlainText(text);
    applyParagraphSpacing();
    moveCursor(QTextCursor::End);
    show();
}

void InlineEditor::setFontSize(int pixelFontSize)
{
    setFontSizeF(pixelFontSize);
}

void InlineEditor::setFontSizeF(qreal pixelFontSize)
{
    m_currentFontPx = qMax(0.5, pixelFontSize);
    applyStyle();
}

qreal InlineEditor::screenDpi() const
{
    const qreal dpi = logicalDpiY();
    return dpi > 1.0 ? dpi : 96.0;
}

qreal InlineEditor::contentWidthPt() const
{
    qreal breit = 0.0;
    for (QTextBlock b = document()->begin(); b.isValid(); b = b.next()) {
        const QString zeile = b.text();
        if (zeile.isEmpty()) continue;
        double w = m_advance ? m_advance(zeile) : -1.0;
        if (w < 0.0) {
            w = QFontMetricsF(styledFont(m_currentFontPx)).horizontalAdvance(zeile)
                / qMax(0.01, m_scale);
        }
        breit = qMax(breit, w);
    }
    return breit;
}

void InlineEditor::setAdvanceMeasure(std::function<double(const QString &)> measure)
{
    m_advance = std::move(measure);
    viewport()->update();
}

void InlineEditor::setCaretVisible(bool on)
{
    m_caretPinned = true;
    m_caretTimer->stop();
    m_caretOn = on;
    viewport()->update();
}

void InlineEditor::setLineSpacingPt(qreal pt)
{
    if (qFuzzyCompare(m_lineSpacingPt + 1.0, pt + 1.0)) return;
    m_lineSpacingPt = qMax(0.0, pt);
    applyParagraphSpacing();
}

void InlineEditor::setGlyphsVisible(bool on)
{
    if (m_glyphs == on) return;
    m_glyphs = on;
    applyStyle();
}

void InlineEditor::setColor(const QColor &color)
{
    if (!color.isValid()) return;
    m_currentColor = color;
    applyStyle();
}

void InlineEditor::setTextFont(const QString &family, bool bold, bool italic)
{
    m_family = family;
    m_bold   = bold;
    m_italic = italic;
    applyStyle();
}

void InlineEditor::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape) {
        Q_EMIT cancelled();
        return;
    }
    // Enter inserts a newline — like any text editor. Committing happens by
    // clicking outside (focus out); Escape cancels.
    // Delete key with the full text selected → erase the block entirely.
    if (e->key() == Qt::Key_Delete) {
        const QTextCursor c = textCursor();
        const int selStart = qMin(c.anchor(), c.position());
        const int selEnd   = qMax(c.anchor(), c.position());
        if (selStart == 0 && selEnd == toPlainText().length() && selEnd > 0) {
            if (!m_committing) {
                m_committing = true;
                Q_EMIT committed(QString());
            }
            return;
        }
    }
    QTextEdit::keyPressEvent(e);
}

void InlineEditor::wheelEvent(QWheelEvent *e)
{
    // The editor has no scrollbars (it grows with its content), so wheel
    // input belongs to the page. Merely calling ignore() is NOT enough:
    // QAbstractScrollArea's viewportEvent reports wheel events as handled
    // even when ignored, so they never propagate — scrolling appears dead
    // whenever the cursor is over an open editor. Forward a copy directly
    // to the enclosing scroll area's viewport instead.
    e->accept();
    QWidget *w = parentWidget();
    while (w && !qobject_cast<QAbstractScrollArea *>(w))
        w = w->parentWidget();
    if (auto *area = qobject_cast<QAbstractScrollArea *>(w)) {
        QWidget *vp = area->viewport();
        QWheelEvent copy(vp->mapFromGlobal(e->globalPosition().toPoint()),
                         e->globalPosition(), e->pixelDelta(), e->angleDelta(),
                         e->buttons(), e->modifiers(), e->phase(),
                         e->inverted());
        QApplication::sendEvent(vp, &copy);
    }
}

void InlineEditor::focusOutEvent(QFocusEvent *e)
{
    if (m_suppressFocusOut || m_dragMode) {
        m_suppressFocusOut = false;
        QTextEdit::focusOutEvent(e);
        return;   // spurious/transient focus loss — don't commit
    }
    QTextEdit::focusOutEvent(e);
    // Property controls intentionally take focus while the text box stays
    // active. Walking away to any other part of the app still commits.
    for (QWidget *w = QApplication::focusWidget(); w; w = w->parentWidget()) {
        if (w->objectName() == QLatin1String("TextPanel"))
            return;
    }
    if (!m_committing) {
        m_committing = true;
        Q_EMIT committed(toPlainText());
    }
}
