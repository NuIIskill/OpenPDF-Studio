#include "ui/export/ExportFormatList.hpp"
#include "ui/theme/Theme.hpp"

#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace {

/// A group's title row, as tall as its icon and texts need.
class GroupHeader : public QPushButton
{
public:
    QSize sizeHint() const override { return layout()->sizeHint(); }
    QSize minimumSizeHint() const override { return layout()->minimumSize(); }
};

}

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

void ExportFormatList::addGroup(const QString &id, const QString &title, bool expanded)
{
    if (!m_groups.isEmpty()) {
        auto *line = new QFrame;
        line->setObjectName(QStringLiteral("XDivider"));
        line->setFixedHeight(1);
        m_layout->insertWidget(m_layout->count() - 1, line);
    }

    auto *header = new GroupHeader;
    header->setObjectName(QStringLiteral("XGroup"));
    header->setCursor(Qt::PointingHandCursor);
    auto *row = new QHBoxLayout(header);
    row->setContentsMargins(4, 8, 10, 8);
    row->setSpacing(10);
    Group group;
    group.id = id;
    group.chevron = new QLabel;
    group.chevron->setAttribute(Qt::WA_TransparentForMouseEvents);
    row->addWidget(group.chevron, 0, Qt::AlignTop);
    auto *texts = new QVBoxLayout;
    texts->setSpacing(2);
    auto *name = new QLabel(title);
    name->setObjectName(QStringLiteral("XGroupTitle"));
    name->setAttribute(Qt::WA_TransparentForMouseEvents);
    texts->addWidget(name);
    group.formats = new QLabel;
    group.formats->setObjectName(QStringLiteral("XFormatNote"));
    group.formats->setWordWrap(true);
    group.formats->setAttribute(Qt::WA_TransparentForMouseEvents);
    texts->addWidget(group.formats);
    row->addLayout(texts, 1);
    m_layout->insertWidget(m_layout->count() - 1, header);

    group.items = new QWidget;
    group.layout = new QVBoxLayout(group.items);
    group.layout->setContentsMargins(0, 0, 0, 8);
    group.layout->setSpacing(4);
    m_layout->insertWidget(m_layout->count() - 1, group.items);

    m_groups.append(group);
    const qsizetype index = m_groups.size() - 1;
    setExpanded(m_groups[index], expanded);
    connect(header, &QPushButton::clicked, this, [this, index] {
        setExpanded(m_groups[index], m_groups[index].items->isHidden());
    });
}

void ExportFormatList::setExpanded(Group &group, bool expanded)
{
    group.items->setVisible(expanded);
    group.formats->setVisible(!expanded);
    group.chevron->setPixmap(Theme::renderSvg(
        expanded ? QStringLiteral("chevron-down") : QStringLiteral("chevron-right"),
        Theme::IconNormal, 18, devicePixelRatioF()));
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
    Group &group = m_groups.last();
    group.layout->addWidget(item);
    group.names.append(title);
    group.formats->setText(group.names.join(QStringLiteral(", ")));
    item->setProperty("formatGroup", m_groups.size() - 1);

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

QStringList ExportFormatList::expandedGroups() const
{
    QStringList ids;
    for (const Group &group : m_groups)
        if (!group.items->isHidden()) ids.append(group.id);
    return ids;
}
