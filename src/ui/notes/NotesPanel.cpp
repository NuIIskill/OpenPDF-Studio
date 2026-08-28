#include "ui/notes/NotesPanel.hpp"

#include "ui/theme/Theme.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

namespace {

QString excerpt(const QString &text)
{
    QString value = text.simplified();
    if (value.size() > 82) value = value.left(79) + QStringLiteral("…");
    return value;
}

class NoteCard : public QFrame
{
public:
    NoteCard(const NoteData &note, bool selected,
             std::function<void()> activated, QWidget *parent)
        : QFrame(parent), m_activated(std::move(activated))
    {
        setObjectName(QStringLiteral("NoteCard"));
        setProperty("selected", selected);
        setCursor(Qt::PointingHandCursor);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 10, 12, 9);
        layout->setSpacing(7);

        auto *top = new QHBoxLayout;
        top->setSpacing(7);
        auto *dot = new QLabel(QStringLiteral("●"), this);
        dot->setObjectName(QStringLiteral("NoteDot"));
        dot->setFixedWidth(12);
        top->addWidget(dot);
        auto *title = new QLabel(note.title.isEmpty()
                                     ? NotesPanel::tr("Untitled note") : note.title, this);
        title->setObjectName(QStringLiteral("NoteCardTitle"));
        title->setTextFormat(Qt::PlainText);
        top->addWidget(title, 1);
        if (note.pinned) {
            auto *pin = new QLabel(QStringLiteral("◆"), this);
            pin->setObjectName(QStringLiteral("NotePinMarker"));
            top->addWidget(pin);
        }
        layout->addLayout(top);

        if (!note.text.trimmed().isEmpty()) {
            auto *body = new QLabel(excerpt(note.text), this);
            body->setObjectName(QStringLiteral("NoteCardBody"));
            body->setWordWrap(true);
            body->setTextFormat(Qt::PlainText);
            layout->addWidget(body);
        }

        auto *meta = new QHBoxLayout;
        auto *page = new QLabel(NotesPanel::tr("Page %1").arg(note.page + 1), this);
        page->setObjectName(QStringLiteral("NoteCardMeta"));
        meta->addWidget(page);
        meta->addStretch();
        auto *time = new QLabel(note.modified.isValid()
            ? note.modified.toString(QStringLiteral("HH:mm")) : QString(), this);
        time->setObjectName(QStringLiteral("NoteCardMeta"));
        meta->addWidget(time);
        layout->addLayout(meta);
    }

protected:
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
};

} // namespace

NotesPanel::NotesPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("NotesPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumWidth(320);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 14);
    root->setSpacing(12);

    auto *header = new QHBoxLayout;
    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("NotesTitle"));
    header->addWidget(m_title);
    header->addStretch();
    m_close = new QPushButton(this);
    m_close->setObjectName(QStringLiteral("NotesClose"));
    m_close->setIcon(Theme::makeIcon(QStringLiteral("x"), Theme::IconMuted));
    m_close->setFixedSize(30, 30);
    connect(m_close, &QPushButton::clicked, this, &NotesPanel::closeRequested);
    header->addWidget(m_close);
    root->addLayout(header);

    m_new = new QPushButton(this);
    m_new->setObjectName(QStringLiteral("NotesNew"));
    m_new->setIcon(Theme::makeIcon(QStringLiteral("plus"), Qt::white,
                                   Qt::white, QColor(QStringLiteral("#93C5FD")), 17));
    connect(m_new, &QPushButton::clicked, this, &NotesPanel::newNoteRequested);
    root->addWidget(m_new, 0, Qt::AlignLeft);

    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("NotesScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_listWidget = new QWidget(m_scroll);
    m_listWidget->setObjectName(QStringLiteral("NotesList"));
    m_listLayout = new QVBoxLayout(m_listWidget);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(10);
    m_empty = new QLabel(m_listWidget);
    m_empty->setObjectName(QStringLiteral("NotesEmpty"));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    m_listLayout->addWidget(m_empty);
    m_listLayout->addStretch();
    m_scroll->setWidget(m_listWidget);
    root->addWidget(m_scroll, 1);

    m_editor = new QWidget(this);
    m_editor->setObjectName(QStringLiteral("NoteEditor"));
    auto *editorLayout = new QVBoxLayout(m_editor);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(8);
    m_titleEdit = new QLineEdit(m_editor);
    m_titleEdit->setObjectName(QStringLiteral("NoteTitleEdit"));
    editorLayout->addWidget(m_titleEdit);
    m_textEdit = new QTextEdit(m_editor);
    m_textEdit->setObjectName(QStringLiteral("NoteTextEdit"));
    m_textEdit->setMinimumHeight(82);
    m_textEdit->setMaximumHeight(120);
    editorLayout->addWidget(m_textEdit);
    auto *actions = new QHBoxLayout;
    actions->setSpacing(8);
    m_pin = new QPushButton(m_editor);
    m_pin->setObjectName(QStringLiteral("NoteSecondaryButton"));
    connect(m_pin, &QPushButton::clicked, this, [this]() {
        if (const NoteData *note = selectedNote())
            Q_EMIT pinRequested(note->id, !note->pinned);
    });
    actions->addWidget(m_pin);
    m_delete = new QPushButton(m_editor);
    m_delete->setObjectName(QStringLiteral("NoteSecondaryButton"));
    connect(m_delete, &QPushButton::clicked, this, [this]() {
        if (!m_selectedId.isEmpty()) Q_EMIT deleteRequested(m_selectedId);
    });
    actions->addWidget(m_delete);
    actions->addStretch();
    m_cancel = new QPushButton(m_editor);
    m_cancel->setObjectName(QStringLiteral("NoteSecondaryButton"));
    connect(m_cancel, &QPushButton::clicked, this, &NotesPanel::showSelection);
    actions->addWidget(m_cancel);
    m_save = new QPushButton(m_editor);
    m_save->setObjectName(QStringLiteral("NoteSaveButton"));
    connect(m_save, &QPushButton::clicked, this, [this]() {
        if (!m_selectedId.isEmpty())
            Q_EMIT saveRequested(m_selectedId, m_titleEdit->text().trimmed(),
                                 m_textEdit->toPlainText().trimmed());
    });
    actions->addWidget(m_save);
    editorLayout->addLayout(actions);
    root->addWidget(m_editor);

    retranslateUi();
    refreshTheme();
    showSelection();
}

