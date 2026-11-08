#include "ui/notes/NotesPanel.hpp"

#include "ui/notes/NoteColorRow.hpp"
#include "ui/notes/NoteDrag.hpp"
#include "ui/theme/Theme.hpp"

#include <QApplication>
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QtMath>

#include <functional>
#include <utility>

namespace {

// Lines the labels up with the text in the title and note editors.
constexpr int kTextIndent = 3;

QString excerpt(const QString &text)
{
    QString value = text.simplified();
    if (value.size() > 140) value = value.left(137) + QStringLiteral("…");
    return value;
}

class NoteCard : public QFrame
{
public:
    NoteCard(bool selected, std::function<void()> activated, QWidget *parent)
        : QFrame(parent), m_activated(std::move(activated))
    {
        setObjectName(QStringLiteral("NoteCard"));
        setProperty("selected", selected);
        if (!selected && m_activated) setCursor(Qt::PointingHandCursor);
    }

    void setAccent(const QColor &color)
    {
        m_accent = color;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        if (!property("draft").toBool()) {
            QFrame::paintEvent(event);
            if (!m_accent.isValid()) return;
            // The note's color as a stripe along the left edge, inside the rounded border.
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing);
            QPainterPath inside;
            inside.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 9, 9);
            painter.setClipPath(inside);
            painter.fillRect(QRectF(1, 1, 4, height() - 2), m_accent);
            return;
        }
        // A smooth dashed edge; the style sheet's dashes fray at the corners.
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF box = QRectF(rect()).adjusted(0.75, 0.75, -0.75, -0.75);
        painter.setBrush(Theme::DarkMode ? QColor(0x3D, 0x3D, 0x3D) : QColor(Qt::white));
        QPen pen(Theme::DarkMode ? QColor(0x60, 0xA5, 0xFA) : QColor(0x93, 0xC5, 0xFD), 1.3);
        pen.setDashPattern({ 4.0, 3.0 });
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.drawRoundedRect(box, 10, 10);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
            if (m_activated) m_activated();
            event->accept();
            return;
        }
        QFrame::mouseReleaseEvent(event);
    }

private:
    std::function<void()> m_activated;
    QColor                m_accent;
};

class GrowingTextEdit : public QTextEdit
{
public:
    explicit GrowingTextEdit(QWidget *parent) : QTextEdit(parent)
    {
        setAcceptRichText(false);
        setTabChangesFocus(true);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        document()->setDocumentMargin(0);
        connect(this, &QTextEdit::textChanged, this, &GrowingTextEdit::fit);
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QTextEdit::resizeEvent(event);
        fit();
    }

private:
    void fit()
    {
        document()->setTextWidth(viewport()->width());
        const int chrome = height() - viewport()->height();
        setFixedHeight(qBound(20, qCeil(document()->size().height()) + chrome, 240));
    }
};

// Holds the new note's card a little in from the left and tugs it toward the
// page, three times in a row, again and again until the mouse comes over it.
class NudgeSlot : public QWidget
{
public:
    static constexpr int kRest = 14;

    NudgeSlot(bool tugs, QWidget *parent) : QWidget(parent), m_tugs(tugs)
    {
        m_tug = new QVariantAnimation(this);
        m_tug->setDuration(900);
        m_tug->setLoopCount(3);
        m_tug->setStartValue(qreal(kRest));
        m_tug->setKeyValueAt(0.3, qreal(0));
        m_tug->setKeyValueAt(0.65, qreal(kRest));
        m_tug->setKeyValueAt(0.8, qreal(kRest / 2));
        m_tug->setEndValue(qreal(kRest));
        m_tug->setEasingCurve(QEasingCurve::InOutSine);
        connect(m_tug, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            m_offset = value.toReal();
            place();
        });
        m_again = new QTimer(this);
        m_again->setInterval(3500);
        connect(m_again, &QTimer::timeout, this, &NudgeSlot::nudge);
    }

    void setCard(QWidget *card)
    {
        m_card = card;
        card->setParent(this);
        card->installEventFilter(this);
        place();
        if (m_tugs) m_again->start();
    }

    void nudge()
    {
        if (!m_tugs) return;
        m_tug->stop();
        m_tug->start();
    }

    void settle()
    {
        m_again->stop();
        m_tug->stop();
        m_offset = kRest;
        place();
    }

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override
    {
        if (!m_card) return 0;
        const int h = m_card->heightForWidth(width - kRest);
        return h >= 0 ? h : m_card->sizeHint().height();
    }
    QSize sizeHint() const override
    {
        return m_card ? m_card->sizeHint() + QSize(kRest, 0) : QSize();
    }

