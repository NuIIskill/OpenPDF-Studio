#include "ui/edit/InlineEditor.hpp"

#include "engine/edit/StandardFont.hpp"
#include "engine/edit/TextWrap.hpp"

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

    QString family = m_family;
    family.remove(u'\'').remove(u'"');
    QStringList kandidaten;
    if (!m_standardFace && !family.isEmpty()) kandidaten << family;
    kandidaten << StandardFont::qtFamilies(StandardFont::kindOf(m_family));
    QStringList zitiert;
    for (const QString &k : std::as_const(kandidaten))
        zitiert << QStringLiteral("'%1'").arg(k);
    const QString familyList = zitiert.join(QStringLiteral(", "));
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

void InlineEditor::paintEvent(QPaintEvent *e)
{
    QTextEdit::paintEvent(e);
    if (!m_glyphs && m_advance) {
        QPainter auswahl(viewport());
        paintSelection(auswahl);
    }

    if (m_glyphs || !m_caretOn || (!hasFocus() && !m_caretPinned)) return;

    QRect caret = cursorRect();

    const QTextCursor c = textCursor();
    const QTextBlock blk = c.block();
    if (const QTextLayout *layout = blk.layout()) {
        const QTextLine line = layout->lineForTextPosition(c.positionInBlock());
        if (line.isValid()) {
            const qreal x = engineX(blk, line, c.positionInBlock());
            if (x >= 0.0) caret.moveLeft(qRound(x));
        }
    }
    caret.setWidth(qMax(1, qRound(m_currentFontPx / 11.0)));

    caret.moveLeft(qBound(0, caret.left(),
                          qMax(0, viewport()->width() - caret.width())));
    QPainter p(viewport());
    p.fillRect(caret, m_currentColor.isValid() ? m_currentColor : QColor(Qt::black));
}

QString InlineEditor::laidOutText() const
{

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
    const QTextBlock block = document()->firstBlock();
    if (block.isValid() && block.layout() && block.layout()->lineCount() > 0) {
        const QTextLine line = block.layout()->lineAt(0);
        return block.layout()->position().y() + line.y() + line.ascent();
    }
    return QFontMetricsF(styledFont(m_currentFontPx)).ascent();
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

qreal InlineEditor::engineX(const QTextBlock &block, const QTextLine &line,
                            int posInBlock) const
{
    if (!m_advance) return -1.0;
    QTextCursor anfang(document());
    anfang.setPosition(block.position() + line.textStart());
    const double breite = advancePt(
        QStringView(block.text()).mid(line.textStart(),
                                      posInBlock - line.textStart()));
    if (breite < 0.0) return -1.0;
    return cursorRect(anfang).left() + breite * m_scale;
}

void InlineEditor::paintSelection(QPainter &p) const
{
    const QTextCursor c = textCursor();
    if (!c.hasSelection()) return;
    const int von = qMin(c.anchor(), c.position());
    const int bis = qMax(c.anchor(), c.position());
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(59, 130, 246, 38));
    for (QTextBlock block = document()->findBlock(von);
         block.isValid() && block.position() <= bis; block = block.next()) {
        const QTextLayout *layout = block.layout();
        if (!layout) continue;
        for (int i = 0; i < layout->lineCount(); ++i) {
            const QTextLine line = layout->lineAt(i);
            const int start = block.position() + line.textStart();
            const int ende  = start + line.textLength();
            const int a = qMax(von, start), b = qMin(bis, ende);
            if (a >= b) continue;
            const qreal x0 = engineX(block, line, a - block.position());
            const qreal x1 = engineX(block, line, b - block.position());
            if (x0 < 0.0 || x1 <= x0) continue;
            QTextCursor at(document());
            at.setPosition(start);
            const QRect zeile = cursorRect(at);
            p.drawRect(QRectF(x0, zeile.top(), x1 - x0, zeile.height()));
        }
    }
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
        double w = advancePt(zeile);
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
    m_charWidth.clear();
    viewport()->update();
}

double InlineEditor::advancePt(QStringView text) const
{
    if (!m_advance) return -1.0;
    double breite = 0.0;
    for (const QChar ch : text) {
        const double w = charWidth(ch);
        if (w < 0.0) return -1.0;
        breite += w;
    }
    return breite;
}

double InlineEditor::charWidth(QChar ch) const
{
    if (!m_advance) return -1.0;
    const uint cp = ch.unicode();
    auto it = m_charWidth.constFind(cp);
    if (it == m_charWidth.cend())
        it = m_charWidth.insert(cp, m_advance(QString(ch)));
    return *it;
}

int InlineEditor::engineLineCount(qreal widthPt) const
{
    if (!m_advance || widthPt <= 0.0) return -1;
    int zeilen = 0;
    for (QTextBlock block = document()->begin(); block.isValid();
         block = block.next()) {
        const QString text = block.text();
        if (advancePt(text) < 0.0) return -1;
        zeilen += TextWrap::lines(text, widthPt,
                                  [this](QChar ch) { return charWidth(ch); },
                                  m_box.characterSpacingPt).size();
    }
    return qMax(1, zeilen);
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

int InlineEditor::positionAt(const QPoint &viewportPos) const
{
    const QTextCursor grob = cursorForPosition(viewportPos);
    const QTextBlock blk = grob.block();
    const QTextLayout *layout = blk.layout();
    if (!m_advance || !layout) return grob.position();
    const QTextLine line = layout->lineForTextPosition(grob.positionInBlock());
    if (!line.isValid()) return grob.position();

    QTextCursor anfang(document());
    anfang.setPosition(blk.position() + line.textStart());
    const double x0 = cursorRect(anfang).left();

    const QStringView zeile = QStringView(blk.text())
                                  .mid(line.textStart(), line.textLength());
    int    beste   = 0;
    double abstand = qAbs(x0 - viewportPos.x());
    double breite  = 0.0;
    for (int i = 0; i < zeile.size(); ++i) {
        const double w = advancePt(zeile.mid(i, 1));
        if (w < 0.0) return grob.position();
        breite += w;
        const double d = qAbs(x0 + breite * m_scale - viewportPos.x());
        if (d < abstand) { abstand = d; beste = i + 1; }
    }
    return blk.position() + line.textStart() + beste;
}

void InlineEditor::mousePressEvent(QMouseEvent *e)
{
    QTextEdit::mousePressEvent(e);
    if (m_glyphs || !m_advance || e->button() != Qt::LeftButton) return;
    QTextCursor c = textCursor();
    c.setPosition(positionAt(e->position().toPoint()),
                  e->modifiers() & Qt::ShiftModifier ? QTextCursor::KeepAnchor
                                                     : QTextCursor::MoveAnchor);
    setTextCursor(c);
}

void InlineEditor::mouseMoveEvent(QMouseEvent *e)
{
    QTextEdit::mouseMoveEvent(e);
    if (m_glyphs || !m_advance || !(e->buttons() & Qt::LeftButton)) return;
    QTextCursor c = textCursor();
    c.setPosition(positionAt(e->position().toPoint()), QTextCursor::KeepAnchor);
    setTextCursor(c);
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