void NotesPanel::setNotes(const QList<NoteData> &notes)
{
    m_notes = notes;
    if (!m_selectedId.isEmpty() && !selectedNote()) m_selectedId.clear();
    rebuildList();
    showSelection();
}

void NotesPanel::setSelectedNote(const QString &id)
{
    m_selectedId = id;
    rebuildList();
    showSelection();
    if (!id.isEmpty()) m_textEdit->setFocus(Qt::OtherFocusReason);
}

void NotesPanel::setDocumentAvailable(bool available)
{
    m_new->setEnabled(available);
}

const NoteData *NotesPanel::selectedNote() const
{
    for (const NoteData &note : m_notes)
        if (!note.removed && note.id == m_selectedId) return &note;
    return nullptr;
}

void NotesPanel::rebuildList()
{
    while (QLayoutItem *item = m_listLayout->takeAt(0)) {
        if (QWidget *widget = item->widget()) widget->deleteLater();
        delete item;
    }

    int visible = 0;
    for (const NoteData &note : m_notes) {
        if (note.removed) continue;
        ++visible;
        const QString id = note.id;
        m_listLayout->addWidget(new NoteCard(note, id == m_selectedId, [this, id]() {
            m_selectedId = id;
            rebuildList();
            showSelection();
            Q_EMIT noteSelected(id);
        }, m_listWidget));
    }
    m_empty = new QLabel(visible == 0
        ? tr("No notes yet. Add one and place it on the current page.") : QString(),
        m_listWidget);
    m_empty->setObjectName(QStringLiteral("NotesEmpty"));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    m_empty->setVisible(visible == 0);
    m_listLayout->addWidget(m_empty);
    m_listLayout->addStretch();
}

void NotesPanel::showSelection()
{
    const NoteData *note = selectedNote();
    m_editor->setVisible(note != nullptr);
    if (!note) return;
    m_titleEdit->setText(note->title);
    m_textEdit->setPlainText(note->text);
    m_pin->setText(note->pinned ? tr("Unpin") : tr("Pin"));
}

void NotesPanel::retranslateUi()
{
    m_title->setText(tr("Notes"));
    m_close->setToolTip(tr("Close notes"));
    m_new->setText(tr("New note"));
    m_titleEdit->setPlaceholderText(tr("Title"));
    m_textEdit->setPlaceholderText(tr("Write a note…"));
    m_delete->setText(tr("Delete"));
    m_cancel->setText(tr("Cancel"));
    m_save->setText(tr("Save"));
    rebuildList();
    showSelection();
}

void NotesPanel::refreshTheme()
{
    m_close->setIcon(Theme::makeIcon(QStringLiteral("x"), Theme::IconMuted));
}