protected:
    void enterEvent(QEnterEvent *event) override
    {
        QWidget::enterEvent(event);
        settle();
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        place();
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_card && event->type() == QEvent::LayoutRequest) {
            updateGeometry();
            place();
        }
        return QWidget::eventFilter(watched, event);
    }

private:
    void place()
    {
        if (!m_card) return;
        m_card->setGeometry(qRound(m_offset), 0, width() - kRest, heightForWidth(width()));
    }

    QWidget           *m_card { nullptr };
    QVariantAnimation *m_tug { nullptr };
    QTimer            *m_again { nullptr };
    qreal              m_offset { kRest };
    bool               m_tugs { true };
};

QPushButton *cardAction(const QString &icon, const QString &tip, QWidget *parent)
{
    auto *button = new QPushButton(parent);
    button->setObjectName(QStringLiteral("NoteCardAction"));
    button->setIcon(Theme::makeIcon(icon, Theme::IconMuted, Theme::Primary));
    button->setIconSize(QSize(16, 16));
    button->setFixedSize(28, 28);
    button->setToolTip(tip);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QLabel *iconLabel(const QString &icon, const QColor &color, int size, QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setPixmap(Theme::renderSvg(icon, color, size, parent->devicePixelRatioF()));
    label->setFixedWidth(size + 2);
    return label;
}

QLabel *gripLabel(const QString &tip, QWidget *parent)
{
    auto *grip = iconLabel(QStringLiteral("grip-vertical"), Theme::IconMuted, 16, parent);
    grip->setObjectName(QStringLiteral("NoteGrip"));
    grip->setFixedSize(18, 20);
    grip->setAlignment(Qt::AlignCenter);
    grip->setCursor(Qt::OpenHandCursor);
    grip->setToolTip(tip);
    return grip;
}

}

NotesPanel::NotesPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("NotesPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    m_draftColor = NoteColorRow::standard();
    m_palette    = NoteColorRow::defaultPalette();
    setMinimumWidth(320);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 22, 20, 20);
    root->setSpacing(16);

    auto *header = new QHBoxLayout;
    header->setSpacing(8);
    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("NotesTitle"));
    header->addWidget(m_title);
    header->addStretch();
    m_new = new QPushButton(this);
    m_new->setObjectName(QStringLiteral("NotesNew"));
    m_new->setIcon(Theme::makeIcon(QStringLiteral("plus"), Qt::white,
                                   Qt::white, QColor(QStringLiteral("#EFF6FF")), 18));
    m_new->setIconSize(QSize(18, 18));
    m_new->setFixedSize(34, 34);
    m_new->setCursor(Qt::PointingHandCursor);
    connect(m_new, &QPushButton::clicked, this, &NotesPanel::openDraft);
    auto *newDrag = new NoteDrag(m_new);
    watchDrag(newDrag);
    connect(newDrag, &NoteDrag::dropped, this, [this](const QPoint &globalTopLeft) {
        commitEdit();
        m_droppingDraft = false;
        Q_EMIT draftDropped(QString(), QString(), QColor(), globalTopLeft);
    });
    header->addWidget(m_new);
    m_close = new QPushButton(this);
    m_close->setObjectName(QStringLiteral("NotesClose"));
    m_close->setFixedSize(30, 30);
    connect(m_close, &QPushButton::clicked, this, &NotesPanel::closeRequested);
    header->addWidget(m_close);
    root->addLayout(header);

    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("NotesScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_listWidget = new QWidget(m_scroll);
    m_listWidget->setObjectName(QStringLiteral("NotesList"));
    m_listLayout = new QVBoxLayout(m_listWidget);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(12);
    m_scroll->setWidget(m_listWidget);
    root->addWidget(m_scroll, 1);

    retranslateUi();
    refreshTheme();
}

