#include "ui/export/ExportFormatList.hpp"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

ExportFormatList::ExportFormatList(QWidget *parent)
    : QWidget(parent)
    , m_group(new QButtonGroup(this))
    , m_layout(new QVBoxLayout(this))
{
    m_group->setExclusive(true);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(4);

    auto *heading = new QLabel(tr("Export to"));
    heading->setObjectName(QStringLiteral("XSideTitle"));
    m_layout->addWidget(heading);
    m_layout->addSpacing(8);
    m_layout->addStretch(1);
}

void ExportFormatList::addFormat(const QString &id, const QPixmap &icon, const QString &title,
                                 const QString &subtitle, bool available)
{
    auto *item = new QPushButton;
    item->setObjectName(QStringLiteral("XFormat"));
    item->setCheckable(true);
    item->setEnabled(available);
    item->setFixedHeight(76);
    item->setCursor(available ? Qt::PointingHandCursor : Qt::ArrowCursor);
    item->setProperty("formatId", id);
    if (!available) item->setToolTip(tr("Not available yet."));

    auto *row = new QHBoxLayout(item);
    row->setContentsMargins(14, 0, 10, 0);
    row->setSpacing(14);
    auto *glyph = new QLabel;
    glyph->setPixmap(icon);
    glyph->setFixedWidth(icon.width() / qMax(1.0, icon.devicePixelRatio()));
    glyph->setAttribute(Qt::WA_TransparentForMouseEvents);
    row->addWidget(glyph, 0, Qt::AlignVCenter);

    auto *texts = new QVBoxLayout;
    texts->setSpacing(2);
    auto *name = new QLabel(title);
    name->setObjectName(QStringLiteral("XFormatTitle"));
    name->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *note = new QLabel(subtitle);
    note->setObjectName(QStringLiteral("XFormatNote"));
    note->setWordWrap(true);
    note->setAttribute(Qt::WA_TransparentForMouseEvents);
    texts->addStretch(1);
    texts->addWidget(name);
    texts->addWidget(note);
    texts->addStretch(1);
    row->addLayout(texts, 1);

    m_group->addButton(item);
    m_layout->insertWidget(m_layout->count() - 1, item);

    connect(item, &QPushButton::toggled, this, [this, item, name, id](bool on) {
        name->setProperty("current", on);
        name->style()->unpolish(name);
        name->style()->polish(name);
        item->setCursor(on || !item->isEnabled() ? Qt::ArrowCursor : Qt::PointingHandCursor);
        if (on) Q_EMIT formatChanged(id);
    });
}

void ExportFormatList::select(const QString &id)
{
    for (QAbstractButton *item : m_group->buttons())
        if (item->property("formatId").toString() == id && item->isEnabled()) {
            item->setChecked(true);
            return;
        }
}
