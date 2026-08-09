#include "ToolPanel.hpp"

#include <QLabel>
#include <QVBoxLayout>

ToolPanel::ToolPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ToolPanel"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(4);

    const struct Section { const char *title; const char *hint; } sections[] = {
        { "Dokument", "Metadaten, Seitenanzahl …" },
        { "Seite",    "Format, Ausrichtung, Ränder …" },
        { "Objekt",   "Kein Objekt ausgewählt." },
    };

    for (const auto &s : sections) {
        layout->addWidget(makeSectionHeader(QString::fromUtf8(s.title)));
        layout->addWidget(makeSectionContent(QString::fromUtf8(s.hint)));
        layout->addSpacing(4);
    }

    layout->addStretch(1);
}

QWidget *ToolPanel::makeSectionHeader(const QString &title)
{
    auto *header = new QLabel(title);
    header->setObjectName(QStringLiteral("ToolPanelSectionHeader"));
    header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return header;
}

QWidget *ToolPanel::makeSectionContent(const QString &placeholderText)
{
    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("ToolPanelSectionContent"));

    auto *inner = new QVBoxLayout(content);
    inner->setContentsMargins(8, 4, 8, 8);
    inner->setSpacing(0);

    auto *label = new QLabel(placeholderText);
    label->setObjectName(QStringLiteral("ToolPanelPlaceholderLabel"));
    label->setWordWrap(true);
    inner->addWidget(label);

    return content;
}