void NotesPanel::setNotes(const QList<NoteData> &notes)
{
    m_notes = notes;
    if (!m_selectedId.isEmpty() && !selectedNote()) m_selectedId.clear();
    rebuildList();
}

void NotesPanel::setSelectedNote(const QString &id)
{
    if (id != m_selectedId) commitEdit();
    m_selectedId = id;
    rebuildList();
    if (!m_textEdit) return;
    m_textEdit->setFocus(Qt::OtherFocusReason);
    m_textEdit->moveCursor(QTextCursor::End);
    m_scroll->ensureWidgetVisible(m_card, 0, 12);
}

void NotesPanel::setDocumentAvailable(bool available)
{
    m_new->setEnabled(available);
    // Without a document the draft cannot be placed; what was typed comes back with the next +.
    if (!available && m_draft) {
        m_draft = false;
        rebuildList();
    }
}

void NotesPanel::openDraft()
{
    commitEdit();
    if (!m_draft) {
        m_draft = true;
        if (m_repeatHints) {
            m_colorHint = true;
            m_hintStage = 1;
        }
        rebuildList();
    }
    m_scroll->ensureVisible(0, 0);
    if (auto *slot = static_cast<NudgeSlot *>(m_draftSlot.data())) slot->nudge();
    if (m_draftTextEdit) m_draftTextEdit->setFocus(Qt::OtherFocusReason);
}

void NotesPanel::closeDraft()
{
    if (m_draftNudgeShown && !m_repeatHints) m_draftNudge = false;
    m_draft = false;
    m_draftTitle.clear();
    m_draftText.clear();
    m_draftColor = NoteColorRow::standard();
    rebuildList();
}

void NotesPanel::draftPlaced(bool placed)
{
    if (!std::exchange(m_droppingDraft, false)) return;
    if (!placed) {
        if (auto *slot = static_cast<NudgeSlot *>(m_draftSlot.data())) slot->nudge();
        return;
    }
    closeDraft();
    if (m_textEdit) m_textEdit->setFocus(Qt::OtherFocusReason);
}

const NoteData *NotesPanel::selectedNote() const
{
    for (const NoteData &note : m_notes)
        if (!note.removed && note.id == m_selectedId) return &note;
    return nullptr;
}

void NotesPanel::select(const QString &id)
{
    commitEdit();
    m_selectedId = id;
    rebuildList();
    Q_EMIT noteSelected(id);
}

void NotesPanel::commitEdit()
{
    const NoteData *note = selectedNote();
    if (!note || !m_titleEdit || !m_textEdit) return;
    const QString title = m_titleEdit->text().trimmed();
    const QString text  = m_textEdit->toPlainText().trimmed();
    if (title == note->title && text == note->text) return;
    // A copy: saving refreshes the list the note lives in.
    const QString id = note->id;
    Q_EMIT saveRequested(id, title, text);
}

void NotesPanel::revertEdit()
{
    const NoteData *note = selectedNote();
    if (!note || !m_titleEdit || !m_textEdit) return;
    m_titleEdit->setText(note->title);
    m_textEdit->setPlainText(note->text);
}

void NotesPanel::watchDrag(NoteDrag *drag)
{
    connect(drag, &NoteDrag::moved, this, &NotesPanel::dragHovered);
}

bool NotesPanel::eventFilter(QObject *watched, QEvent *event)
{
    const bool editor = watched == m_titleEdit.data() || watched == m_textEdit.data();
    if (!editor) return QWidget::eventFilter(watched, event);

    if (event->type() == QEvent::FocusOut) {
        QWidget *next = QApplication::focusWidget();
        if (!m_card || !next || !m_card->isAncestorOf(next)) commitEdit();
    } else if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) {
            revertEdit();
            static_cast<QWidget *>(watched)->clearFocus();
            return true;
        }
        const bool enter = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
        if (enter && watched == m_titleEdit.data()) {
            m_textEdit->setFocus(Qt::TabFocusReason);
            return true;
        }
        if (enter && (key->modifiers() & Qt::ControlModifier)) {
            static_cast<QWidget *>(watched)->clearFocus();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void NotesPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    markHintsShown();
}

