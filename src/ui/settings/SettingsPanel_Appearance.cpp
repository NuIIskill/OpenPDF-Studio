#include "ui/settings/SettingsPanel.hpp"
#include "ui/settings/OptionCard.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

QWidget *SettingsPanel::buildAppearancePage()
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("SettingsScrollContent"));
    auto *vbox = new QVBoxLayout(page);
    vbox->setContentsMargins(32, 28, 32, 32);
    vbox->setSpacing(0);

    auto *title = new QLabel(tr("Appearance"), page);
    title->setObjectName(QStringLiteral("SettingsSectionTitle"));
    vbox->addWidget(title);
    vbox->addSpacing(6);

    auto *desc = new QLabel(tr("Choose how OpenPDF Studio is displayed."), page);
    desc->setObjectName(QStringLiteral("SettingsSectionDesc"));
    vbox->addWidget(desc);
    vbox->addSpacing(24);

    auto *row = new QHBoxLayout;
    row->setSpacing(14);
    row->setAlignment(Qt::AlignLeft);

    struct T { const char *icon, *id, *title, *desc; };
    const T themes[] = {
        { "monitor", "system", QT_TR_NOOP("System"), QT_TR_NOOP("Follows system settings") },
        { "sun",     "light",  QT_TR_NOOP("Light"),  QT_TR_NOOP("Light user interface") },
        { "moon",    "dark",   QT_TR_NOOP("Dark"),   QT_TR_NOOP("Dark user interface") },
    };
    for (const auto &t : themes)
        row->addWidget(buildOptionCard(
            QLatin1String(t.icon), tr(t.title), tr(t.desc),
            QLatin1String(t.id), m_themeCards, m_themeIds, true));
    row->addStretch(1);
    vbox->addLayout(row);
    vbox->addStretch(1);
    return page;
}
