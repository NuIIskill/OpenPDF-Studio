#include "ui/edit/InlineEditor.hpp"

#include "engine/edit/StandardFont.hpp"

#include <QApplication>
#include <QDebug>
#include <QPainter>
#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QScrollBar>
#include <QSet>
#include <QWheelEvent>
#include <QTextBlock>
#include <QTextLayout>
#include <QTextLine>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextListFormat>
#include <QGraphicsOpacityEffect>
#include <QTimer>

#include <cmath>
#include <limits>
#include <utility>

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

    document()->setDocumentMargin(0);
    m_caretTimer = new QTimer(this);
    m_caretTimer->setInterval(530);
    connect(m_caretTimer, &QTimer::timeout, this, [this]() {
        m_caretOn = !m_caretOn;
        viewport()->update();
    });
    m_caretTimer->start();
    for (QScrollBar *bar : { horizontalScrollBar(), verticalScrollBar() })
        connect(bar, &QScrollBar::valueChanged, this, [this, bar](int value) {
            if (value != 0 && usesEngineLayout()) bar->setValue(0);
        });
    connect(this, &QTextEdit::textChanged, this, [this]() {
        const QString jetzt = toPlainText();
        if (jetzt != m_lastText && !m_caretPinned) {
            m_caretOn = true;
            m_caretTimer->start();
        }
        m_lastText = jetzt;
        relayout();
        Q_EMIT changed(jetzt);
        QTimer::singleShot(0, this, &InlineEditor::updateVerticalAlignment);
    });
    connect(this, &QTextEdit::cursorPositionChanged, this, [this]() {
        if (usesEngineLayout()) viewport()->update();
    });
}

bool InlineEditor::usesEngineLayout() const
{
    return !m_glyphs && m_source;
}

void InlineEditor::applyStyle()
{

    QString family = m_family;
    family.remove(u'\'').remove(u'"');
    QStringList kandidaten;
    if (!m_standardFace && !family.isEmpty()) kandidaten << family;
    kandidaten << StandardFont::qtFamilies(StandardFont::kindOf(m_family));
    QStringList zitiert;
    for (const QString &k : std::as_const(kandidaten))
        zitiert << QStringLiteral("'%1'").arg(k);
    const QString familyList = zitiert.join(QStringLiteral(", "));
    const qreal pad = usesEngineLayout() ? 0.0 : qMax(0.0, m_box.paddingPt * m_scale);
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
        "  text-decoration: %8;"
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
                      : QStringLiteral("transparent"))
        .arg(m_underline ? QStringLiteral("underline")
                         : QStringLiteral("none")));
    setCursorWidth(m_glyphs ? 1 : 0);
}

void InlineEditor::applyParagraphSpacing()
{
    if (usesEngineLayout()) return;
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
    relayout();
    QTimer::singleShot(0, this, &InlineEditor::updateVerticalAlignment);
}