void NotesPanel::markHintsShown()
{
    // A hint counts as seen once it was on screen.
    if (m_repeatHints || !isVisible()) return;
    if (m_colorHint && !m_colorHintShown && !m_listWidget->findChildren<NoteColorRow *>().isEmpty()) {
        m_colorHintShown = true;
        Q_EMIT colorHintSeen();
    }
    if (m_draftNudge && !m_draftNudgeShown && m_draftSlot) {
        m_draftNudgeShown = true;
        Q_EMIT draftNudgeSeen();
    }
}

void NotesPanel::hideEvent(QHideEvent *event)
{
    commitEdit();
    // A hint shown once ends when the panel closes, unless it is to come every time.
    if (!m_repeatHints && (m_colorHintShown || m_draftNudgeShown)) {
        m_colorHint = m_colorHint && !m_colorHintShown;
        m_draftNudge = m_draftNudge && !m_draftNudgeShown;
        QTimer::singleShot(0, this, &NotesPanel::rebuildList);
    }
    QWidget::hideEvent(event);
}

NoteColorRow *NotesPanel::makeColorRow(QWidget *parent)
{
    auto *row = new NoteColorRow(parent);
    row->setPalette(m_palette);
    row->setHint(m_colorHint ? m_hintStage : 0);
    if (m_popColor.isValid()) row->pop(m_popColor);
    connect(row, &NoteColorRow::picked, this, &NotesPanel::tookColor);
    connect(row, &NoteColorRow::dragStarted, this, [this]() {
        if (!m_colorHint && !m_repeatHints) return;
        for (NoteColorRow *r : m_listWidget->findChildren<NoteColorRow *>()) r->setDropTargets(true);
    });
    connect(row, &NoteColorRow::dragEnded, this, [this]() {
        for (NoteColorRow *r : m_listWidget->findChildren<NoteColorRow *>()) r->setDropTargets(false);
    });
    row->setMixedColor(m_mixedColor);
    connect(row, &NoteColorRow::mixed, this, [this](const QColor &color) { m_mixedColor = color; });
    connect(row, &NoteColorRow::dropped, this, &NotesPanel::colorDropped);
    connect(row, &NoteColorRow::removed, this, [this](int slot) {
        if (slot < 0 || slot >= m_palette.size()) return;
        m_palette[slot] = QColor();
        Q_EMIT paletteChanged(m_palette);
        QTimer::singleShot(0, this, &NotesPanel::rebuildList);
    });
    connect(row, &NoteColorRow::filled, this, [this](int slot, const QColor &color) {
        if (slot < 0 || slot >= m_palette.size()) return;
        m_palette[slot] = color;
        Q_EMIT paletteChanged(m_palette);
        QTimer::singleShot(0, this, &NotesPanel::rebuildList);
    });
    connect(row, &NoteColorRow::resetRequested, this, [this]() {
        m_palette = NoteColorRow::defaultPalette();
        Q_EMIT paletteChanged(m_palette);
        QTimer::singleShot(0, this, &NotesPanel::rebuildList);
    });
    return row;
}

void NotesPanel::setHints(bool colorHint, bool draftNudge, bool repeat)
{
    m_colorHint = colorHint;
    m_hintStage = 1;
    m_draftNudge = draftNudge;
    m_repeatHints = repeat;
    m_colorHintShown = false;
    m_draftNudgeShown = false;
    rebuildList();
}

void NotesPanel::tookColor(const QColor &color)
{
    // The taken swatch swells in the row built anew; with a color in hand, the
    // hint moves on from the mixer to the swatches it can be dropped on.
    m_popColor = color;
    QTimer::singleShot(400, this, [this]() { m_popColor = QColor(); });
    if (m_colorHint && m_hintStage == 1) m_hintStage = 2;
    QTimer::singleShot(0, this, &NotesPanel::rebuildList);
}

