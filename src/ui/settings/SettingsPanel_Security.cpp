#include "ui/settings/SettingsPanel.hpp"
#include "app/AppSettings.hpp"

#include <QAbstractButton>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

QWidget *SettingsPanel::buildSecurityPage()
{
    QWidget *page = nullptr;
    QVBoxLayout *vl = buildScrollPage(page, tr("Security"),
        tr("Protection for documents and external content."));

    QVBoxLayout *edit = addSettingsCard(vl, tr("Edit Protection"));
    m_readModeCheck = addSettingsCheck(edit, tr("Open new documents in reading mode"),
        tr("Changes require explicit approval."),
        m_settings->openInReadMode());

    QVBoxLayout *external = addSettingsCard(vl, tr("External Content"));
    QWidget *card = external->parentWidget();
    auto *row = new QHBoxLayout;
    row->setSpacing(12);
    auto *text = new QVBoxLayout;
    text->setSpacing(2);
    auto *label = new QLabel(tr("Open external links"), card);
    label->setObjectName(QStringLiteral("SettingsRowLabel"));
    QFont bold = label->font();
    bold.setBold(true);
    label->setFont(bold);
    text->addWidget(label);
    auto *desc = new QLabel(card);
    desc->setObjectName(QStringLiteral("SettingsRowDesc"));
    desc->setWordWrap(true);
    text->addWidget(desc);
    row->addLayout(text, 1);

    m_externalLinksCombo = new QComboBox(card);
    m_externalLinksCombo->setObjectName(QStringLiteral("AdvancedCombo"));
    m_externalLinksCombo->setFixedWidth(220);
    m_externalLinksCombo->addItem(tr("Ask"),          QStringLiteral("ask"));
    m_externalLinksCombo->addItem(tr("Always open"),  QStringLiteral("always"));
    m_externalLinksCombo->addItem(tr("Never open"),   QStringLiteral("never"));
    selectComboData(m_externalLinksCombo, m_settings->externalLinks());
    row->addWidget(m_externalLinksCombo, 0, Qt::AlignVCenter);
    external->addLayout(row);

    const auto describe = [this, desc]() {
        const QString policy = m_externalLinksCombo->currentData().toString();
        desc->setText(policy == QLatin1String("always")
            ? tr("Links open in the browser right away.")
            : policy == QLatin1String("never")
            ? tr("Links in documents are not opened.")
            : tr("Confirmation before switching to the browser."));
    };
    describe();
    connect(m_externalLinksCombo, &QComboBox::currentIndexChanged, this, describe);

    finishScrollPage(vl);
    return page;
}