void InlineEditor::updateVerticalAlignment()
{
    int extra = 0;
    if (!usesEngineLayout() && m_box.verticalAlign != TextBoxProperties::VerticalAlign::Top) {
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

void InlineEditor::scrollContentsBy(int dx, int dy)
{
    if (usesEngineLayout()) return;
    QTextEdit::scrollContentsBy(dx, dy);
}

QFont InlineEditor::styledFont(qreal pixelFontSize) const
{
    const StandardFont::Kind art = StandardFont::kindOf(m_family);
    QFont f;
    if (m_standardFace) {
        f.setFamilies(StandardFont::qtFamilies(art));
    } else {
        f.setFamily(m_family.isEmpty() ? QStringLiteral("Helvetica") : m_family);
        f.setFamilies(QStringList(m_family) + StandardFont::qtFamilies(art));
    }
    f.setStyleHint(art == StandardFont::Kind::Mono  ? QFont::TypeWriter
                 : art == StandardFont::Kind::Serif ? QFont::Serif
                                                    : QFont::SansSerif);
    f.setPointSizeF(qMax(0.5, pixelFontSize) * 72.0 / screenDpi());
    f.setHintingPreference(QFont::PreferNoHinting);
    f.setBold(m_bold);
    f.setItalic(m_italic);
    f.setUnderline(m_underline);
    return f;
}

void InlineEditor::refreshMetrics()
{
    const QString text = plainText();
    if (!m_metricsStale && text == m_metricsText) return;
    const QList<uint> points = text.toUcs4();
    QSet<uint> characters(points.cbegin(), points.cend());
    if (m_metricsStale || characters != m_metricsCharacters)
        m_metrics = m_source ? m_source(text) : TextLayout::Metrics();
    m_metricsText       = text;
    m_metricsCharacters = std::move(characters);
    m_metricsStale      = false;
}

void InlineEditor::relayout()
{
    m_lines.clear();
    if (!usesEngineLayout()) return;
    refreshMetrics();
    if (!m_metrics.isValid()) return;

    const double size = m_metrics.sizePt;
    TextLayout::Params params;
    params.lineStepPt         = TextLayout::lineStep(size, m_lineSpacingPt);
    params.paragraphSpacingPt = m_box.paragraphSpacingPt;
    params.charSpacingPt      = m_box.characterSpacingPt;
    params.list               = m_box.listStyle;
    params.align              = m_box.horizontalAlign;
    const TextLayout::Result result = TextLayout::layout(
        m_metricsText, params, m_box, QRectF(m_metrics.boxTopLeft, m_boundsPt), size,
        m_wraps, m_hasAnchor, m_anchorPt, m_metrics.originals,
        [this](char32_t cp) { return m_metrics.advance(cp); });
    m_lines  = result.lines;
    m_origin = result.firstBaseline;
    viewport()->update();
}

void InlineEditor::setMetricsSource(MetricsSource source)
{
    m_source = std::move(source);
    m_metricsStale = true;
    if (usesEngineLayout()) setLineWrapMode(QTextEdit::NoWrap);
    applyStyle();
    relayout();
}

void InlineEditor::invalidateMetrics()
{
    m_metricsStale = true;
    relayout();
}

void InlineEditor::setLayoutBox(const QSizeF &boundsPt, bool wraps)
{
    m_boundsPt = boundsPt;
    m_wraps    = wraps;
    if (usesEngineLayout()) setLineWrapMode(QTextEdit::NoWrap);
    else setLineWrapMode(wraps ? QTextEdit::WidgetWidth : QTextEdit::NoWrap);
    relayout();
}

void InlineEditor::setPixelOffset(const QPointF &offset)
{
    if (offset == m_pixelOffset) return;
    m_pixelOffset = offset;
    viewport()->update();
}

void InlineEditor::setTextAnchor(bool valid, const QPointF &penOffsetPt)
{
    m_hasAnchor = valid;
    m_anchorPt  = penOffsetPt;
    relayout();
}

int InlineEditor::lineOf(int position) const
{
    int best = 0;
    for (int i = 0; i < m_lines.size(); ++i) {
        if (m_lines.at(i).start > position) break;
        best = i;
    }
    return best;
}

double InlineEditor::advanceTo(const TextLayout::Line &line, int position) const
{
    return TextLayout::xAt(line, m_metricsText, position, m_box.characterSpacingPt,
                           [this](char32_t cp) { return m_metrics.advance(cp); });
}

QPointF InlineEditor::caretPointPt(int position) const
{
    if (m_lines.isEmpty()) return m_origin;
    const TextLayout::Line &line = m_lines.at(lineOf(position));
    return QPointF(m_origin.x() + line.x + advanceTo(line, position),
                   m_origin.y() + line.y);
}

QRectF InlineEditor::caretRectPx(int position) const
{
    const QPointF p = caretPointPt(position) * m_scale + m_pixelOffset;
    const double sizePx = m_metrics.sizePt * m_scale;
    return QRectF(p.x(), p.y() - sizePx * 0.85, qMax(1.0, sizePx / 11.0), sizePx * 1.1);
}

int InlineEditor::positionOnLine(int lineIndex, double xPt) const
{
    if (lineIndex < 0 || lineIndex >= m_lines.size()) return textCursor().position();
    const TextLayout::Line &line = m_lines.at(lineIndex);
    const int end = line.start + line.length;
    int best = line.start;
    double bestDistance = std::abs(xPt);
    for (int i = line.start; i < end;) {
        i += m_metricsText.at(i).isHighSurrogate() && i + 1 < end ? 2 : 1;
        const double d = std::abs(advanceTo(line, i) - xPt);
        if (d < bestDistance) { bestDistance = d; best = i; }
    }
    return best;
}

int InlineEditor::positionAt(const QPoint &viewportPos) const
{
    if (m_lines.isEmpty()) return textCursor().position();
    const double yPt = (viewportPos.y() - m_pixelOffset.y()) / m_scale;
    const double size = m_metrics.sizePt;
    int lineIndex = 0;
    double bestDistance = std::numeric_limits<double>::max();
    for (int i = 0; i < m_lines.size(); ++i) {
        const double d = std::abs(yPt - (m_origin.y() + m_lines.at(i).y - size * 0.3));
        if (d < bestDistance) { bestDistance = d; lineIndex = i; }
    }
    const TextLayout::Line &line = m_lines.at(lineIndex);
    return positionOnLine(lineIndex, (viewportPos.x() - m_pixelOffset.x()) / m_scale
                                         - m_origin.x() - line.x);
}

void InlineEditor::moveCursorTo(int position, bool keepAnchor)
{
    QTextCursor c = textCursor();
    c.setPosition(qBound(0, position, document()->characterCount() - 1),
                  keepAnchor ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
    setTextCursor(c);
    m_caretOn = true;
    if (!m_caretPinned) m_caretTimer->start();
    viewport()->update();
}

void InlineEditor::paintEvent(QPaintEvent *e)
{
    QTextEdit::paintEvent(e);
    if (!usesEngineLayout() || m_lines.isEmpty()) return;
    QPainter p(viewport());
    paintSelection(p);
    if (m_caretOn && (hasFocus() || m_caretPinned)) paintCaret(p);
}

void InlineEditor::paintCaret(QPainter &p) const
{
    p.fillRect(caretRectPx(textCursor().position()),
               m_currentColor.isValid() ? m_currentColor : QColor(Qt::black));
}

QString InlineEditor::plainText() const
{
    return toPlainText();
}

QString InlineEditor::laidOutText() const
{
    if (usesEngineLayout())
        return m_lines.isEmpty() ? plainText()
                                 : TextLayout::withSoftBreaks(plainText(), m_lines);

    (void)document()->size();
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
    if (usesEngineLayout() && m_metrics.isValid()) return m_origin.y() * m_scale;
    const QTextBlock block = document()->firstBlock();
    if (block.isValid() && block.layout() && block.layout()->lineCount() > 0) {
        const QTextLine line = block.layout()->lineAt(0);
        return block.layout()->position().y() + line.y() + line.ascent();
    }
    return QFontMetricsF(styledFont(m_currentFontPx)).ascent();
}

double InlineEditor::contentWidthPt() const
{
    if (usesEngineLayout() && m_metrics.isValid()) {
        double width = 0.0;
        for (const TextLayout::Line &line : m_lines)
            width = qMax(width, m_origin.x() + line.x + line.width);
        return width + m_box.paddingPt;
    }
    qreal breit = 0.0;
    const QFontMetricsF fm(styledFont(m_currentFontPx));
    for (QTextBlock b = document()->begin(); b.isValid(); b = b.next())
        breit = qMax(breit, fm.horizontalAdvance(b.text()) / qMax(0.01, m_scale));
    return breit;
}

double InlineEditor::contentHeightPt() const
{
    if (usesEngineLayout() && m_metrics.isValid()) {
        const double size = m_metrics.sizePt;
        const bool anchored = m_hasAnchor && qFuzzyIsNull(m_box.paddingPt)
                           && m_box.verticalAlign == TextBoxProperties::VerticalAlign::Top;
        const double first = anchored ? m_anchorPt.y() : m_box.paddingPt + size * 0.8;
        const double last  = m_lines.isEmpty() ? 0.0 : m_lines.constLast().y;
        return first + last + size * 0.3 + m_box.paddingPt;
    }
    return document()->size().height() / qMax(0.01, m_scale);
}

void InlineEditor::present(const QString &text, qreal pixelFontSize, const QColor &color,
                           const QString &family, bool bold, bool italic,
                           bool underline)
{
    m_currentColor  = color.isValid() ? color : QColor(0x11, 0x11, 0x11);
    m_currentFontPx = qMax(0.5, qreal(pixelFontSize));
    m_family        = family;
    m_bold          = bold;
    m_italic        = italic;
    m_underline     = underline;
    m_metricsStale  = true;
    m_goalXPt       = -1.0;
    applyStyle();
    setPlainText(TextLayout::withoutSoftBreaks(text));
    applyParagraphSpacing();
    moveCursor(QTextCursor::End);
    relayout();
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

void InlineEditor::paintSelection(QPainter &p) const
{
    const QTextCursor c = textCursor();
    if (!c.hasSelection()) return;
    const int von = qMin(c.anchor(), c.position());
    const int bis = qMax(c.anchor(), c.position());
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(59, 130, 246, 38));
    const double sizePx = m_metrics.sizePt * m_scale;
    for (const TextLayout::Line &line : m_lines) {
        const int a = qMax(von, line.start);
        const int b = qMin(bis, line.start + line.length);
        if (a >= b) continue;
        const double x0 = (m_origin.x() + line.x + advanceTo(line, a)) * m_scale + m_pixelOffset.x();
        const double x1 = (m_origin.x() + line.x + advanceTo(line, b)) * m_scale + m_pixelOffset.x();
        const double baseline = (m_origin.y() + line.y) * m_scale + m_pixelOffset.y();
        p.drawRect(QRectF(x0, baseline - sizePx * 0.85, x1 - x0, sizePx * 1.1));
    }
}

qreal InlineEditor::screenDpi() const
{
    const qreal dpi = logicalDpiY();
    return dpi > 1.0 ? dpi : 96.0;
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
    relayout();
}

void InlineEditor::setGlyphsVisible(bool on)
{
    if (m_glyphs == on) return;
    m_glyphs = on;
    applyStyle();
    relayout();
}

void InlineEditor::setColor(const QColor &color)
{
    if (!color.isValid()) return;
    m_currentColor = color;
    applyStyle();
}

void InlineEditor::setStandardFace(bool on)
{
    if (m_standardFace == on) return;
    m_standardFace = on;
    applyStyle();
}

void InlineEditor::setTextFont(const QString &family, bool bold, bool italic,
                               bool underline)
{
    m_family    = family;
    m_bold      = bold;
    m_italic    = italic;
    m_underline = underline;
    applyStyle();
}

void InlineEditor::mousePressEvent(QMouseEvent *e)
{
    QTextEdit::mousePressEvent(e);
    if (!usesEngineLayout() || e->button() != Qt::LeftButton) return;
    m_goalXPt = -1.0;
    moveCursorTo(positionAt(e->position().toPoint()),
                 e->modifiers() & Qt::ShiftModifier);
}

void InlineEditor::mouseMoveEvent(QMouseEvent *e)
{
    QTextEdit::mouseMoveEvent(e);
    if (!usesEngineLayout() || !(e->buttons() & Qt::LeftButton)) return;
    moveCursorTo(positionAt(e->position().toPoint()), true);
}

void InlineEditor::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (!usesEngineLayout() || e->button() != Qt::LeftButton) {
        QTextEdit::mouseDoubleClickEvent(e);
        return;
    }
    QTextCursor c(document());
    c.setPosition(positionAt(e->position().toPoint()));
    c.select(QTextCursor::WordUnderCursor);
    setTextCursor(c);
    viewport()->update();
}

QVariant InlineEditor::inputMethodQuery(Qt::InputMethodQuery query) const
{
    if (usesEngineLayout() && query == Qt::ImCursorRectangle)
        return caretRectPx(textCursor().position()).translated(viewport()->pos());
    return QTextEdit::inputMethodQuery(query);
}

void InlineEditor::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape) {
        Q_EMIT cancelled();
        return;
    }

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

    const bool plainMove = !(e->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier));
    if (usesEngineLayout() && !m_lines.isEmpty() && plainMove) {
        const bool keep = e->modifiers() & Qt::ShiftModifier;
        const int position = textCursor().position();
        const int lineIndex = lineOf(position);
        const TextLayout::Line line = m_lines.at(lineIndex);
        switch (e->key()) {
        case Qt::Key_Up:
        case Qt::Key_Down: {
            if (m_goalXPt < 0.0) m_goalXPt = caretPointPt(position).x();
            const double goal = m_goalXPt;
            const int target = lineIndex + (e->key() == Qt::Key_Up ? -1 : 1);
            if (target < 0) moveCursorTo(0, keep);
            else if (target >= m_lines.size())
                moveCursorTo(document()->characterCount() - 1, keep);
            else
                moveCursorTo(positionOnLine(target, goal - m_origin.x() - m_lines.at(target).x),
                             keep);
            m_goalXPt = goal;
            return;
        }
        case Qt::Key_Home:
            m_goalXPt = -1.0;
            moveCursorTo(line.start, keep);
            return;
        case Qt::Key_End:
            m_goalXPt = -1.0;
            moveCursorTo(line.start + line.length, keep);
            return;
        default:
            break;
        }
    }
    m_goalXPt = -1.0;
    QTextEdit::keyPressEvent(e);
}

void InlineEditor::wheelEvent(QWheelEvent *e)
{

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
        return;
    }
    QTextEdit::focusOutEvent(e);

    for (QWidget *w = QApplication::focusWidget(); w; w = w->parentWidget()) {
        if (w->objectName() == QLatin1String("TextPanel"))
            return;
    }
    if (!m_committing) {
        m_committing = true;
        Q_EMIT committed(toPlainText());
    }
}