void NotesPanel::colorLearned()
{
    // One color dragged somewhere is enough to know how it works.
    if (!m_colorHint) return;
    m_colorHint = false;
    Q_EMIT colorHintSeen();
    QTimer::singleShot(0, this, &NotesPanel::rebuildList);
}

void NotesPanel::setPalette(const QList<QColor> &colors)
{
    m_palette = colors.mid(0, NoteColorRow::MaxColors);
    while (m_palette.size() < NoteColorRow::MaxColors) m_palette << QColor();
    rebuildList();
}

void NotesPanel::colorDropped(const QColor &color, const QPoint &globalPos, int fromSlot)
{
    // Into a row of colors keeps it, onto a card or the draft colors that note;
    // let go over the page, the page decides. Found inside the panel, not on
    // the screen: on Wayland a global point can name another screen.
    const QPoint local = mapFromGlobal(globalPos);
    colorLearned();
    if (!rect().contains(local)) {
        Q_EMIT colorDroppedOnPage(color, globalPos);
        return;
    }
    for (QWidget *w = childAt(local); w; w = w->parentWidget()) {
        if (const QVariant slot = w->property("swatchSlot"); slot.isValid()) {
            // A bubble of the collection moves to the place it is let go on,
            // trading with what is there; a mixed color fills or replaces it.
            const int to = slot.toInt();
            if (to < 0 || to >= m_palette.size() || to == fromSlot) return;
            if (fromSlot >= 0 && fromSlot < m_palette.size()) m_palette.swapItemsAt(fromSlot, to);
            else m_palette[to] = color;
            Q_EMIT paletteChanged(m_palette);
            rebuildList();
            return;
        }
        if (qobject_cast<NoteColorRow *>(w)) {
            if (fromSlot < 0) keepColor(color);
            return;
        }
        if (const QVariant id = w->property("noteId"); id.isValid()) {
            commitEdit();
            Q_EMIT colorRequested(id.toString(), color);
            return;
        }
        if (w->property("draft").toBool()) {
            m_draftColor = color;
            rebuildList();
            return;
        }
        if (w == this) return;
    }
}

void NotesPanel::keepColor(const QColor &color)
{
    const int free = int(m_palette.indexOf(QColor()));
    if (m_palette.contains(color) || free < 0) return;
    m_palette[free] = color;
    Q_EMIT paletteChanged(m_palette);
    rebuildList();
}

QWidget *NotesPanel::makeDraft()
{
    auto *slot = new NudgeSlot(m_draftNudge, m_listWidget);
    auto *card = new NoteCard(false, {}, slot);
    card->setProperty("draft", true);

    auto *row = new QHBoxLayout(card);
    row->setContentsMargins(8, 12, 12, 10);
    row->setSpacing(6);
    row->addWidget(gripLabel(tr("Drag onto the page to place the note"), card), 0, Qt::AlignTop);
    auto *content = new QVBoxLayout;
    content->setSpacing(6);
    row->addLayout(content, 1);

    auto *title = new QLineEdit(m_draftTitle, card);
    title->setObjectName(QStringLiteral("NoteTitleEdit"));
    title->setPlaceholderText(tr("Title"));
    connect(title, &QLineEdit::textChanged, slot, [this, slot](const QString &t) {
        m_draftTitle = t;
        slot->settle();
    });
    content->addWidget(title);
    auto *text = new GrowingTextEdit(card);
    text->setObjectName(QStringLiteral("NoteTextEdit"));
    text->setPlaceholderText(tr("Write a note…"));
    text->setPlainText(m_draftText);
    connect(text, &QTextEdit::textChanged, slot, [this, text, slot]() {
        m_draftText = text->toPlainText();
        slot->settle();
    });
    content->addWidget(text);
    m_draftTextEdit = text;
    auto *colors = makeColorRow(card);
    colors->setColor(m_draftColor);
    connect(colors, &NoteColorRow::picked, slot, [this, slot](const QColor &color) {
        m_draftColor = color;
        slot->settle();
    });
    content->addWidget(colors);

    auto *footer = new QHBoxLayout;
    footer->setSpacing(4);
    footer->addWidget(iconLabel(QStringLiteral("chevron-left"), Theme::Primary, 14, card));
    auto *hint = new QLabel(tr("Drag onto the page"), card);
    hint->setObjectName(QStringLiteral("NoteDraftHint"));
    footer->addWidget(hint);
    footer->addStretch();
    auto *discard = cardAction(QStringLiteral("x"), tr("Discard"), card);
    connect(discard, &QPushButton::clicked, this, &NotesPanel::closeDraft);
    footer->addWidget(discard);
    content->addLayout(footer);

    auto *drag = new NoteDrag(card);
    watchDrag(drag);
    connect(drag, &NoteDrag::dropped, this, [this](const QPoint &globalTopLeft) {
        commitEdit();
        m_droppingDraft = true;
        Q_EMIT draftDropped(m_draftTitle.trimmed(), m_draftText.trimmed(), m_draftColor,
                            globalTopLeft);
    });
    slot->setCard(card);
    if (!m_draftTitle.isEmpty() || !m_draftText.isEmpty()) slot->settle();
    m_draftSlot = slot;
    return slot;
}

QWidget *NotesPanel::makeCard(const NoteData &note)
{
    const QString id = note.id;
    const bool selected = id == m_selectedId;
    auto *card = new NoteCard(selected, [this, id]() {
        if (id != m_selectedId) select(id);
    }, m_listWidget);

    card->setAccent(note.color);
    card->setProperty("noteId", id);

    auto *row = new QHBoxLayout(card);
    row->setContentsMargins(8, 12, 12, 10);
    row->setSpacing(6);
    row->addWidget(gripLabel(tr("Drag onto the page to move the note"), card), 0, Qt::AlignTop);

    auto *content = new QVBoxLayout;
    content->setSpacing(6);
    row->addLayout(content, 1);

    if (selected) {
        m_card = card;
        m_titleEdit = new QLineEdit(note.title, card);
        m_titleEdit->setObjectName(QStringLiteral("NoteTitleEdit"));
        m_titleEdit->setPlaceholderText(tr("Title"));
        m_titleEdit->installEventFilter(this);
        content->addWidget(m_titleEdit);
        m_textEdit = new GrowingTextEdit(card);
        m_textEdit->setObjectName(QStringLiteral("NoteTextEdit"));
        m_textEdit->setPlaceholderText(tr("Write a note…"));
        m_textEdit->setPlainText(note.text);
        m_textEdit->installEventFilter(this);
        content->addWidget(m_textEdit);
        auto *colors = makeColorRow(card);
        colors->setColor(note.color);
        colors->setToolTip(tr("Note color"));
        connect(colors, &NoteColorRow::picked, this, [this, id](const QColor &color) {
            commitEdit();
            Q_EMIT colorRequested(id, color);
        });
        content->addWidget(colors);
    } else {
        auto *title = new QLabel(note.title.isEmpty() ? tr("Untitled note") : note.title, card);
        title->setObjectName(QStringLiteral("NoteCardTitle"));
        title->setTextFormat(Qt::PlainText);
        title->setIndent(kTextIndent);
        title->setWordWrap(true);
        content->addWidget(title);
        if (!note.text.trimmed().isEmpty()) {
            auto *body = new QLabel(excerpt(note.text), card);
            body->setObjectName(QStringLiteral("NoteCardBody"));
            body->setTextFormat(Qt::PlainText);
            body->setIndent(kTextIndent);
            body->setWordWrap(true);
            content->addWidget(body);
        }
    }

    auto *footer = new QHBoxLayout;
    footer->setSpacing(2);
    const bool linked = !note.textRects.isEmpty();
    if (linked) footer->addWidget(iconLabel(QStringLiteral("link"), Theme::IconMuted, 13, card));
    auto *where = new QLabel(card);
    where->setObjectName(QStringLiteral("NoteCardMeta"));
    where->setTextFormat(Qt::PlainText);
    where->ensurePolished();
    // The quote gets what the footer leaves, so the card never grows wider than the panel.
    const QString quote = where->fontMetrics().elidedText(
        note.quote.simplified(), Qt::ElideRight, selected ? 80 : 180);
    where->setText(linked ? tr("“%1” · p. %2").arg(quote).arg(note.page + 1)
                          : tr("Page %1").arg(note.page + 1));

    if (linked) where->setToolTip(note.quote);
    else where->setIndent(kTextIndent);
    footer->addWidget(where);
    if (selected && linked) {
        auto *unlink = cardAction(QStringLiteral("x"), tr("Unlink from the text"), card);
        unlink->setFixedSize(22, 22);
        unlink->setIconSize(QSize(13, 13));
        connect(unlink, &QPushButton::clicked, this, [this, id]() {
            commitEdit();
            Q_EMIT unlinkRequested(id);
        });
        footer->addWidget(unlink);
    }
    footer->addStretch();
    if (selected) {
        auto *pin = cardAction(QStringLiteral("pin"),
                               note.pinned ? tr("Unpin") : tr("Pin to top"), card);
        pin->setCheckable(true);
        pin->setChecked(note.pinned);
        connect(pin, &QPushButton::clicked, this, [this, id, pinned = note.pinned]() {
            commitEdit();
            Q_EMIT pinRequested(id, !pinned);
        });
        footer->addWidget(pin);
        auto *locate = cardAction(QStringLiteral("map-pin"), tr("Show on page"), card);
        connect(locate, &QPushButton::clicked, this, [this, id]() {
            commitEdit();
            Q_EMIT noteSelected(id);
        });
        footer->addWidget(locate);
        auto *remove = cardAction(QStringLiteral("trash-2"), tr("Delete note"), card);
        connect(remove, &QPushButton::clicked, this, [this, id]() {
            Q_EMIT deleteRequested(id);
        });
        footer->addWidget(remove);
    } else if (note.pinned) {
        auto *pinned = iconLabel(QStringLiteral("pin"), Theme::Primary, 14, card);
        pinned->setToolTip(tr("Pinned"));
        footer->addWidget(pinned);
    }
    content->addLayout(footer);

    auto *drag = new NoteDrag(card);
    watchDrag(drag);
    connect(drag, &NoteDrag::dropped, this, [this, id](const QPoint &globalTopLeft) {
        commitEdit();
        Q_EMIT placeRequested(id, globalTopLeft);
    });
    return card;
}

void NotesPanel::rebuildList()
{
    m_card.clear();
    m_titleEdit.clear();
    m_textEdit.clear();
    m_draftSlot.clear();
    m_draftTextEdit.clear();
    while (QLayoutItem *item = m_listLayout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }

    if (m_draft) m_listLayout->addWidget(makeDraft());
    int visible = 0;
    for (const NoteData &note : std::as_const(m_notes)) {
        if (note.removed) continue;
        ++visible;
        m_listLayout->addWidget(makeCard(note));
    }
    if (visible == 0 && !m_draft) {
        auto *empty = new QWidget(m_listWidget);
        auto *column = new QVBoxLayout(empty);
        column->setContentsMargins(12, 40, 12, 12);
        column->setSpacing(10);
        auto *icon = iconLabel(QStringLiteral("message-square"), Theme::IconMuted, 28, empty);
        icon->setMaximumWidth(QWIDGETSIZE_MAX);
        icon->setMinimumWidth(0);
        icon->setAlignment(Qt::AlignCenter);
        column->addWidget(icon);
        auto *text = new QLabel(
            tr("No notes yet.\nClick + and drag the new note onto the page. Let go over text to link it."),
            empty);
        text->setObjectName(QStringLiteral("NotesEmpty"));
        text->setAlignment(Qt::AlignCenter);
        text->setWordWrap(true);
        column->addWidget(text);
        m_listLayout->addWidget(empty);
    }
    m_listLayout->addStretch();
    markHintsShown();
}

void NotesPanel::retranslateUi()
{
    m_title->setText(tr("Notes"));
    m_close->setToolTip(tr("Close notes"));
    m_new->setToolTip(tr("New note: click, then drag it onto the page"));
    rebuildList();
}

void NotesPanel::refreshTheme()
{
    m_close->setIcon(Theme::makeIcon(QStringLiteral("x"), Theme::IconMuted));
    rebuildList();
}
